#include <cstdio>
#include <cmath>
#include <cstring>
#include <imgui/imgui.h>
#include "offsets/buttons.hpp"
#include "features.h"
#include "memory.h"
#include "globals.h"

// ============================================================================
// BONE INDICES & SKELETON CONNECTIONS
// Verified from user's Bone Explorer screenshots:
// 0=root, 1=spine_0, 2=spine_1, 3=spine_2, 4=spine_3, 5=neck, 6=head
// 8=left_clavicle, 9=left_arm_upper, 10=left_arm_lower, 11=left_hand
// 13=right_clavicle, 14=right_arm_upper, 15=right_arm_lower, 16=right_hand
// 17=left_hip, 18=left_knee, 19=left_foot
// 20=right_hip, 21=right_knee, 22=right_foot
// ============================================================================
namespace bones {
    constexpr int HEAD        = 6;
    constexpr int NECK        = 5;
    constexpr int SPINE3      = 4;
    constexpr int SPINE2      = 3;
    constexpr int SPINE1      = 2;
    constexpr int SPINE0      = 1;
    constexpr int PELVIS      = 0;

    constexpr int CLAVICLE_L  = 8;
    constexpr int ARM_UPPER_L = 9;
    constexpr int ARM_LOWER_L = 10;
    constexpr int HAND_L      = 11;

    constexpr int CLAVICLE_R  = 13;
    constexpr int ARM_UPPER_R = 14;
    constexpr int ARM_LOWER_R = 15;
    constexpr int HAND_R      = 16;

    constexpr int HIP_L       = 17;
    constexpr int KNEE_L      = 18;
    constexpr int FOOT_L      = 19;

    constexpr int HIP_R       = 20;
    constexpr int KNEE_R      = 21;
    constexpr int FOOT_R      = 22;

    constexpr int CONNECTIONS[][2] = {
        {HEAD, NECK},       // 0
        {NECK, SPINE3},     // 1
        {SPINE3, SPINE2},   // 2
        {SPINE2, SPINE1},   // 3
        {SPINE1, SPINE0},   // 4
        {NECK, CLAVICLE_L}, // 5
        {CLAVICLE_L, ARM_UPPER_L},  // 6
        {ARM_UPPER_L, ARM_LOWER_L}, // 7
        {ARM_LOWER_L, HAND_L},      // 8
        {NECK, CLAVICLE_R}, // 9
        {CLAVICLE_R, ARM_UPPER_R},  // 10
        {ARM_UPPER_R, ARM_LOWER_R}, // 11
        {ARM_LOWER_R, HAND_R},      // 12
        {SPINE0, HIP_L},    // 13  -- connect spine directly to hip, skip pelvis
        {HIP_L, KNEE_L},    // 14
        {KNEE_L, FOOT_L},   // 15
        {SPINE0, HIP_R},    // 16  -- connect spine directly to hip, skip pelvis
        {HIP_R, KNEE_R},    // 17
        {KNEE_R, FOOT_R},   // 18
    };
    constexpr int NUM_CONNECTIONS = sizeof(CONNECTIONS) / sizeof(CONNECTIONS[0]);

    constexpr int ALL_BONES[] = {
        HEAD, NECK, SPINE3, SPINE2, SPINE1, SPINE0,
        CLAVICLE_L, ARM_UPPER_L, ARM_LOWER_L, HAND_L,
        CLAVICLE_R, ARM_UPPER_R, ARM_LOWER_R, HAND_R,
        HIP_L, KNEE_L, FOOT_L, HIP_R, KNEE_R, FOOT_R
    };
    constexpr int NUM_BONES = sizeof(ALL_BONES) / sizeof(ALL_BONES[0]);
}

// ============================================================================
// Bone format structs
// ============================================================================
struct CTransform {
    float pos[3];
    float rot[4];
};

// ============================================================================
// Memory safety
// ============================================================================
static bool IsRangeReadable(void* ptr, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;
    if ((std::uintptr_t)ptr + len > (std::uintptr_t)mbi.BaseAddress + mbi.RegionSize)
        return false;
    return true;
}

// ============================================================================
// CACHED BONE STATE
// cachedBoneState: [baseType:4 bits][format:2 bits][strideType:2 bits][offset:8 bits]
// 0 = not scanned yet
// ============================================================================
static constexpr int FMT_MATRIX3X4 = 1;
static constexpr int FMT_CTRANSFORM = 2;
static std::atomic<int> g_CachedBoneState{ 0 };
static std::atomic<bool> g_BoneScanFailed{ false };

// ============================================================================
// BUNNYHOP HELPERS
// ============================================================================
static uintptr_t GetLocalPawn()
{
    HMODULE clientDll = GetModuleHandleA("client.dll");
    if (!clientDll) return 0;
    uintptr_t clientBase = reinterpret_cast<uintptr_t>(clientDll);
    uintptr_t localPawnPtr = clientBase + cs2_offsets::dwLocalPlayerPawn;
    if (!IsValidPtrFast(reinterpret_cast<void*>(localPawnPtr))) return 0;
    uintptr_t localPawn = SafeRead<uintptr_t>(reinterpret_cast<uintptr_t*>(localPawnPtr), 0);
    if (!localPawn || !IsInValidRange(localPawn)) return 0;
    return localPawn;
}

static bool CanJump(uintptr_t pawn)
{
    uint8_t moveType = SafeRead<uint8_t>(reinterpret_cast<uint8_t*>(pawn + cs2_offsets::m_MoveType), 0);
    if (moveType == 9 || moveType == 8 || moveType == 10) return false;
    float waterLevel = SafeRead<float>(reinterpret_cast<float*>(pawn + cs2_offsets::m_flWaterLevel), 0.0f);
    if (waterLevel >= 2.0f) return false;
    return true;
}

static bool IsOnGround(uintptr_t pawn)
{
    uint32_t flags = SafeRead<uint32_t>(reinterpret_cast<uint32_t*>(pawn + cs2_offsets::m_fFlags), 0);
    return (flags & 1) != 0;
}

void DoBhop(CUserCmd* cmd)
{
    if (!cmd || !g_BhopEnabled.load()) return;
    if (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) return;
    uintptr_t localPawn = GetLocalPawn();
    if (!localPawn || !CanJump(localPawn)) return;
    bool onGround = IsOnGround(localPawn);
    cmd->nButtons.nValue       &= ~IN_JUMP;
    cmd->nButtons.nValueScroll &= ~IN_JUMP;
    if (onGround) {
        cmd->nButtons.nValue       |= IN_JUMP;
        cmd->nButtons.nValueScroll |= IN_JUMP;
    }
}

// ============================================================================
// ESP MATH & HELPERS
// ============================================================================
bool WorldToScreen(const Vector3& worldPos, Vector2& screenPos, const ViewMatrix& vm, float width, float height)
{
    float w = vm.m[3][0] * worldPos.x + vm.m[3][1] * worldPos.y + vm.m[3][2] * worldPos.z + vm.m[3][3];
    if (w < 0.001f) return false;
    float invW = 1.0f / w;
    float x = vm.m[0][0] * worldPos.x + vm.m[0][1] * worldPos.y + vm.m[0][2] * worldPos.z + vm.m[0][3];
    float y = vm.m[1][0] * worldPos.x + vm.m[1][1] * worldPos.y + vm.m[1][2] * worldPos.z + vm.m[1][3];
    screenPos.x = (width  / 2.0f) * (1.0f + x * invW);
    screenPos.y = (height / 2.0f) * (1.0f - y * invW);
    return true;
}

Vector3 GetEntityOriginFast(std::uintptr_t entity)
{
    if (!entity || !IsInValidRange(entity)) return { 0, 0, 0 };
    Vector3 origin = SafeRead<Vector3>(reinterpret_cast<Vector3*>(entity + cs2_offsets::m_vOldOrigin), {0,0,0});
    if (origin.x != 0 || origin.y != 0 || origin.z != 0) return origin;
    std::uintptr_t gameSceneNode = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(entity + cs2_offsets::m_pGameSceneNode), 0);
    if (gameSceneNode && IsInValidRange(gameSceneNode)) {
        origin = SafeRead<Vector3>(reinterpret_cast<Vector3*>(gameSceneNode + 0xC8), {0,0,0});
        if (origin.x != 0 || origin.y != 0 || origin.z != 0) return origin;
    }
    return { 0, 0, 0 };
}

void GetPlayerNameFast(std::uintptr_t controller, char* outName, size_t outSize)
{
    if (!controller || !IsInValidRange(controller) || outSize < 2) {
        outName[0] = '?'; outName[1] = '\0'; return;
    }
    std::uintptr_t namePtr = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(controller + cs2_offsets::m_sSanitizedPlayerName), 0);
    if (!namePtr || !IsInValidRange(namePtr)) {
        outName[0] = '?'; outName[1] = '\0'; return;
    }
    const char* name = reinterpret_cast<const char*>(namePtr);
    size_t i = 0;
    for (; i < outSize - 1 && i < 31; i++) {
        char c = name[i];
        if (c == '\0') break;
        outName[i] = c;
    }
    outName[i] = '\0';
}

std::uintptr_t GetEntityFromListFast(std::uintptr_t entityList, int index)
{
    if (!entityList || !IsInValidRange(entityList) || index < 0 || index > 0x7FFF) return 0;
    int chunkIndex = (index >> 9);
    int entryIndex = (index & 0x1FF);
    std::uintptr_t chunkPtrAddr = entityList + 0x10 + 0x8 * chunkIndex;
    if (!IsValidPtrFast(reinterpret_cast<void*>(chunkPtrAddr))) return 0;
    std::uintptr_t listEntry = SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(chunkPtrAddr), 0);
    if (!listEntry || !IsInValidRange(listEntry)) return 0;
    std::uintptr_t entityAddr = listEntry + 0x70 * entryIndex;
    if (!IsValidPtrFast(reinterpret_cast<void*>(entityAddr))) return 0;
    std::uintptr_t result = SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(entityAddr), 0);
    if (result && IsInValidRange(result)) return result;
    return 0;
}

bool IsValidPlayerPawnFast(std::uintptr_t pawn, std::uintptr_t localPawn)
{
    if (!pawn || pawn == localPawn) return false;
    if (!IsInValidRange(pawn)) return false;
    int health = SafeRead<int>(reinterpret_cast<int*>(pawn + cs2_offsets::m_iHealth), -1);
    int team   = SafeRead<int>(reinterpret_cast<int*>(pawn + cs2_offsets::m_iTeamNum), 0);
    Vector3 origin = GetEntityOriginFast(pawn);
    if (health <= 0 || health > 200) return false;
    if (team != 2 && team != 3)     return false;
    if (origin.x == 0 && origin.y == 0 && origin.z == 0) return false;
    return true;
}

// ============================================================================
// BONE READING
// ============================================================================

static Vector3 ReadBoneAt(std::uintptr_t boneArrayPtr, int boneIndex, int format, size_t stride)
{
    std::uintptr_t addr = boneArrayPtr + boneIndex * stride;
    if (!IsRangeReadable(reinterpret_cast<void*>(addr), 12)) return { 0, 0, 0 };
    if (format == FMT_CTRANSFORM) {
        CTransform* t = reinterpret_cast<CTransform*>(addr);
        return { t->pos[0], t->pos[1], t->pos[2] };
    } else {
        float* m = reinterpret_cast<float*>(addr);
        return { m[3], m[7], m[11] };
    }
}

static bool IsFiniteVec(const Vector3& v)
{
    return !std::isnan(v.x) && !std::isnan(v.y) && !std::isnan(v.z)
        && !std::isinf(v.x) && !std::isinf(v.y) && !std::isinf(v.z);
}

static int CountValidBones(std::uintptr_t boneArrayPtr, int format, size_t stride, const Vector3& origin)
{
    int validCount = 0;
    // Test a wide range of bones to catch any valid model
    int testBones[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22 };
    const int NUM_TEST = sizeof(testBones) / sizeof(testBones[0]);

    for (int i = 0; i < NUM_TEST; i++) {
        Vector3 pos = ReadBoneAt(boneArrayPtr, testBones[i], format, stride);
        if (!IsFiniteVec(pos)) continue;
        if (pos.x == 0 && pos.y == 0 && pos.z == 0) continue;
        float dx = pos.x - origin.x;
        float dy = pos.y - origin.y;
        float dz = pos.z - origin.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        // Very lenient: 10-150 units (covers all player bones)
        if (dist < 10.0f || dist > 150.0f) continue;
        validCount++;
    }
    return validCount;
}

static bool TestBoneConfig(std::uintptr_t baseAddr, std::uintptr_t offset,
                           int format, size_t stride, const Vector3& origin, Vector3& outHead)
{
    if (!baseAddr || !IsInValidRange(baseAddr)) return false;
    std::uintptr_t ptrAddr = baseAddr + offset;
    if (!IsRangeReadable(reinterpret_cast<void*>(ptrAddr), sizeof(std::uintptr_t))) return false;

    std::uintptr_t boneArrayPtr = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(ptrAddr), 0);
    if (!boneArrayPtr || !IsInValidRange(boneArrayPtr)) return false;

    int validBones = CountValidBones(boneArrayPtr, format, stride, origin);
    if (validBones < 2) return false; // Need at least 2 valid bones

    outHead = ReadBoneAt(boneArrayPtr, bones::HEAD, format, stride);
    return IsFiniteVec(outHead) && (outHead.x != 0 || outHead.y != 0 || outHead.z != 0);
}

static void ScanForBoneOffset(std::uintptr_t entity)
{
    if (g_BoneScanFailed.load()) return;
    if (g_CachedBoneState.load() != 0) return;

    g_DbgBoneScanning.store(true);

    Vector3 origin = GetEntityOriginFast(entity);

    std::uintptr_t gameSceneNode = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(entity + cs2_offsets::m_pGameSceneNode), 0);
    if (!gameSceneNode || !IsInValidRange(gameSceneNode)) {
        g_DbgBoneScanning.store(false);
        return;
    }

    std::uintptr_t child = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(gameSceneNode + 0x40), 0);
    std::uintptr_t modelStateViaGsn = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(gameSceneNode + 0x150), 0);
    std::uintptr_t modelStateViaChild = (child && IsInValidRange(child))
        ? SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(child + 0x150), 0) : 0;

    struct BaseInfo { std::uintptr_t addr; int type; };
    BaseInfo bases[] = {
        { gameSceneNode,       0 },
        { child,               1 },
        { modelStateViaGsn,    2 },
        { modelStateViaChild,  3 },
        { entity,              4 },
    };
    const int NUM_BASES = 5;

    struct StrideInfo { size_t stride; int type; };
    StrideInfo strides[] = { {0x30, 0}, {0x20, 1}, {0x1C, 2}, {0x40, 3} };
    const int NUM_STRIDES = 4;

    int formats[] = { FMT_MATRIX3X4, FMT_CTRANSFORM };
    const int NUM_FORMATS = 2;

    for (int bi = 0; bi < NUM_BASES; bi++)
    {
        if (!bases[bi].addr || !IsInValidRange(bases[bi].addr)) continue;

        for (std::uintptr_t off = 0x0; off <= 0x400; off += 8)
        {
            for (int fi = 0; fi < NUM_FORMATS; fi++)
            {
                for (int si = 0; si < NUM_STRIDES; si++)
                {
                    Vector3 headPos;
                    if (TestBoneConfig(bases[bi].addr, off, formats[fi], strides[si].stride, origin, headPos))
                    {
                        int offIdx = (int)(off / 8);
                        int state = (bases[bi].type << 12) | (formats[fi] << 10) | (strides[si].type << 8) | offIdx;
                        g_CachedBoneState.store(state);
                        g_DbgBoneScanResult.store((int)off);
                        g_DbgBoneScanChild.store(bases[bi].type == 1 || bases[bi].type == 3);
                        g_DbgBoneScanFormat.store(formats[fi]);
                        g_DbgBoneScanning.store(false);
                        g_DbgBoneHeadX.store(headPos.x);
                        g_DbgBoneHeadY.store(headPos.y);
                        g_DbgBoneHeadZ.store(headPos.z);
                        return;
                    }
                }
            }
        }
    }

    g_BoneScanFailed.store(true);
    g_DbgBoneScanning.store(false);
    g_DbgBoneScanResult.store(-1);
}

static Vector3 ReadCachedBone(std::uintptr_t entity, int boneIndex);

static void EnsureBoneScan(std::uintptr_t firstEntity)
{
    if (g_CachedBoneState.load() != 0) return;

    // Try hardcoded known-good config first (gameSceneNode + 0x1D0, Matrix3x4, stride 0x30)
    // cachedState encoding: [baseType:4][format:2][strideType:2][offset/8:8]
    // baseType=0 (gameSceneNode), format=1 (Matrix3x4), strideType=0 (0x30), offset=0x1D0/8=58
    int hardcodedState = (0 << 12) | (1 << 10) | (0 << 8) | 58;
    g_CachedBoneState.store(hardcodedState);

    Vector3 headPos = ReadCachedBone(firstEntity, bones::HEAD);
    if (IsFiniteVec(headPos) && (headPos.x != 0 || headPos.y != 0 || headPos.z != 0)) {
        // Hardcoded config works - head is valid
        g_DbgBoneScanResult.store(0x1D0);
        g_DbgBoneScanFormat.store(FMT_MATRIX3X4);
        g_DbgBoneScanning.store(false);
        g_DbgBoneHeadX.store(headPos.x);
        g_DbgBoneHeadY.store(headPos.y);
        g_DbgBoneHeadZ.store(headPos.z);
        return;
    }

    // Hardcoded didn't work, try auto-scanning
    g_CachedBoneState.store(0);
    g_BoneScanFailed.store(false);
    ScanForBoneOffset(firstEntity);
}

static Vector3 ReadCachedBone(std::uintptr_t entity, int boneIndex)
{
    int cachedState = g_CachedBoneState.load();
    if (cachedState == 0) return { 0, 0, 0 };

    int baseType   = (cachedState >> 12) & 0xF;
    int format     = (cachedState >> 10) & 0x3;
    int strideType = (cachedState >> 8)  & 0x3;
    int offIdx     = cachedState & 0xFF;
    std::uintptr_t offset = (std::uintptr_t)offIdx * 8;

    std::uintptr_t gameSceneNode = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(entity + cs2_offsets::m_pGameSceneNode), 0);
    if (!gameSceneNode || !IsInValidRange(gameSceneNode)) return { 0, 0, 0 };

    std::uintptr_t base = 0;
    switch (baseType) {
        case 0: base = gameSceneNode; break;
        case 1: base = SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(gameSceneNode + 0x40), 0); break;
        case 2: base = SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(gameSceneNode + 0x150), 0); break;
        case 3: {
            std::uintptr_t c = SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(gameSceneNode + 0x40), 0);
            base = (c && IsInValidRange(c)) ? SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(c + 0x150), 0) : 0;
            break;
        }
        case 4: base = entity; break;
    }

    if (!base || !IsInValidRange(base)) return { 0, 0, 0 };

    std::uintptr_t ptr = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(base + offset), 0);
    if (!ptr || !IsInValidRange(ptr)) return { 0, 0, 0 };

    size_t stride = 0x30;
    if (strideType == 1) stride = 0x20;
    else if (strideType == 2) stride = 0x1C;
    else if (strideType == 3) stride = 0x40;

    return ReadBoneAt(ptr, boneIndex, format, stride);
}

Vector3 GetBonePosition(std::uintptr_t entity, int boneIndex)
{
    if (!entity || !IsInValidRange(entity)) return { 0, 0, 0 };
    if (boneIndex < 0 || boneIndex > 100) return { 0, 0, 0 };

    Vector3 origin = GetEntityOriginFast(entity);
    if (origin.x == 0 && origin.y == 0 && origin.z == 0) return { 0, 0, 0 };

    // Try to get gameSceneNode from entity
    std::uintptr_t gameSceneNode = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(entity + cs2_offsets::m_pGameSceneNode), 0);
    if (!gameSceneNode || !IsInValidRange(gameSceneNode)) return { 0, 0, 0 };

    // Try multiple bone array offsets from gameSceneNode
    std::uintptr_t boneOffsets[] = { 0x1D0, 0x1C0, 0x1E0, 0x200, 0x180, 0x1B0, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270, 0x280, 0x290, 0x2A0, 0x2B0, 0x2C0, 0x2D0, 0x2E0, 0x2F0, 0x300 };
    const int NUM_BONE = sizeof(boneOffsets) / sizeof(boneOffsets[0]);

    // Try both Matrix3x4 and CTransform formats
    struct FormatInfo { int format; size_t stride; };
    FormatInfo formats[] = { {FMT_MATRIX3X4, 0x30}, {FMT_CTRANSFORM, 0x20} };
    const int NUM_FORMATS = sizeof(formats) / sizeof(formats[0]);

    for (int b = 0; b < NUM_BONE; b++) {
        std::uintptr_t boneArray = SafeRead<std::uintptr_t>(
            reinterpret_cast<std::uintptr_t*>(gameSceneNode + boneOffsets[b]), 0);
        if (!boneArray || !IsInValidRange(boneArray)) continue;

        for (int f = 0; f < NUM_FORMATS; f++) {
            Vector3 pos = ReadBoneAt(boneArray, boneIndex, formats[f].format, formats[f].stride);
            if (!IsFiniteVec(pos)) continue;
            if (pos.x == 0 && pos.y == 0 && pos.z == 0) continue;

            // Verify bone is near entity origin (not random garbage)
            float dx = pos.x - origin.x;
            float dy = pos.y - origin.y;
            float dz = pos.z - origin.z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < 10.0f || dist > 150.0f) continue; // Too close or too far

            // Found valid bone!
            if (boneIndex == bones::HEAD) {
                g_DbgBoneHeadX.store(pos.x);
                g_DbgBoneHeadY.store(pos.y);
                g_DbgBoneHeadZ.store(pos.z);
            }
            return pos;
        }
    }

    return { 0, 0, 0 };
}

// ============================================================================
// ESP UPDATE
// ============================================================================
void UpdateEspData(float screenW, float screenH)
{
    HMODULE clientDll = GetModuleHandleA("client.dll");
    if (!clientDll) return;

    std::vector<EspRenderData> renderData;
    renderData.reserve(16);
    const auto clientBase = reinterpret_cast<std::uintptr_t>(clientDll);

    std::uintptr_t localPawn = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(clientBase + cs2_offsets::dwLocalPlayerPawn), 0);
    if (!localPawn || !IsInValidRange(localPawn)) return;

    int localTeam = SafeRead<int>(reinterpret_cast<int*>(localPawn + cs2_offsets::m_iTeamNum), 0);
    Vector3 localOrigin = GetEntityOriginFast(localPawn);
    if (localOrigin.x == 0 && localOrigin.y == 0 && localOrigin.z == 0) return;

    std::uintptr_t entityList = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(clientBase + cs2_offsets::dwEntityList), 0);
    if (!entityList || !IsInValidRange(entityList)) return;

    std::uintptr_t vmAddr = clientBase + cs2_offsets::dwViewMatrix;
    if (!IsValidPtrFast(reinterpret_cast<void*>(vmAddr))) return;
    ViewMatrix vm = SafeRead<ViewMatrix>(reinterpret_cast<ViewMatrix*>(vmAddr), {});

    int maxDist     = g_EspMaxDistance.load();
    bool drawBoxes  = g_EspBoxes.load();
    bool drawHealth = g_EspHealth.load();
    bool drawNames  = g_EspNames.load();
    bool drawSkeleton = g_EspSkeleton.load();

    std::uintptr_t seenPawns[64] = {};
    int seenCount = 0;
    bool scannedThisFrame = false;

    for (int i = 1; i <= 64; i++)
    {
        std::uintptr_t controller = GetEntityFromListFast(entityList, i);
        if (!controller || !IsInValidRange(controller)) continue;

        std::uint32_t raw32 = SafeRead<std::uint32_t>(
            reinterpret_cast<std::uint32_t*>(controller + cs2_offsets::m_hPlayerPawn), 0);
        if (raw32 == 0) {
            raw32 = SafeRead<std::uint32_t>(reinterpret_cast<std::uint32_t*>(controller + 0x60C), 0);
            if (raw32 == 0) {
                raw32 = SafeRead<std::uint32_t>(reinterpret_cast<std::uint32_t*>(controller + 0x7EC), 0);
            }
        }
        if (raw32 == 0) continue;

        uint32_t indexBits = raw32 & 0x7FFF;
        int handleIdx = (int)indexBits;
        if (handleIdx <= 0 || handleIdx >= 0x7FFF) continue;

        std::uintptr_t pawn = GetEntityFromListFast(entityList, handleIdx);
        if (!pawn) continue;

        bool alreadySeen = false;
        for (int s = 0; s < seenCount; s++) {
            if (seenPawns[s] == pawn) { alreadySeen = true; break; }
        }
        if (alreadySeen) continue;
        if (seenCount < 64) seenPawns[seenCount++] = pawn;

        if (pawn == localPawn) continue;
        if (!IsValidPlayerPawnFast(pawn, localPawn)) continue;

        if (!scannedThisFrame) {
            EnsureBoneScan(pawn);
            scannedThisFrame = true;
        }

        int health    = SafeRead<int>(reinterpret_cast<int*>(pawn + cs2_offsets::m_iHealth), 0);
        int maxHealth = SafeRead<int>(reinterpret_cast<int*>(pawn + cs2_offsets::m_iMaxHealth), 100);
        int team      = SafeRead<int>(reinterpret_cast<int*>(pawn + cs2_offsets::m_iTeamNum), 0);
        Vector3 origin = GetEntityOriginFast(pawn);
        float dist = (origin - localOrigin).Length();

        if (maxDist > 0 && dist > maxDist) continue;

        Vector3 headPos = origin;
        headPos.z += 72.0f;
        Vector2 screenHead, screenFeet;
        if (!WorldToScreen(headPos, screenHead, vm, screenW, screenH)) continue;
        if (!WorldToScreen(origin, screenFeet, vm, screenW, screenH)) continue;

        float boxHeight = screenFeet.y - screenHead.y;
        if (boxHeight < 5.0f) continue;
        float boxWidth  = boxHeight * 0.4f;
        float boxX      = screenHead.x - boxWidth / 2.0f;
        float boxY      = screenHead.y;

        if (boxX + boxWidth < 0 || boxY + boxHeight < 0 || boxX > screenW || boxY > screenH)
            continue;

        EspRenderData data = {};
        data.boxX = boxX; data.boxY = boxY; data.boxW = boxWidth; data.boxH = boxHeight;
        data.isEnemy = (team != localTeam);
        data.health  = health;

        if (drawHealth) {
            data.healthPct = static_cast<float>(health) / static_cast<float>(maxHealth);
            // Avoid potential macro collisions with min/max on Windows by using explicit clamps
            if (data.healthPct < 0.0f) data.healthPct = 0.0f;
            else if (data.healthPct > 1.0f) data.healthPct = 1.0f;
            data.barW = 4.0f;
            data.barH = boxHeight;
            data.barX = boxX - data.barW - 3.0f;
            data.barY = boxY;
            data.healthBarFillH = data.barH * data.healthPct;
        }

        if (drawNames) {
            GetPlayerNameFast(controller, data.name, sizeof(data.name));
            data.hasName = (data.name[0] != '\0' && data.name[0] != '?');
            float nameWidth = strlen(data.name) * 7.0f;
            data.nameX = screenHead.x - nameWidth / 2.0f;
            data.nameY = screenHead.y - 14.0f;
        }

        snprintf(data.distText, sizeof(data.distText), "%.0f", dist);
        float distWidth = strlen(data.distText) * 7.0f;
        data.distX = screenFeet.x - distWidth / 2.0f;
        data.distY = screenFeet.y + 2.0f;

        // Hardcoded skeleton: read all mapped bones and store screen positions
        if (drawSkeleton) {
            for (int b = 0; b < bones::NUM_BONES && b < 20; b++) {
                Vector3 boneWorld = GetBonePosition(pawn, bones::ALL_BONES[b]);
                // Reject zeroed bones
                if (boneWorld.x == 0 && boneWorld.y == 0 && boneWorld.z == 0) {
                    data.bones[b].visible = false;
                    continue;
                }
                // Reject bones far from entity (garbage)
                float dX = boneWorld.x - origin.x;
                float dY = boneWorld.y - origin.y;
                float dZ = boneWorld.z - origin.z;
                if ((dX*dX + dY*dY + dZ*dZ) > (200.0f * 200.0f)) {
                    data.bones[b].visible = false;
                    continue;
                }
                Vector2 boneScreen;
                if (WorldToScreen(boneWorld, boneScreen, vm, screenW, screenH)) {
                    data.bones[b].x = boneScreen.x;
                    data.bones[b].y = boneScreen.y;
                    data.bones[b].visible = true;
                } else {
                    data.bones[b].visible = false;
                }
            }
            data.boneCount = bones::NUM_BONES;
        } else {
            data.boneCount = 0;
        }

        // Explorer: read all bones 0-31 for mapping discovery
        data.explorerBoneCount = 0;
        for (int b = 0; b < 32; b++) {
            Vector3 boneWorld = GetBonePosition(pawn, b);
            if (boneWorld.x == 0 && boneWorld.y == 0 && boneWorld.z == 0) {
                data.explorerBones[b].visible = false;
                continue;
            }
            float dX = boneWorld.x - origin.x;
            float dY = boneWorld.y - origin.y;
            float dZ = boneWorld.z - origin.z;
            if ((dX*dX + dY*dY + dZ*dZ) > (200.0f * 200.0f)) {
                data.explorerBones[b].visible = false;
                continue;
            }
            Vector2 boneScreen;
            if (WorldToScreen(boneWorld, boneScreen, vm, screenW, screenH)) {
                data.explorerBones[b].x = boneScreen.x;
                data.explorerBones[b].y = boneScreen.y;
                data.explorerBones[b].visible = true;
                data.explorerWorld[b].x = boneWorld.x;
                data.explorerWorld[b].y = boneWorld.y;
                data.explorerWorld[b].z = boneWorld.z;
                data.explorerWorld[b].visible = true;
                data.explorerBoneCount++;
            } else {
                data.explorerBones[b].visible = false;
                data.explorerWorld[b].visible = false;
            }
        }

        renderData.push_back(data);
    }

    g_DbgEntityCount.store(seenCount);
    int cachedState = g_CachedBoneState.load();
    g_DbgBoneReadOK.store(cachedState != 0 ? 1 : 0);

    int writeIdx = g_EspWriteIdx.load();
    g_EspRenderBuffer[writeIdx].swap(renderData);
    g_EspEntityCount.store((int)g_EspRenderBuffer[writeIdx].size());
    g_EspWriteIdx.store(1 - writeIdx);
}

// ============================================================================
// ESP RENDER
// ============================================================================
void RenderEspFast(ImDrawList* drawList)
{
    if (!g_EspEnabled.load()) return;

    int readIdx = 1 - g_EspWriteIdx.load();
    const auto& entities = g_EspRenderBuffer[readIdx];
    if (entities.empty()) return;

    bool drawBoxes  = g_EspBoxes.load();
    bool drawHealth = g_EspHealth.load();
    bool drawNames  = g_EspNames.load();
    bool drawSkeleton = g_EspSkeleton.load();

    for (const auto& ent : entities)
    {
        ImU32 boxColor = ent.isEnemy ? IM_COL32(255, 60, 60, 140) : IM_COL32(60, 255, 60, 140);

        if (drawBoxes) {
            drawList->AddRect(
                ImVec2(ent.boxX, ent.boxY),
                ImVec2(ent.boxX + ent.boxW, ent.boxY + ent.boxH),
                boxColor, 2.0f, 0, 1.5f
            );
        }

        if (drawHealth) {
            drawList->AddRectFilled(
                ImVec2(ent.barX, ent.barY),
                ImVec2(ent.barX + ent.barW, ent.barY + ent.barH),
                IM_COL32(0, 0, 0, 180)
            );

            ImU32 healthColor;
            if (ent.healthPct > 0.6f) healthColor = IM_COL32(50, 255, 50, 255);
            else if (ent.healthPct > 0.3f) healthColor = IM_COL32(255, 255, 50, 255);
            else healthColor = IM_COL32(255, 50, 50, 255);

            drawList->AddRectFilled(
                ImVec2(ent.barX, ent.barY + ent.barH - ent.healthBarFillH),
                ImVec2(ent.barX + ent.barW, ent.barY + ent.barH),
                healthColor
            );

            char healthText[8];
            snprintf(healthText, sizeof(healthText), "%d", ent.health);
            ImVec2 textSize = ImGui::CalcTextSize(healthText);
            drawList->AddText(
                ImVec2(ent.barX - textSize.x - 2.0f,
                       ent.barY + ent.barH - ent.healthBarFillH - textSize.y / 2.0f),
                IM_COL32(255, 255, 255, 255), healthText
            );
        }

        if (drawNames && ent.hasName) {
            drawList->AddText(ImVec2(ent.nameX, ent.nameY), IM_COL32(255, 255, 255, 255), ent.name);
        }

        drawList->AddText(ImVec2(ent.distX, ent.distY), IM_COL32(200, 200, 200, 200), ent.distText);

        if (drawSkeleton) {
            // Skeleton: thick bright lines when bones are valid
            ImU32 skelColor = ent.isEnemy ? IM_COL32(255, 80, 80, 200) : IM_COL32(80, 255, 80, 200);
            int visibleConnections = 0;

            for (int c = 0; c < bones::NUM_CONNECTIONS; c++) {
                int b1 = bones::CONNECTIONS[c][0];
                int b2 = bones::CONNECTIONS[c][1];
                int idx1 = -1, idx2 = -1;
                for (int b = 0; b < ent.boneCount; b++) {
                    if (bones::ALL_BONES[b] == b1) idx1 = b;
                    if (bones::ALL_BONES[b] == b2) idx2 = b;
                }
                if (idx1 >= 0 && idx2 >= 0 && ent.bones[idx1].visible && ent.bones[idx2].visible) {
                    drawList->AddLine(
                        ImVec2(ent.bones[idx1].x, ent.bones[idx1].y),
                        ImVec2(ent.bones[idx2].x, ent.bones[idx2].y),
                        skelColor, 2.0f
                    );
                    visibleConnections++;
                }
            }

            // Fallback: magenta cross when bones are not available (clearly distinct from skeleton)
            if (visibleConnections == 0) {
                float cx = ent.boxX + ent.boxW / 2.0f;
                float cy = ent.boxY + ent.boxH / 2.0f;
                ImU32 fallbackColor = IM_COL32(255, 0, 255, 180);
                // Draw small X in center of box
                drawList->AddLine(ImVec2(cx - 5, cy - 5), ImVec2(cx + 5, cy + 5), fallbackColor, 2.0f);
                drawList->AddLine(ImVec2(cx + 5, cy - 5), ImVec2(cx - 5, cy + 5), fallbackColor, 2.0f);
            }
        }

        // Bone Explorer: draw index labels on all valid bones
        if (g_DbgBoneExplorer.load()) {
            for (int b = 0; b < 32; b++) {
                if (ent.explorerBones[b].visible) {
                    char label[4];
                    snprintf(label, sizeof(label), "%d", b);
                    drawList->AddText(
                        ImVec2(ent.explorerBones[b].x + 4.0f, ent.explorerBones[b].y - 4.0f),
                        IM_COL32(255, 255, 0, 255), label
                    );
                    drawList->AddCircleFilled(
                        ImVec2(ent.explorerBones[b].x, ent.explorerBones[b].y),
                        2.0f, IM_COL32(255, 255, 0, 200)
                    );
                }
            }
        }
    }
}

// ============================================================================
// AIMBOT / TRIGGERBOT
// ============================================================================

static bool IsValidAngle(float pitch, float yaw)
{
    return !std::isnan(pitch) && !std::isnan(yaw) && !std::isinf(pitch) && !std::isinf(yaw);
}

// CS2 subtick: real viewangles live in pBaseCmd+0x10, NOT cmd->viewangles.
// CUserCmd+0x40 = CCSGOUserCmd, +0x08 = pBaseCmd, +0x10 = CBaseCmd::viewangles.
// So effective offset from cmd = 0x58.
static bool GetViewAngles(CUserCmd* cmd, float outAngles[3])
{
    if (!cmd) return false;

    // Method 1: inner CBaseCmd (CS2 subtick) at cmd+0x48 -> pBaseCmd -> +0x10
    std::uintptr_t pBaseCmd = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(cmd) + 0x48), 0);
    if (pBaseCmd && IsInValidRange(pBaseCmd)) {
        float* innerAngles = reinterpret_cast<float*>(pBaseCmd + 0x10);
        if (IsValidPtrFast(innerAngles)) {
            outAngles[0] = SafeRead<float>(&innerAngles[0], 0.0f);
            outAngles[1] = SafeRead<float>(&innerAngles[1], 0.0f);
            outAngles[2] = SafeRead<float>(&innerAngles[2], 0.0f);
            if (IsValidAngle(outAngles[0], outAngles[1])) return true;
        }
    }

    // Method 2: dwViewAngles direct read
    HMODULE clientDll = GetModuleHandleA("client.dll");
    if (!clientDll) return false;
    std::uintptr_t clientBase = reinterpret_cast<std::uintptr_t>(clientDll);
    std::uintptr_t viewAnglesAddr = clientBase + cs2_offsets::dwViewAngles;
    if (IsValidPtrFast(reinterpret_cast<void*>(viewAnglesAddr))) {
        float* va = reinterpret_cast<float*>(viewAnglesAddr);
        outAngles[0] = SafeRead<float>(&va[0], 0.0f);
        outAngles[1] = SafeRead<float>(&va[1], 0.0f);
        outAngles[2] = SafeRead<float>(&va[2], 0.0f);
        if (IsValidAngle(outAngles[0], outAngles[1])) return true;
    }

    // Method 3: dwViewAngles indirect (pointer)
    std::uintptr_t vaPtr = SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t*>(viewAnglesAddr), 0);
    if (vaPtr && IsInValidRange(vaPtr)) {
        float* va = reinterpret_cast<float*>(vaPtr);
        outAngles[0] = SafeRead<float>(&va[0], 0.0f);
        outAngles[1] = SafeRead<float>(&va[1], 0.0f);
        outAngles[2] = SafeRead<float>(&va[2], 0.0f);
        if (IsValidAngle(outAngles[0], outAngles[1])) return true;
    }

    return false;
}

// Safe head approximation for CreateMove hook context (NO VirtualQuery / bone reads)
static Vector3 GetHeadPositionFastSafe(std::uintptr_t pawn)
{
    Vector3 origin = GetEntityOriginFast(pawn);
    // Standing player eye level is roughly origin + 64 units Z
    origin.z += 64.0f;
    return origin;
}

static std::uintptr_t FindBestTarget(const float localViewAngles[3], Vector3* outHeadPos, float maxFOV, bool targetTeam, float* outDistance = nullptr)
{
    HMODULE clientDll = GetModuleHandleA("client.dll");
    if (!clientDll) return 0;

    const auto clientBase = reinterpret_cast<std::uintptr_t>(clientDll);

    std::uintptr_t localPawn = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(clientBase + cs2_offsets::dwLocalPlayerPawn), 0);
    if (!localPawn || !IsInValidRange(localPawn)) return 0;

    int localTeam = SafeRead<int>(reinterpret_cast<int*>(localPawn + cs2_offsets::m_iTeamNum), 0);
    Vector3 localOrigin = GetEntityOriginFast(localPawn);
    if (localOrigin.x == 0 && localOrigin.y == 0 && localOrigin.z == 0) return 0;

    Vector3 eyePos = localOrigin;
    eyePos.z += 64.0f;

    std::uintptr_t entityList = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(clientBase + cs2_offsets::dwEntityList), 0);
    if (!entityList || !IsInValidRange(entityList)) return 0;

    std::uintptr_t bestTarget = 0;
    float bestDist = maxFOV;
    Vector3 bestHead = { 0, 0, 0 };

    std::uintptr_t seenPawns[64] = {};
    int seenCount = 0;

    for (int i = 1; i <= 64; i++)
    {
        std::uintptr_t controller = GetEntityFromListFast(entityList, i);
        if (!controller || !IsInValidRange(controller)) continue;

        std::uint32_t raw32 = SafeRead<std::uint32_t>(
            reinterpret_cast<std::uint32_t*>(controller + cs2_offsets::m_hPlayerPawn), 0);
        if (raw32 == 0) {
            raw32 = SafeRead<std::uint32_t>(reinterpret_cast<std::uint32_t*>(controller + 0x60C), 0);
            if (raw32 == 0) {
                raw32 = SafeRead<std::uint32_t>(reinterpret_cast<std::uint32_t*>(controller + 0x7EC), 0);
            }
        }
        if (raw32 == 0) continue;

        uint32_t indexBits = raw32 & 0x7FFF;
        int handleIdx = (int)indexBits;
        if (handleIdx <= 0 || handleIdx >= 0x7FFF) continue;

        std::uintptr_t pawn = GetEntityFromListFast(entityList, handleIdx);
        if (!pawn) continue;

        bool alreadySeen = false;
        for (int s = 0; s < seenCount; s++) {
            if (seenPawns[s] == pawn) { alreadySeen = true; break; }
        }
        if (alreadySeen) continue;
        if (seenCount < 64) seenPawns[seenCount++] = pawn;

        if (pawn == localPawn) continue;
        if (!IsValidPlayerPawnFast(pawn, localPawn)) continue;

        int team = SafeRead<int>(reinterpret_cast<int*>(pawn + cs2_offsets::m_iTeamNum), 0);
        if (!targetTeam && team == localTeam) continue;

        // NO GetBonePosition here — VirtualQuery in CreateMove can deadlock.
        // Use origin + 64z as safe head approximation.
        Vector3 headPos = GetHeadPositionFastSafe(pawn);

        Vector2 targetAngle = CalcAngle(eyePos, headPos);
        float angleDist = AngleDistance(localViewAngles, &targetAngle.x);

        if (angleDist < bestDist) {
            bestDist = angleDist;
            bestTarget = pawn;
            bestHead = headPos;
        }
    }

    if (bestTarget && outHeadPos)
        *outHeadPos = bestHead;

    if (outDistance)
        *outDistance = bestDist;

    return bestTarget;
}

void DoAimbot(CUserCmd* cmd)
{
    g_DbgAimbotTarget.store(0);
    if (!cmd || !g_AimbotEnabled.load()) return;

    if (g_AimbotOnKey.load()) {
        int key = g_AimbotKey.load();
        if (!(GetAsyncKeyState(key) & 0x8000)) return;
    }

    bool targetTeam = g_AimbotTargetTeam.load();
    float fov = g_AimbotFOV.load();
    float smooth = g_AimbotSmoothness.load();

    float curAngles[3];
    if (!GetViewAngles(cmd, curAngles)) return;

    Vector3 targetHead;
    std::uintptr_t target = FindBestTarget(curAngles, &targetHead, fov, targetTeam);
    if (!target) return;

    g_DbgAimbotTarget.store(1);

    HMODULE clientDll = GetModuleHandleA("client.dll");
    if (!clientDll) return;
    const auto clientBase = reinterpret_cast<std::uintptr_t>(clientDll);

    std::uintptr_t localPawn = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(clientBase + cs2_offsets::dwLocalPlayerPawn), 0);
    if (!localPawn) return;

    Vector3 localOrigin = GetEntityOriginFast(localPawn);
    Vector3 eyePos = localOrigin;
    eyePos.z += 64.0f;

    Vector2 targetAngle = CalcAngle(eyePos, targetHead);
    if (!IsValidAngle(targetAngle.x, targetAngle.y)) return;

    float finalPitch, finalYaw;
    if (smooth <= 1.0f) {
        finalPitch = targetAngle.x;
        finalYaw   = targetAngle.y;
    } else {
        float factor = 1.0f / smooth;
        finalPitch = curAngles[0] + (targetAngle.x - curAngles[0]) * factor;
        finalYaw   = curAngles[1] + (targetAngle.y - curAngles[1]) * factor;
    }

    ClampAngles(&finalPitch);
    if (!IsValidAngle(finalPitch, finalYaw)) return;

    // Write to pBaseCmd inner angles (CS2 subtick) — this is what the game processes
    std::uintptr_t pBaseCmd = SafeRead<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(cmd) + 0x48), 0);
    if (pBaseCmd && IsInValidRange(pBaseCmd)) {
        float* innerAngles = reinterpret_cast<float*>(pBaseCmd + 0x10);
        if (IsValidPtrFast(innerAngles)) {
            innerAngles[0] = finalPitch;
            innerAngles[1] = finalYaw;
        }
    }

    // Also update dwViewAngles so the camera follows immediately
    std::uintptr_t viewAnglesAddr = clientBase + cs2_offsets::dwViewAngles;
    if (IsValidPtrFast(reinterpret_cast<void*>(viewAnglesAddr))) {
        float* va = reinterpret_cast<float*>(viewAnglesAddr);
        // Use VirtualProtect only if needed — try direct write first for performance
        DWORD oldProtect;
        if (VirtualProtect(va, 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            va[0] = finalPitch;
            va[1] = finalYaw;
            VirtualProtect(va, 8, oldProtect, &oldProtect);
        } else {
            // Fallback: direct write (may crash if page is RO, but most are RW)
            va[0] = finalPitch;
            va[1] = finalYaw;
        }
    }
}

// Triggerbot (W.I.P)
// void DoTriggerbot(CUserCmd* cmd)
// {
//     g_DbgTriggerFired.store(0);
//     if (!cmd || !g_TriggerbotEnabled.load()) return;

//     if (g_TriggerbotOnKey.load()) {
//         int key = g_TriggerbotKey.load();
//         if (!(GetAsyncKeyState(key) & 0x8000)) return;
//     }

//     bool targetTeam = g_AimbotTargetTeam.load();

//     float curAngles[3];
//     if (!GetViewAngles(cmd, curAngles)) return;

//     HMODULE clientDll = GetModuleHandleA("client.dll");
//     if (!clientDll) return;
//     const auto clientBase = reinterpret_cast<std::uintptr_t>(clientDll);

//     std::uintptr_t localPawn = SafeRead<std::uintptr_t>(
//         reinterpret_cast<std::uintptr_t*>(clientBase + cs2_offsets::dwLocalPlayerPawn), 0);
//     if (!localPawn || !IsInValidRange(localPawn)) return;

//     int localTeam = SafeRead<int>(reinterpret_cast<int*>(localPawn + cs2_offsets::m_iTeamNum), 0);
//     Vector3 localOrigin = GetEntityOriginFast(localPawn);
//     if (localOrigin.x == 0 && localOrigin.y == 0 && localOrigin.z == 0) return;

//     Vector3 eyePos = localOrigin;
//     eyePos.z += 64.0f;

//     std::uintptr_t entityList = SafeRead<std::uintptr_t>(
//         reinterpret_cast<std::uintptr_t*>(clientBase + cs2_offsets::dwEntityList), 0);
//     if (!entityList || !IsInValidRange(entityList)) return;

//     std::uintptr_t bestTarget = 0;
//     float bestDist = 9999.0f;

//     std::uintptr_t seenPawns[64] = {};
//     int seenCount = 0;

//     for (int i = 1; i <= 64; i++)
//     {
//         std::uintptr_t controller = GetEntityFromListFast(entityList, i);
//         if (!controller || !IsInValidRange(controller)) continue;

//         std::uint32_t raw32 = SafeRead<std::uint32_t>(
//             reinterpret_cast<std::uint32_t*>(controller + cs2_offsets::m_hPlayerPawn), 0);
//         if (raw32 == 0) {
//             raw32 = SafeRead<std::uint32_t>(reinterpret_cast<std::uint32_t*>(controller + 0x60C), 0);
//             if (raw32 == 0) {
//                 raw32 = SafeRead<std::uint32_t>(reinterpret_cast<std::uint32_t*>(controller + 0x7EC), 0);
//             }
//         }
//         if (raw32 == 0) continue;

//         uint32_t indexBits = raw32 & 0x7FFF;
//         int handleIdx = (int)indexBits;
//         if (handleIdx <= 0 || handleIdx >= 0x7FFF) continue;

//         std::uintptr_t pawn = GetEntityFromListFast(entityList, handleIdx);
//         if (!pawn) continue;

//         bool alreadySeen = false;
//         for (int s = 0; s < seenCount; s++) {
//             if (seenPawns[s] == pawn) { alreadySeen = true; break; }
//         }
//         if (alreadySeen) continue;
//         if (seenCount < 64) seenPawns[seenCount++] = pawn;

//         if (pawn == localPawn) continue;
//         if (!IsValidPlayerPawnFast(pawn, localPawn)) continue;

//         int team = SafeRead<int>(reinterpret_cast<int*>(pawn + cs2_offsets::m_iTeamNum), 0);
//         if (!targetTeam && team == localTeam) continue;

//         // Check against body center (chest) for a forgiving, crisp trigger
//         Vector3 bodyPos = GetEntityOriginFast(pawn);
//         bodyPos.z += 40.0f;

//         Vector2 targetAngle = CalcAngle(eyePos, bodyPos);
//         float angleDist = AngleDistance(curAngles, &targetAngle.x);

//         if (angleDist < bestDist) {
//             bestDist = angleDist;
//             bestTarget = pawn;
//         }
//     }

//     static std::uintptr_t lastTarget = 0;
//     static DWORD lastAcquireTime = 0;
//     DWORD now = GetTickCount();

//     // Fire if crosshair is on body (within 2.0 degrees of chest center)
//     if (!bestTarget || bestDist > 2.0f) {
//         lastTarget = 0;
//         return;
//     }

//     int delayMs = g_TriggerbotDelay.load();

//     if (bestTarget != lastTarget) {
//         lastTarget = bestTarget;
//         lastAcquireTime = now;
//     }

//     if (now - lastAcquireTime >= (DWORD)delayMs) {
//         g_DbgTriggerFired.store(1);
//         cmd->nButtons.nValue |= IN_ATTACK;
//     }
// }
