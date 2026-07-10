#include "input.h"
#include "globals.h"
#include <cstdint>

bool IsInGameplay()
{
    HMODULE clientDll = GetModuleHandleA("client.dll");
    if (!clientDll) return false;

    std::uintptr_t localPawn = *reinterpret_cast<std::uintptr_t*>(
        reinterpret_cast<std::uintptr_t>(clientDll) + cs2_offsets::dwLocalPlayerPawn);
    if (!localPawn) return false;

    int health = *reinterpret_cast<int*>(localPawn + cs2_offsets::m_iHealth);
    return health > 0;
}

void SetCursorVisible(bool visible)
{
    CURSORINFO ci = { sizeof(ci) };
    GetCursorInfo(&ci);

    if (visible)
    {
        if (ci.flags == 0)
        {
            int count = ShowCursor(TRUE);
            while (count < 0) count = ShowCursor(TRUE);
        }
    }
    else
    {
        if (ci.flags != 0)
        {
            int count = ShowCursor(FALSE);
            while (count >= 0) count = ShowCursor(FALSE);
        }
        SetCursor(nullptr);
    }
}

void ForceGameplayCursorMode()
{
    if (!IsInGameplay())
    {
        SetCursorVisible(true);
        return;
    }

    SetCursorVisible(false);
}

void ForceMenuCursorMode()
{
    SetCursorVisible(true);
}
