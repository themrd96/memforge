#include "core/anti_anticheat.h"
#include <intrin.h>
#include <fstream>
#include <cstring>
#include <sstream>

namespace memforge {

AacStatus AntiAntiCheat::s_status;

// ─── Syscall Number Discovery ────────────────────────────

DWORD AntiAntiCheat::GetSyscallNumber(const char* functionName) {
    // Read ntdll.dll from disk to find the syscall number
    char systemDir[MAX_PATH] = {};
    GetSystemDirectoryA(systemDir, MAX_PATH);
    std::string ntdllPath = std::string(systemDir) + "\\ntdll.dll";

    HANDLE hFile = CreateFileA(ntdllPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    DWORD fileSize = GetFileSize(hFile, nullptr);
    std::vector<uint8_t> fileData(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, fileData.data(), fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (bytesRead < sizeof(IMAGE_DOS_HEADER)) return 0;

    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(fileData.data());
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;

    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(fileData.data() + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return 0;

    auto& exportDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDir.VirtualAddress == 0) return 0;

    // Convert RVA to file offset
    auto rvaToOffset = [&](DWORD rva) -> DWORD {
        auto* sections = IMAGE_FIRST_SECTION(ntHeaders);
        for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
            if (rva >= sections[i].VirtualAddress &&
                rva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize) {
                return rva - sections[i].VirtualAddress + sections[i].PointerToRawData;
            }
        }
        return 0;
    };

    DWORD exportOffset = rvaToOffset(exportDir.VirtualAddress);
    if (exportOffset == 0) return 0;

    auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(fileData.data() + exportOffset);

    DWORD namesOffset = rvaToOffset(exports->AddressOfNames);
    DWORD ordinalsOffset = rvaToOffset(exports->AddressOfNameOrdinals);
    DWORD functionsOffset = rvaToOffset(exports->AddressOfFunctions);

    if (namesOffset == 0 || ordinalsOffset == 0 || functionsOffset == 0) return 0;

    auto* names = reinterpret_cast<DWORD*>(fileData.data() + namesOffset);
    auto* ordinals = reinterpret_cast<WORD*>(fileData.data() + ordinalsOffset);
    auto* functions = reinterpret_cast<DWORD*>(fileData.data() + functionsOffset);

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        DWORD nameOffset = rvaToOffset(names[i]);
        if (nameOffset == 0) continue;

        const char* name = reinterpret_cast<const char*>(fileData.data() + nameOffset);
        if (strcmp(name, functionName) == 0) {
            DWORD funcRva = functions[ordinals[i]];
            DWORD funcOffset = rvaToOffset(funcRva);
            if (funcOffset == 0) continue;

            // x64 syscall stubs start with: mov r10, rcx; mov eax, <syscall_num>
            // Pattern: 4C 8B D1 B8 XX XX XX XX
            const uint8_t* code = fileData.data() + funcOffset;
            if (code[0] == 0x4C && code[1] == 0x8B && code[2] == 0xD1 && code[3] == 0xB8) {
                DWORD syscallNum;
                std::memcpy(&syscallNum, code + 4, 4);
                return syscallNum;
            }
        }
    }

    return 0;
}

// ─── Manual Map DLL ──────────────────────────────────────

bool AntiAntiCheat::ManualMapDll(HANDLE hProcess, const std::string& dllPath) {
    s_status.lastError.clear();

    // Read the DLL file
    HANDLE hFile = CreateFileA(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        s_status.lastError = "Cannot open DLL file: " + dllPath;
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    std::vector<uint8_t> fileData(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, fileData.data(), fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (bytesRead < sizeof(IMAGE_DOS_HEADER)) {
        s_status.lastError = "DLL file too small";
        return false;
    }

    // Parse PE headers
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(fileData.data());
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        s_status.lastError = "Invalid DOS signature";
        return false;
    }

    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(fileData.data() + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        s_status.lastError = "Invalid NT signature";
        return false;
    }

    // Allocate memory in target process
    SIZE_T imageSize = ntHeaders->OptionalHeader.SizeOfImage;
    void* remoteBase = VirtualAllocEx(hProcess, nullptr, imageSize,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteBase) {
        s_status.lastError = "VirtualAllocEx failed";
        return false;
    }

    // Copy PE headers
    SIZE_T written = 0;
    WriteProcessMemory(hProcess, remoteBase, fileData.data(),
                       ntHeaders->OptionalHeader.SizeOfHeaders, &written);

    // Copy sections
    auto* sections = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
        if (sections[i].SizeOfRawData == 0) continue;

        void* sectionDest = static_cast<uint8_t*>(remoteBase) + sections[i].VirtualAddress;
        WriteProcessMemory(hProcess, sectionDest,
                          fileData.data() + sections[i].PointerToRawData,
                          sections[i].SizeOfRawData, &written);
    }

    // Resolve imports by walking the IAT
    auto& importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress != 0) {
        // We need to read the import table from the remote process since we wrote it there
        std::vector<uint8_t> remoteCopy(imageSize);
        SIZE_T remoteRead = 0;
        ReadProcessMemory(hProcess, remoteBase, remoteCopy.data(), imageSize, &remoteRead);

        auto* importDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            remoteCopy.data() + importDir.VirtualAddress);

        while (importDesc->Name != 0) {
            const char* moduleName = reinterpret_cast<const char*>(
                remoteCopy.data() + importDesc->Name);

            // Load the required DLL in target using LoadLibraryA stub
            // We need to get the module handle in the target process
            HMODULE hMod = GetModuleHandleA(moduleName);
            if (!hMod) {
                // The module must already be loaded in the target for this to work
                // This is a limitation of not using LoadLibrary
                importDesc++;
                continue;
            }

            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                remoteCopy.data() + importDesc->FirstThunk);
            auto* origThunk = importDesc->OriginalFirstThunk ?
                reinterpret_cast<IMAGE_THUNK_DATA*>(
                    remoteCopy.data() + importDesc->OriginalFirstThunk) : thunk;

            size_t idx = 0;
            while (origThunk->u1.AddressOfData != 0) {
                FARPROC funcAddr = nullptr;

                if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                    funcAddr = GetProcAddress(hMod,
                        MAKEINTRESOURCEA(IMAGE_ORDINAL(origThunk->u1.Ordinal)));
                } else {
                    auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        remoteCopy.data() + origThunk->u1.AddressOfData);
                    funcAddr = GetProcAddress(hMod, importByName->Name);
                }

                if (funcAddr) {
                    uintptr_t funcAddrVal = reinterpret_cast<uintptr_t>(funcAddr);
                    void* thunkDest = static_cast<uint8_t*>(remoteBase) +
                                      importDesc->FirstThunk + idx * sizeof(IMAGE_THUNK_DATA);
                    WriteProcessMemory(hProcess, thunkDest, &funcAddrVal, sizeof(funcAddrVal), &written);
                }

                origThunk++;
                idx++;
            }

            importDesc++;
        }
    }

    // Apply relocations
    auto& relocDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (relocDir.VirtualAddress != 0) {
        ptrdiff_t delta = reinterpret_cast<uintptr_t>(remoteBase) -
                          ntHeaders->OptionalHeader.ImageBase;

        if (delta != 0) {
            std::vector<uint8_t> remoteCopy(imageSize);
            SIZE_T remoteRead = 0;
            ReadProcessMemory(hProcess, remoteBase, remoteCopy.data(), imageSize, &remoteRead);

            auto* relocBlock = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                remoteCopy.data() + relocDir.VirtualAddress);
            DWORD relocSize = relocDir.Size;
            DWORD processed = 0;

            while (processed < relocSize && relocBlock->VirtualAddress != 0) {
                DWORD numEntries = (relocBlock->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                auto* entries = reinterpret_cast<WORD*>(
                    reinterpret_cast<uint8_t*>(relocBlock) + sizeof(IMAGE_BASE_RELOCATION));

                for (DWORD i = 0; i < numEntries; ++i) {
                    WORD type = entries[i] >> 12;
                    WORD offset = entries[i] & 0x0FFF;

                    if (type == IMAGE_REL_BASED_DIR64) {
                        uintptr_t patchAddr = reinterpret_cast<uintptr_t>(remoteBase) +
                                              relocBlock->VirtualAddress + offset;
                        uint64_t val = 0;
                        ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(patchAddr),
                                         &val, sizeof(val), nullptr);
                        val += delta;
                        WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(patchAddr),
                                          &val, sizeof(val), &written);
                    } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                        uintptr_t patchAddr = reinterpret_cast<uintptr_t>(remoteBase) +
                                              relocBlock->VirtualAddress + offset;
                        DWORD val = 0;
                        ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(patchAddr),
                                         &val, sizeof(val), nullptr);
                        val += static_cast<DWORD>(delta);
                        WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(patchAddr),
                                          &val, sizeof(val), &written);
                    }
                }

                processed += relocBlock->SizeOfBlock;
                relocBlock = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                    reinterpret_cast<uint8_t*>(relocBlock) + relocBlock->SizeOfBlock);
            }
        }
    }

    // Call DllMain via CreateRemoteThread
    uintptr_t entryPoint = reinterpret_cast<uintptr_t>(remoteBase) +
                           ntHeaders->OptionalHeader.AddressOfEntryPoint;

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(entryPoint),
        remoteBase, 0, nullptr);

    if (!hThread) {
        s_status.lastError = "CreateRemoteThread failed";
        VirtualFreeEx(hProcess, remoteBase, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);

    s_status.manualMapAvailable = true;
    return true;
}

// ─── Direct Syscall Stubs ────────────────────────────────

bool AntiAntiCheat::UseSyscallStubs() {
    s_status.lastError.clear();

    // Get syscall numbers for the NT functions we need
    DWORD readNum = GetSyscallNumber("NtReadVirtualMemory");
    DWORD writeNum = GetSyscallNumber("NtWriteVirtualMemory");
    DWORD protectNum = GetSyscallNumber("NtProtectVirtualMemory");

    if (readNum == 0 || writeNum == 0 || protectNum == 0) {
        s_status.lastError = "Failed to resolve syscall numbers";
        return false;
    }

    // Allocate executable memory for our syscall stubs
    // Each stub: mov r10, rcx; mov eax, <num>; syscall; ret = 12 bytes
    void* stubMem = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
    if (!stubMem) {
        s_status.lastError = "VirtualAlloc for stubs failed";
        return false;
    }

    auto buildStub = [](uint8_t* buf, DWORD syscallNum) {
        buf[0] = 0x4C; buf[1] = 0x8B; buf[2] = 0xD1;           // mov r10, rcx
        buf[3] = 0xB8;                                            // mov eax, <imm32>
        std::memcpy(buf + 4, &syscallNum, 4);
        buf[8] = 0x0F; buf[9] = 0x05;                            // syscall
        buf[10] = 0xC3;                                           // ret
    };

    uint8_t* base = static_cast<uint8_t*>(stubMem);
    buildStub(base + 0, readNum);     // NtReadVirtualMemory stub
    buildStub(base + 16, writeNum);   // NtWriteVirtualMemory stub
    buildStub(base + 32, protectNum); // NtProtectVirtualMemory stub

    s_status.syscallStubsActive = true;
    return true;
}

// ─── Hide Handle ─────────────────────────────────────────

bool AntiAntiCheat::HideHandle(HANDLE hTarget) {
    s_status.lastError.clear();

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        s_status.lastError = "Cannot find ntdll.dll";
        return false;
    }

    auto pNtSetInformationObject = reinterpret_cast<NtSetInformationObject_t>(
        GetProcAddress(ntdll, "NtSetInformationObject"));

    if (!pNtSetInformationObject) {
        s_status.lastError = "Cannot find NtSetInformationObject";
        return false;
    }

    // ObjectHandleFlagInformation = 4
    // Set ProtectFromClose = true, Inherit = false
    struct OBJECT_HANDLE_FLAG_INFORMATION {
        BOOLEAN Inherit;
        BOOLEAN ProtectFromClose;
    } flagInfo;
    flagInfo.Inherit = FALSE;
    flagInfo.ProtectFromClose = TRUE;

    LONG status = pNtSetInformationObject(hTarget, 4, &flagInfo, sizeof(flagInfo));

    if (status != 0) {
        std::ostringstream oss;
        oss << "NtSetInformationObject failed: 0x" << std::hex << status;
        s_status.lastError = oss.str();
        return false;
    }

    s_status.handleHidden = true;
    return true;
}

// ─── Unlink From PEB ─────────────────────────────────────

bool AntiAntiCheat::UnlinkFromPEB() {
    s_status.lastError.clear();

#if defined(_M_X64) || defined(__x86_64__)
    // Access PEB directly via the TEB (Thread Environment Block)
    // On x64: PEB is at gs:[0x60]
    PEB_LDR_DATA_NT* ldr = nullptr;

    __try {
        // Read PEB address from TEB
        // TEB.ProcessEnvironmentBlock is at offset 0x60 on x64
        uintptr_t peb;
        peb = __readgsqword(0x60);

        // PEB.Ldr is at offset 0x18
        ldr = *reinterpret_cast<PEB_LDR_DATA_NT**>(peb + 0x18);
        if (!ldr) {
            s_status.lastError = "PEB Ldr is null";
            return false;
        }

        // Get our module's base address
        HMODULE ourModule = GetModuleHandleA(nullptr);

        // Walk InMemoryOrderModuleList
        LIST_ENTRY* head = &ldr->InMemoryOrderModuleList;
        LIST_ENTRY* current = head->Flink;

        while (current != head) {
            // LDR_DATA_TABLE_ENTRY: InMemoryOrderLinks is the second LIST_ENTRY
            // So container = current - offsetof(LDR_DATA_TABLE_ENTRY_NT, InMemoryOrderLinks)
            auto* entry = CONTAINING_RECORD(current, LDR_DATA_TABLE_ENTRY_NT, InMemoryOrderLinks);

            if (entry->DllBase == ourModule) {
                // Unlink from all three lists
                // InLoadOrderLinks
                entry->InLoadOrderLinks.Blink->Flink = entry->InLoadOrderLinks.Flink;
                entry->InLoadOrderLinks.Flink->Blink = entry->InLoadOrderLinks.Blink;

                // InMemoryOrderLinks
                entry->InMemoryOrderLinks.Blink->Flink = entry->InMemoryOrderLinks.Flink;
                entry->InMemoryOrderLinks.Flink->Blink = entry->InMemoryOrderLinks.Blink;

                // InInitializationOrderLinks
                entry->InInitializationOrderLinks.Blink->Flink = entry->InInitializationOrderLinks.Flink;
                entry->InInitializationOrderLinks.Flink->Blink = entry->InInitializationOrderLinks.Blink;

                s_status.pebUnlinked = true;
                return true;
            }

            current = current->Flink;
        }

        s_status.lastError = "Module not found in PEB";
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        s_status.lastError = "Exception accessing PEB";
        return false;
    }
#else
    s_status.lastError = "PEB unlinking only supported on x64";
    return false;
#endif
}

// ─── Hide Thread ─────────────────────────────────────────

bool AntiAntiCheat::HideThread(HANDLE hThread) {
    s_status.lastError.clear();

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        s_status.lastError = "Cannot find ntdll.dll";
        return false;
    }

    auto pNtSetInformationThread = reinterpret_cast<NtSetInformationThread_t>(
        GetProcAddress(ntdll, "NtSetInformationThread"));

    if (!pNtSetInformationThread) {
        s_status.lastError = "Cannot find NtSetInformationThread";
        return false;
    }

    // ThreadHideFromDebugger = 0x11
    LONG status = pNtSetInformationThread(hThread, 0x11, nullptr, 0);

    if (status != 0) {
        std::ostringstream oss;
        oss << "NtSetInformationThread failed: 0x" << std::hex << status;
        s_status.lastError = oss.str();
        return false;
    }

    s_status.threadHidden = true;
    return true;
}

// ─── Get Status ──────────────────────────────────────────

AacStatus& AntiAntiCheat::GetStatus() {
    return s_status;
}

} // namespace memforge
