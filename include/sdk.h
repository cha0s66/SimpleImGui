// ============================================================================
// sdk.h  —  Core Data Structures & Constants
// ============================================================================
// Pure data structures and constants. No Windows headers, no function 
// declarations — just types and values every module needs to agree on.
// ============================================================================

#pragma once
#include <cstdint>
#include <cmath>

// ============================================================================
// CInButtonState  —  CS2 Button State (32 bytes, nested inside CUserCmd)
// ============================================================================
// Layout confirmed by newbhop.cpp SDK:
//   nValue        = current button bitmask (IN_JUMP, etc.)
//   nValueChanged = buttons that changed this tick (engine-managed)
//   nValueScroll  = scroll wheel state
// ============================================================================
struct CInButtonState {
    uint32_t nValue;           // 0x00 current button state
    uint32_t nValueChanged;    // 0x04 buttons changed this tick
    uint32_t nValueScroll;     // 0x08 scroll wheel state
    char     pad[0x14];        // 0x0C–0x20 remaining fields
};

// ============================================================================
// CCSGOUserCmd  —  Nested struct at CUserCmd + 0x40
// ============================================================================
// Holds a pointer to CBaseCmd (offset 0x08) used by the engine for input processing.
// ============================================================================
struct CCSGOUserCmd {
    void* vtable;            // 0x00
    void* pBaseCmd;          // 0x08  CBaseCmd*
    void* pPredictedBaseCmd; // 0x10  CBaseCmd*
    int   nSplitScreenPlayer; // 0x18
    char  pad[0x04];         // 0x1C–0x20
};

// ============================================================================
// CUserCmd  —  CS2 Input Command Structure (136 bytes total)
// ============================================================================
// Passed to CreateMove every tick. We read/modify it before the game processes.
//
// Layout: 0x00 vtable, 0x08 cmd# / tick#, 0x18 viewangles, 0x40 csgoUserCmd,
//         0x60 nButtons (CInButtonState), 0x80 weaponselect.
// ============================================================================
#pragma pack(push, 1)
struct CUserCmd {
    void*           vtable;             // 0x00
    int             command_number;     // 0x08
    int             tick_count;         // 0x0C
    float           command_time;       // 0x10
    float           frame_time;         // 0x14
    float           viewangles[3];      // 0x18
    char            pad_0x24[0x1C];     // 0x24–0x40 (28 bytes)
    CCSGOUserCmd    csgoUserCmd;        // 0x40–0x60 (32 bytes)
    CInButtonState  nButtons;           // 0x60–0x80 (32 bytes)
    uint32_t        weaponselect;       // 0x80
    char            pad_0x84[0x4];      // 0x84
};
#pragma pack(pop)

// ============================================================================
// Math Types
// ============================================================================
struct Vector3 {
    float x, y, z;
    Vector3 operator-(const Vector3& other) const {
        return { x - other.x, y - other.y, z - other.z };
    }
    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }
};

struct Vector2 {
    float x, y;
};

struct ViewMatrix {
    float m[4][4];
};

// ============================================================================
// Matrix3x4  —  Bone transform matrix (48 bytes)
// ============================================================================
// Used to read bone positions from the game's bone matrix.
// Position is stored in the last column: (m[0][3], m[1][3], m[2][3]).
// ============================================================================
struct Matrix3x4 {
    float m[3][4];
    Vector3 Origin() const {
        return { m[0][3], m[1][3], m[2][3] };
    }
};

// ============================================================================
// Angle Utilities  —  for Aimbot calculations
// ============================================================================
inline void NormalizeAngles(float* angles) {
    for (int i = 0; i < 2; i++) {
        while (angles[i] > 180.0f)  angles[i] -= 360.0f;
        while (angles[i] < -180.0f) angles[i] += 360.0f;
    }
}

inline void ClampAngles(float* angles) {
    if (angles[0] > 89.0f)  angles[0] = 89.0f;
    if (angles[0] < -89.0f) angles[0] = -89.0f;
    NormalizeAngles(angles);
}

// Safe overload: avoids undefined behaviour when caller passes a single float
inline void ClampAngles(float& pitch, float& yaw) {
    float angles[2] = { pitch, yaw };
    ClampAngles(angles);
    pitch = angles[0];
    yaw   = angles[1];
}

inline Vector2 CalcAngle(const Vector3& src, const Vector3& dst) {
    Vector3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };
    float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    Vector2 angles;
    if (dist < 0.001f) {
        // Directly above/below (or same position) — avoid atan2(0,0) undefined behaviour
        if (delta.z > 0.0f) {
            angles.x = -90.0f;
        } else if (delta.z < 0.0f) {
            angles.x = 90.0f;
        } else {
            angles.x = 0.0f;
        }
        angles.y = 0.0f;
    } else {
        angles.x = -std::atan2(delta.z, dist) * (180.0f / 3.14159265f);
        angles.y = std::atan2(delta.y, delta.x) * (180.0f / 3.14159265f);
    }
    return angles;
}

inline float AngleDistance(const float* a, const float* b) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    while (dy > 180.0f)  dy -= 360.0f;
    while (dy < -180.0f) dy += 360.0f;
    return std::sqrt(dx * dx + dy * dy);
}

// ============================================================================
// Input Button Constants
// ============================================================================
constexpr uint32_t IN_ATTACK    = (1 << 0);
constexpr uint32_t IN_JUMP      = (1 << 1);   // Space bar
constexpr uint32_t IN_DUCK      = (1 << 2);
constexpr uint32_t IN_FORWARD   = (1 << 3);
constexpr uint32_t IN_BACK      = (1 << 4);
constexpr uint32_t IN_USE       = (1 << 5);
constexpr uint32_t IN_CANCEL    = (1 << 6);
constexpr uint32_t IN_LEFT      = (1 << 7);
constexpr uint32_t IN_RIGHT     = (1 << 8);
constexpr uint32_t IN_MOVELEFT  = (1 << 9);
constexpr uint32_t IN_MOVERIGHT = (1 << 10);
constexpr uint32_t IN_ATTACK2   = (1 << 11);
constexpr uint32_t IN_RUN       = (1 << 12);
constexpr uint32_t IN_RELOAD    = (1 << 13);
constexpr uint32_t IN_ALT1      = (1 << 14);
constexpr uint32_t IN_SCORE     = (1 << 15);
constexpr uint32_t IN_WALK      = (1 << 17);
constexpr uint32_t IN_ZOOM      = (1 << 19);