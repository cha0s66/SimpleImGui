// ============================================================================
// features.h  —  BunnyHop & ESP Interface
// ============================================================================
// Clean public interface. Implementation details live in features.cpp.
// ============================================================================

#pragma once
#include <windows.h>
#include "sdk.h"

// ============================================================================
// BunnyHop
// ============================================================================
// Core BunnyHop logic. Called inside hkCreateMoveWrapper.
// If the player is pressing Space and is on the ground, forces IN_JUMP.
// If the player is in the air, clears IN_JUMP.
// ============================================================================
void DoBhop(CUserCmd* cmd);

// ============================================================================
// ESP Math Helpers
// ============================================================================
bool WorldToScreen(const Vector3& worldPos, Vector2& screenPos, const ViewMatrix& vm, float width, float height);
Vector3 GetEntityOriginFast(std::uintptr_t entity);
void    GetPlayerNameFast(std::uintptr_t controller, char* outName, size_t outSize);
std::uintptr_t GetEntityFromListFast(std::uintptr_t entityList, int index);
bool           IsValidPlayerPawnFast(std::uintptr_t pawn, std::uintptr_t localPawn);

// ============================================================================
// Bone / Skeleton Helpers
// ============================================================================
// Read a bone world position from an entity's bone matrix.
// NOTE: Bone matrix offsets may need adjustment per CS2 build.
Vector3 GetBonePosition(std::uintptr_t entity, int boneIndex);

// ============================================================================
// ESP Pipeline (Two-Stage for Performance)
// ============================================================================
// Stage 1: Read all visible player data from game memory and pre-compute
// screen positions, colors, and text positions. Store in the write buffer.
// Runs in a background thread — does all heavy work here.
void UpdateEspData(float screenW, float screenH);

// Stage 2: Draw everything from the pre-computed read buffer.
// Runs in the Present hook — zero heavy work, just ImGui draw calls.
void RenderEspFast(struct ImDrawList* drawList);

// ============================================================================
// Aimbot
// ============================================================================
// Called inside hkCreateMoveWrapper. Smoothly adjusts viewangles toward
// the closest enemy head within the configured FOV.
void DoAimbot(CUserCmd* cmd);

// ============================================================================
// Triggerbot
// ============================================================================
// Called inside hkCreateMoveWrapper. Fires when crosshair is on an enemy
// within the configured trigger FOV.
void DoTriggerbot(CUserCmd* cmd);
