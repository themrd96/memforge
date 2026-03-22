#include "core/manual_mapper.h"
#include <fstream>
#include <random>
#include <cstring>

#ifndef IMAGE_REL_BASED_DIR64
#define IMAGE_REL_BASED_DIR64 10
#endif

namespace memforge {

static std::string HexStr(uintptr_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
    return buf;
}

ManualMapper::MapResult ManualMapper::Map(HANDLE hProcess,
                                          const std::string& dllPath,
                                          const MapConfig& config) {
    std::ifstream file(dllPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        MapResult r; r.success = false;
        r.errorMessage = "Failed to open file: " + dllPath;
        r.log.push_back("[ERR] " + r.errorMessage);
        return r;
    }
    std::streamsize sz = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    if (!file.read(reinterpret_cast<char*>(data.data()), sz)) {
        MapResult r; r.success = false;
        r.errorMessage = "Failed to read file: " + dllPath;
        r.log.push_back("[ERR] " + r.errorMessage);
        return r;
    }
    file.close();
    return MapFromMemory(hProcess, data, config);
}

ManualMapper::MapResult ManualMapper::MapFromMemory(HANDLE hProcess,
                                                     const std::vector<uint8_t>& dllData,
                                                     const MapConfig& config) {
    m_dosHeader  = nullptr;
    m_ntHeaders  = nullptr;
    m_remoteBase = 0;
    m_imageSize  = 0;
    m_log.clear();
    m_config = config;

    auto Fail = [&](const std::string& msg) -> MapResult {
        m_log.push_back("[ERR] " + msg);
        MapResult r;
        r.success      = false;
        r.remoteBase   = 0;
        r.errorMessage = msg;
        r.log          = m_log;
        return r;
    };

    m_log.push_back("[*] Parsing PE headers...");
    if (!ParseHeaders(dllData))
        return Fail("PE header validation failed");

    m_log.push_back("[*] Allocating remote memory (" + HexStr(m_imageSize) + " bytes)...");
    if (!AllocateMemory(hProcess))
        return Fail("VirtualAllocEx failed");
    m_log.push_back("[+] Allocated at " + HexStr(m_remoteBase));

    m_log.push_back("[*] Copying sections...");
    if (!CopySections(hProcess, dllData))
        return Fail("Section copy failed");
    m_log.push_back("[+] Sections copied");

    uintptr_t delta = m_remoteBase -
                      static_cast<uintptr_t>(m_ntHeaders->OptionalHeader.ImageBase);
    if (delta != 0) {
        m_log.push_back("[*] Fixing relocations (delta=" + HexStr(delta) + ")...");
        if (!FixRelocations(hProcess, delta))
            return Fail("Relocation fixup failed");
        m_log.push_back("[+] Relocations fixed");
    } else {
        m_log.push_back("[*] No relocation needed (loaded at preferred base)");
    }

    m_log.push_back("[*] Resolving imports...");
    if (!ResolveImports(hProcess))
        return Fail("Import resolution failed");
    m_log.push_back("[+] Imports resolved");

    if (config.callTlsCallbacks) {
        m_log.push_back("[*] Executing TLS callbacks...");
        if (!HandleTlsCallbacks(hProcess))
            m_log.push_back("[W] TLS callback execution had errors (continuing)");
        else
            m_log.push_back("[+] TLS callbacks done");
    }

    if (config.erasePEHeaders) {
        m_log.push_back("[*] Erasing PE headers...");
        ErasePEHeaders(hProcess);
        m_log.push_back("[+] PE headers erased");
    }

    if (config.randomizeSectionNames) {
        m_log.push_back("[*] Randomizing section names...");
        RandomizeSectionNames(hProcess);
        m_log.push_back("[+] Section names randomized");
    }

    if (config.callDllMain) {
        m_log.push_back("[*] Calling DLL entry point (DLL_PROCESS_ATTACH)...");
        if (!CallEntryPoint(hProcess, true))
            m_log.push_back("[W] Entry point call had errors (continuing)");
        else
            m_log.push_back("[+] Entry point returned");
    }

    m_log.push_back("[+] Manual map complete. Base=" + HexStr(m_remoteBase));
    MapResult r;
    r.success    = true;
    r.remoteBase = m_remoteBase;
    r.log        = m_log;
    return r;
}

bool ManualMapper::Unmap(HANDLE hProcess, uintptr_t remoteBase) {
    if (!remoteBase) return false;
    return VirtualFreeEx(hProcess, reinterpret_cast<LPVOID>(remoteBase),
                         0, MEM_RELEASE) != FALSE;
}

bool ManualMapper::ParseHeaders(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(IMAGE_DOS_HEADER)) {
        m_log.push_back("[ERR] File too small for DOS header");
        return false;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(const_cast<uint8_t*>(data.data()));
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        m_log.push_back("[ERR] Invalid DOS signature");
        return false;
    }
    if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > data.size()) {
        m_log.push_back("[ERR] NT headers beyond file bounds");
        return false;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        const_cast<uint8_t*>(data.data()) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        m_log.push_back("[ERR] Invalid PE signature");
        return false;
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        m_log.push_back("[ERR] Only 64-bit (AMD64) DLLs are supported");
        return false;
    }
    m_dosHeader = dos;
    m_ntHeaders = nt;
    m_imageSize = static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
    m_log.push_back("[+] DOS+PE headers valid, image size=" + HexStr(m_imageSize) +
                    ", preferred base=" + HexStr(nt->OptionalHeader.ImageBase));
    return true;
}

bool ManualMapper::AllocateMemory(HANDLE hProcess) {
    LPVOID base = VirtualAllocEx(
        hProcess,
        reinterpret_cast<LPVOID>(m_ntHeaders->OptionalHeader.ImageBase),
        m_imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!base) {
        m_log.push_back("[*] Preferred base unavailable, letting Windows choose...");
        base = VirtualAllocEx(hProcess, nullptr, m_imageSize,
                              MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    }
    if (!base) return false;
    m_remoteBase = reinterpret_cast<uintptr_t>(base);
    return true;
}

bool ManualMapper::CopySections(HANDLE hProcess, const std::vector<uint8_t>& data) {
    WORD numSections = m_ntHeaders->FileHeader.NumberOfSections;
    auto* section = IMAGE_FIRST_SECTION(m_ntHeaders);
    for (WORD i = 0; i < numSections; ++i, ++section) {
        if (section->SizeOfRawData == 0) {
            m_log.push_back("[*] Skipping empty section " +
                std::string(reinterpret_cast<const char*>(section->Name), 8));
            continue;
        }
        if (static_cast<size_t>(section->PointerToRawData) + section->SizeOfRawData > data.size()) {
            m_log.push_back("[ERR] Section " +
                std::string(reinterpret_cast<const char*>(section->Name), 8) +
                " exceeds file bounds");
            return false;
        }
        LPVOID dest = reinterpret_cast<LPVOID>(m_remoteBase + section->VirtualAddress);
        const void* src = data.data() + section->PointerToRawData;
        SIZE_T written = 0;
        if (!WriteProcessMemory(hProcess, dest, src, section->SizeOfRawData, &written)) {
            m_log.push_back("[ERR] WriteProcessMemory failed for section at " +
                HexStr(section->VirtualAddress));
            return false;
        }
        DWORD protect = PAGE_EXECUTE_READWRITE;
        DWORD chars = section->Characteristics;
        bool exec  = (chars & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool read  = (chars & IMAGE_SCN_MEM_READ)    != 0;
        bool write = (chars & IMAGE_SCN_MEM_WRITE)   != 0;
        if (exec && read && !write)       protect = PAGE_EXECUTE_READ;
        else if (!exec && read && write)  protect = PAGE_READWRITE;
        else if (!exec && read && !write) protect = PAGE_READONLY;
        DWORD old = 0;
        VirtualProtectEx(hProcess, dest, section->SizeOfRawData, protect, &old);
        m_log.push_back("[+] Section " +
            std::string(reinterpret_cast<const char*>(section->Name), 8) +
            " copied (" + std::to_string(section->SizeOfRawData) + " bytes)");
    }
    return true;
}

bool ManualMapper::FixRelocations(HANDLE hProcess, uintptr_t delta) {
    auto& relocDir =
        m_ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (relocDir.VirtualAddress == 0 || relocDir.Size == 0) {
        m_log.push_back("[*] No relocation directory present");
        return true;
    }
    uintptr_t cursor = m_remoteBase + relocDir.VirtualAddress;
    DWORD remaining  = relocDir.Size;
    while (remaining > 0) {
        IMAGE_BASE_RELOCATION block{};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(cursor),
                               &block, sizeof(block), &bytesRead) ||
            block.SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) break;
        DWORD entryCount =
            (block.SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        uintptr_t entriesAddr = cursor + sizeof(IMAGE_BASE_RELOCATION);
        for (DWORD e = 0; e < entryCount; ++e) {
            WORD entry = 0;
            SIZE_T r2  = 0;
            if (!ReadProcessMemory(hProcess,
                    reinterpret_cast<LPCVOID>(entriesAddr + e * sizeof(WORD)),
                    &entry, sizeof(entry), &r2)) continue;
            int type   = entry >> 12;
            int offset = entry & 0x0FFF;
            if (type == IMAGE_REL_BASED_DIR64) {
                uintptr_t patchAddr =
                    m_remoteBase + block.VirtualAddress + static_cast<DWORD>(offset);
                ULONGLONG value = 0;
                SIZE_T r3 = 0;
                if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(patchAddr),
                                       &value, sizeof(value), &r3)) continue;
                value += static_cast<ULONGLONG>(delta);
                SIZE_T written = 0;
                WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(patchAddr),
                                   &value, sizeof(value), &written);
            }
        }
        remaining -= block.SizeOfBlock;
        cursor    += block.SizeOfBlock;
    }
    return true;
}

bool ManualMapper::ResolveImports(HANDLE hProcess) {
    auto& importDir =
        m_ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress == 0 || importDir.Size == 0) {
        m_log.push_back("[*] No import directory - nothing to resolve");
        return true;
    }
    uintptr_t descAddr = m_remoteBase + importDir.VirtualAddress;
    for (;;) {
        IMAGE_IMPORT_DESCRIPTOR desc{};
        SIZE_T r = 0;
        if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(descAddr),
                               &desc, sizeof(desc), &r)) {
            m_log.push_back("[ERR] Failed to read import descriptor");
            return false;
        }
        if (desc.Name == 0 && desc.FirstThunk == 0) break;
        char dllName[256] = {};
        SIZE_T r2 = 0;
        ReadProcessMemory(hProcess,
            reinterpret_cast<LPCVOID>(m_remoteBase + desc.Name),
            dllName, sizeof(dllName) - 1, &r2);
        HMODULE hMod = GetModuleHandleA(dllName);
        if (!hMod) hMod = LoadLibraryA(dllName);
        if (!hMod) {
            m_log.push_back("[ERR] Failed to load import DLL: " + std::string(dllName));
            return false;
        }
        m_log.push_back("[*] Resolving imports from " + std::string(dllName));
        uintptr_t thunkAddr = m_remoteBase +
            (desc.OriginalFirstThunk ? desc.OriginalFirstThunk : desc.FirstThunk);
        uintptr_t iatAddr   = m_remoteBase + desc.FirstThunk;
        for (;;) {
            IMAGE_THUNK_DATA64 thunk{};
            SIZE_T r3 = 0;
            if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(thunkAddr),
                                   &thunk, sizeof(thunk), &r3)) break;
            if (thunk.u1.AddressOfData == 0) break;
            FARPROC fn = nullptr;
            if (IMAGE_SNAP_BY_ORDINAL64(thunk.u1.Ordinal)) {
                WORD ordinal = IMAGE_ORDINAL64(thunk.u1.Ordinal);
                fn = GetProcAddress(hMod, MAKEINTRESOURCEA(ordinal));
                if (!fn) {
                    m_log.push_back("[ERR] Failed to resolve ordinal " +
                        std::to_string(ordinal) + " in " + std::string(dllName));
                    return false;
                }
            } else {
                uintptr_t ibnAddr = m_remoteBase +
                    static_cast<uintptr_t>(thunk.u1.AddressOfData);
                char funcName[256] = {};
                SIZE_T r5 = 0;
                ReadProcessMemory(hProcess,
                    reinterpret_cast<LPCVOID>(ibnAddr + offsetof(IMAGE_IMPORT_BY_NAME, Name)),
                    funcName, sizeof(funcName) - 1, &r5);
                fn = GetProcAddress(hMod, funcName);
                if (!fn) {
                    m_log.push_back("[ERR] Failed to resolve " + std::string(funcName) +
                        " from " + std::string(dllName));
                    return false;
                }
            }
            ULONGLONG fnAddr = reinterpret_cast<ULONGLONG>(fn);
            SIZE_T written = 0;
            WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(iatAddr),
                               &fnAddr, sizeof(fnAddr), &written);
            thunkAddr += sizeof(IMAGE_THUNK_DATA64);
            iatAddr   += sizeof(IMAGE_THUNK_DATA64);
        }
        descAddr += sizeof(IMAGE_IMPORT_DESCRIPTOR);
    }
    return true;
}

bool ManualMapper::HandleTlsCallbacks(HANDLE hProcess) {
    auto& tlsDir =
        m_ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tlsDir.VirtualAddress == 0) {
        m_log.push_back("[*] No TLS directory");
        return true;
    }
    IMAGE_TLS_DIRECTORY64 tls{};
    SIZE_T r = 0;
    if (!ReadProcessMemory(hProcess,
            reinterpret_cast<LPCVOID>(m_remoteBase + tlsDir.VirtualAddress),
            &tls, sizeof(tls), &r)) {
        m_log.push_back("[ERR] Failed to read TLS directory");
        return false;
    }
    if (tls.AddressOfCallBacks == 0) {
        m_log.push_back("[*] TLS directory present but no callbacks");
        return true;
    }
    uintptr_t cbTableAddr = static_cast<uintptr_t>(tls.AddressOfCallBacks);
    int cbIdx = 0;
    for (;;) {
        ULONGLONG cbAddr = 0;
        SIZE_T r2 = 0;
        if (!ReadProcessMemory(hProcess,
                reinterpret_cast<LPCVOID>(cbTableAddr + cbIdx * sizeof(ULONGLONG)),
                &cbAddr, sizeof(cbAddr), &r2)) break;
        if (cbAddr == 0) break;
        m_log.push_back("[*] Calling TLS callback " + std::to_string(cbIdx) +
                        " at " + HexStr(static_cast<uintptr_t>(cbAddr)));
        HANDLE hThread = CreateRemoteThread(
            hProcess, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(cbAddr),
            reinterpret_cast<LPVOID>(m_remoteBase), 0, nullptr);
        if (hThread) {
            WaitForSingleObject(hThread, 5000);
            CloseHandle(hThread);
            m_log.push_back("[+] TLS callback " + std::to_string(cbIdx) + " returned");
        } else {
            m_log.push_back("[W] CreateRemoteThread for TLS callback " +
                std::to_string(cbIdx) + " failed (error=" +
                std::to_string(GetLastError()) + ")");
        }
        ++cbIdx;
    }
    return true;
}

bool ManualMapper::CallEntryPoint(HANDLE hProcess, bool) {
    if (m_ntHeaders->OptionalHeader.AddressOfEntryPoint == 0) {
        m_log.push_back("[*] No entry point defined");
        return true;
    }
    uintptr_t ep = m_remoteBase + m_ntHeaders->OptionalHeader.AddressOfEntryPoint;
    m_log.push_back("[*] Entry point at " + HexStr(ep));
    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(ep),
        reinterpret_cast<LPVOID>(m_remoteBase), 0, nullptr);
    if (!hThread) {
        m_log.push_back("[ERR] CreateRemoteThread for entry point failed (error=" +
            std::to_string(GetLastError()) + ")");
        return false;
    }
    DWORD waitResult = WaitForSingleObject(hThread, 5000);
    if (waitResult == WAIT_TIMEOUT)
        m_log.push_back("[W] Entry point timed out (5 s) - DLL may still be initializing");
    CloseHandle(hThread);
    return true;
}

void ManualMapper::ErasePEHeaders(HANDLE hProcess) {
    DWORD headerSize = m_ntHeaders->OptionalHeader.SizeOfHeaders;
    std::vector<uint8_t> zeros(headerSize, 0);
    SIZE_T written = 0;
    WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(m_remoteBase),
                       zeros.data(), headerSize, &written);
}

void ManualMapper::RandomizeSectionNames(HANDLE hProcess) {
    static const char kAlpha[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    WORD numSections = m_ntHeaders->FileHeader.NumberOfSections;
    uintptr_t sectionHeadersBase =
        m_remoteBase +
        static_cast<uintptr_t>(m_dosHeader->e_lfanew) +
        sizeof(IMAGE_NT_HEADERS64);
    srand(static_cast<unsigned int>(GetTickCount64()));
    for (WORD i = 0; i < numSections; ++i) {
        char name[IMAGE_SIZEOF_SHORT_NAME] = {};
        for (int c = 0; c < IMAGE_SIZEOF_SHORT_NAME; ++c)
            name[c] = kAlpha[rand() % (sizeof(kAlpha) - 1)];
        uintptr_t nameAddr =
            sectionHeadersBase + i * sizeof(IMAGE_SECTION_HEADER) +
            offsetof(IMAGE_SECTION_HEADER, Name);
        SIZE_T written = 0;
        WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(nameAddr),
                           name, IMAGE_SIZEOF_SHORT_NAME, &written);
    }
}

} // namespace memforge
