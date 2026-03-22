#pragma once
#include <Windows.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <cstdint>

namespace memforge {

class ManualMapper {
public:
    struct MapConfig {
        bool erasePEHeaders       = true;
        bool randomizeSectionNames = true;
        bool callTlsCallbacks     = true;
        bool callDllMain          = true;
    };

    struct MapResult {
        bool success             = false;
        uintptr_t remoteBase     = 0;
        std::string errorMessage;
        std::vector<std::string> log;
    };

    // Map a DLL from a file path into the target process
    MapResult Map(HANDLE hProcess, const std::string& dllPath, const MapConfig& config);

    // Map a DLL from an in-memory buffer into the target process
    MapResult MapFromMemory(HANDLE hProcess, const std::vector<uint8_t>& dllData,
                            const MapConfig& config);

    // Unmap a previously mapped DLL (frees its remote allocation)
    bool Unmap(HANDLE hProcess, uintptr_t remoteBase);

private:
    // Parsing / validation
    bool ParseHeaders(const std::vector<uint8_t>& data);

    // Allocation
    bool AllocateMemory(HANDLE hProcess);

    // Section copy
    bool CopySections(HANDLE hProcess, const std::vector<uint8_t>& data);

    // Base relocation fixup
    bool FixRelocations(HANDLE hProcess, uintptr_t delta);

    // Import resolution
    bool ResolveImports(HANDLE hProcess);

    // TLS callbacks
    bool HandleTlsCallbacks(HANDLE hProcess);

    // DllMain / entry point
    bool CallEntryPoint(HANDLE hProcess, bool attach);

    // Stealth helpers
    void ErasePEHeaders(HANDLE hProcess);
    void RandomizeSectionNames(HANDLE hProcess);

    // ─── Private state ───────────────────────────────────
    IMAGE_DOS_HEADER*   m_dosHeader  = nullptr;
    IMAGE_NT_HEADERS64* m_ntHeaders  = nullptr;
    uintptr_t           m_remoteBase = 0;
    size_t              m_imageSize  = 0;
    std::vector<std::string> m_log;
    MapConfig           m_config;
};

} // namespace memforge
