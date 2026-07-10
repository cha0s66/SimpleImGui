// ============================================================================
// memory.cpp  -  Pattern Scanner Implementation
// ============================================================================
// Pattern scanning is the "magic" behind most modern internal cheats.
// Instead of hardcoding addresses (which change every game update), we search
// for a known sequence of bytes (the "signature" or "pattern") inside a loaded
// module. The signature is expressed as hex bytes with '?' for wildcards.
//
// Example:
//   "48 89 5C 24 ? 48 89 6C 24 ?"
//   →  match any byte at the '?' positions
//
// Process:
//  1. Load the target module (e.g., "client.dll")
//  2. Parse the signature string into an array of byte values (-1 = wildcard)
//  3. Walk every byte of the module's image, compare against pattern
//  4. Return the first match (PatternScan) or all matches (PatternScanAll)
// ============================================================================

#include "memory.h"

// ============================================================================
// PatternScan  —  find the first match, return nullptr if none
// ============================================================================
std::uint8_t* PatternScan(const char* module_name, const char* signature) noexcept
{
    const auto module_handle = GetModuleHandleA(module_name);
    if (!module_handle)
        return nullptr;

    // Lambda: converts a signature string into a vector of ints.
    // '?' or '??' becomes -1 (wildcard). Hex bytes become their integer values.
    static auto pattern_to_byte = [](const char* pattern) {
        auto bytes = std::vector<int>{};
        auto start = const_cast<char*>(pattern);
        auto end   = const_cast<char*>(pattern) + std::strlen(pattern);

        for (auto current = start; current < end; ++current) {
            if (*current == ' ') continue;              // skip spaces
            if (*current == '?') {                      // wildcard
                ++current;
                if (*current == '?')
                    ++current;
                bytes.push_back(-1);
            }
            else {
                // strtoul reads a hex string and advances the pointer
                bytes.push_back(std::strtoul(current, &current, 16));
            }
        }
        return bytes;
    };

    // Parse PE headers to get the module's image size
    auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
    auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(
        reinterpret_cast<std::uint8_t*>(module_handle) + dos_header->e_lfanew);

    auto size_of_image = nt_headers->OptionalHeader.SizeOfImage;
    auto pattern_bytes = pattern_to_byte(signature);
    auto scan_bytes    = reinterpret_cast<std::uint8_t*>(module_handle);

    auto s = pattern_bytes.size();
    auto d = pattern_bytes.data();

    // Brute-force scan: check every byte position in the module image
    for (auto i = 0ul; i < size_of_image - s; ++i) {
        bool found = true;
        for (auto j = 0ul; j < s; ++j) {
            // d[j] == -1 means "wildcard" — any byte matches
            if (scan_bytes[i + j] != d[j] && d[j] != -1) {
                found = false;
                break;
            }
        }
        if (found)
            return &scan_bytes[i];  // found it!
    }
    return nullptr;  // no match
}

// ============================================================================
// PatternScanAll  —  find every match, return them in a vector
// ============================================================================
// Same algorithm as PatternScan but collects ALL matches instead of stopping
// at the first one. This is useful for CreateMove where multiple functions may
// share the same signature and we need to try each one.
// ============================================================================
std::vector<std::uint8_t*> PatternScanAll(const char* module_name, const char* signature)
{
    std::vector<std::uint8_t*> results;
    const auto module_handle = GetModuleHandleA(module_name);
    if (!module_handle)
        return results;

    static auto pattern_to_byte = [](const char* pattern) {
        auto bytes = std::vector<int>{};
        auto start = const_cast<char*>(pattern);
        auto end   = const_cast<char*>(pattern) + std::strlen(pattern);

        for (auto current = start; current < end; ++current) {
            if (*current == ' ') continue;
            if (*current == '?') {
                ++current;
                if (*current == '?')
                    ++current;
                bytes.push_back(-1);
            }
            else {
                bytes.push_back(std::strtoul(current, &current, 16));
            }
        }
        return bytes;
    };

    auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
    auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(
        reinterpret_cast<std::uint8_t*>(module_handle) + dos_header->e_lfanew);

    auto size_of_image = nt_headers->OptionalHeader.SizeOfImage;
    auto pattern_bytes = pattern_to_byte(signature);
    auto scan_bytes    = reinterpret_cast<std::uint8_t*>(module_handle);

    auto s = pattern_bytes.size();
    auto d = pattern_bytes.data();

    for (auto i = 0ul; i < size_of_image - s; ++i) {
        bool found = true;
        for (auto j = 0ul; j < s; ++j) {
            if (scan_bytes[i + j] != d[j] && d[j] != -1) {
                found = false;
                break;
            }
        }
        if (found)
            results.push_back(&scan_bytes[i]);
    }
    return results;
}
