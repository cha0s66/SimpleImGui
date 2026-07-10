// ============================================================================
// input.h  -  Cursor Management
// ============================================================================
// These helpers keep the Windows cursor behavior consistent while the menu is
// open and while gameplay rendering is active.
// ============================================================================

#pragma once

#include <windows.h>

// Check if the local player is alive (health > 0).
// Used to decide whether to hide the cursor or leave it visible.
bool IsInGameplay();

// Set the Windows cursor visibility (ShowCursor loop).
void SetCursorVisible(bool visible);

// Hide the cursor for gameplay mode.
void ForceGameplayCursorMode();

// Show the cursor for menu interaction.
void ForceMenuCursorMode();
