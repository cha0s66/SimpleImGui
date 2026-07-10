// ============================================================================
// memory.h  -  Pattern Scanning & Fast Memory Helpers
// ============================================================================
// Two tools every internal cheat needs:
//  1. PatternScan / PatternScanAll  — find bytes in a loaded module by signature
//  2. IsValidPtrFast / FastRead / SafeRead  — safe pointer dereference helpers
//
// IMPORTANT: IsValidPtrFast only checks if the pointer is within a reasonable
// user-mode address range. SafeRead adds __try/__except (SEH) to actually catch
// access violations when the pointer is in-range but unmapped/freed.
// ============================================================================

#pragma once

#include <windows.h>
#include <cstdint>
#include <vector>
#include <cstring>

// ============================================================================
// Pattern Scanning
// ============================================================================
// `signature` format: space-separated hex bytes, '?' or '??' for wildcards.
// Example: "48 89 5C 24 ? 48 89 6C 24 ?" matches any value at the ? positions.
// ============================================================================
std::uint8_t* PatternScan(const char* module_name, const char* signature) noexcept;
std::vector<std::uint8_t*> PatternScanAll(const char* module_name, const char* signature);

// ============================================================================
// Fast Pointer Validation (no VirtualQuery — safe for hot paths)
// ============================================================================
// 0x10000          = lowest valid user-mode address
// 0x7FFFFFFEFFFF   = highest valid user-mode address on x64 Windows
// ============================================================================
inline bool IsInValidRange(uintptr_t ptr) {
    return ptr >= 0x10000 && ptr <= 0x7FFFFFFEFFFF;
}

inline bool IsValidPtrFast(void* ptr) {
    if (!ptr) return false;
    return IsInValidRange(reinterpret_cast<uintptr_t>(ptr));
}

// ============================================================================
// FastRead  —  SEH-protected dereference with a default fallback
// ============================================================================
// Uses __try/__except to catch access violations from stale/freed pointers.
// This is the standard read function for ALL game-memory access.
// ============================================================================
template<typename T>
inline T FastRead(T* ptr, T defaultVal = T{}) {
#if defined(_MSC_VER)
    __try {
        if (!IsValidPtrFast(ptr)) return defaultVal;
        return *ptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return defaultVal;
    }
#else
    if (!IsValidPtrFast(ptr)) return defaultVal;
    return *ptr;
#endif
}

// ============================================================================
// SafeRead  —  alias for FastRead (same SEH protection)
// ============================================================================
template<typename T>
inline T SafeRead(T* ptr, T defaultVal = T{}) {
    return FastRead<T>(ptr, defaultVal);
}
