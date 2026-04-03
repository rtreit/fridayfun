#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "dbghelp.lib")

static HANDLE g_hTargetProcess = NULL;
static void* g_pDumpBuffer = NULL;
static SIZE_T g_dumpSize = 0;

// Read the target PID from the environment variable set by the loader.
static DWORD GetTargetPid()
{
    char buf[32] = {};
    DWORD len = GetEnvironmentVariableA("DEBUGHOOK_TARGET_PID", buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf))
        return 0;
    return (DWORD)strtoul(buf, NULL, 10);
}

static void OnProcessAttach()
{
    DWORD targetPid = GetTargetPid();
    if (targetPid == 0)
    {
        OutputDebugStringA("DebugHook: DEBUGHOOK_TARGET_PID not set or invalid.\n");
        return;
    }

    // Open a handle with debug-useful access rights.
    g_hTargetProcess = OpenProcess(
        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_SUSPEND_RESUME,
        FALSE,
        targetPid);

    if (g_hTargetProcess == NULL)
    {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "DebugHook: OpenProcess(%lu) failed, error %lu\n",
                 targetPid, GetLastError());
        OutputDebugStringA(msg);
    }
    else
    {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "DebugHook: Opened handle 0x%p to PID %lu\n",
                 g_hTargetProcess, targetPid);
        OutputDebugStringA(msg);
    }
}

static void OnProcessDetach()
{
    if (g_hTargetProcess != NULL)
    {
        CloseHandle(g_hTargetProcess);
        g_hTargetProcess = NULL;
        OutputDebugStringA("DebugHook: Closed target process handle.\n");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        OnProcessAttach();
        break;
    case DLL_PROCESS_DETACH:
        OnProcessDetach();
        break;
    }
    return TRUE;
}

// Exported function to let the loader query the handle.
extern "C" __declspec(dllexport) HANDLE GetTargetProcessHandle()
{
    return g_hTargetProcess;
}

// NtSuspendProcess / NtResumeProcess from ntdll.
typedef LONG(NTAPI* PFN_NtSuspendProcess)(HANDLE);
typedef LONG(NTAPI* PFN_NtResumeProcess)(HANDLE);

static PFN_NtSuspendProcess pfnSuspend = nullptr;
static PFN_NtResumeProcess pfnResume = nullptr;

static bool EnsureNtFunctions()
{
    if (pfnSuspend && pfnResume)
        return true;

    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll)
        return false;

    pfnSuspend = (PFN_NtSuspendProcess)GetProcAddress(hNtdll, "NtSuspendProcess");
    pfnResume = (PFN_NtResumeProcess)GetProcAddress(hNtdll, "NtResumeProcess");
    return pfnSuspend && pfnResume;
}

// Returns 0 on success, NTSTATUS error otherwise.
extern "C" __declspec(dllexport) LONG SuspendTargetProcess()
{
    if (!g_hTargetProcess || !EnsureNtFunctions())
        return -1;
    return pfnSuspend(g_hTargetProcess);
}

extern "C" __declspec(dllexport) LONG ResumeTargetProcess()
{
    if (!g_hTargetProcess || !EnsureNtFunctions())
        return -1;
    return pfnResume(g_hTargetProcess);
}

// Creates a minidump of the target process into a memory buffer.
// Returns the buffer pointer via ppBuffer and size via pSize.
// Returns 0 on success, Win32 error code on failure.
extern "C" __declspec(dllexport) DWORD CreateTargetDump(void** ppBuffer, SIZE_T* pSize, DWORD dumpType)
{
    if (!g_hTargetProcess || !ppBuffer || !pSize)
        return ERROR_INVALID_PARAMETER;

    // Free any previous dump.
    if (g_pDumpBuffer)
    {
        VirtualFree(g_pDumpBuffer, 0, MEM_RELEASE);
        g_pDumpBuffer = NULL;
        g_dumpSize = 0;
    }

    // Write the dump to a temp file first (MiniDumpWriteDump requires a file handle),
    // then read it into a memory buffer.
    char tempPath[MAX_PATH], tempFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    GetTempFileNameA(tempPath, "dmp", 0, tempFile);

    HANDLE hFile = CreateFileA(tempFile, GENERIC_READ | GENERIC_WRITE, 0,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return GetLastError();

    DWORD targetPid = GetProcessId(g_hTargetProcess);

    BOOL ok = MiniDumpWriteDump(
        g_hTargetProcess,
        targetPid,
        hFile,
        (MINIDUMP_TYPE)dumpType,
        NULL, NULL, NULL);

    if (!ok)
    {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        return err;
    }

    // Get file size and read into memory buffer.
    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    g_dumpSize = (SIZE_T)fileSize.QuadPart;

    g_pDumpBuffer = VirtualAlloc(NULL, g_dumpSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_pDumpBuffer)
    {
        DWORD err = GetLastError();
        CloseHandle(hFile);
        g_dumpSize = 0;
        return err;
    }

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    DWORD bytesRead = 0;
    ReadFile(hFile, g_pDumpBuffer, (DWORD)g_dumpSize, &bytesRead, NULL);
    CloseHandle(hFile);  // FILE_FLAG_DELETE_ON_CLOSE removes the temp file.

    *ppBuffer = g_pDumpBuffer;
    *pSize = g_dumpSize;

    char msg[128];
    snprintf(msg, sizeof(msg), "DebugHook: Dump created, %zu bytes at 0x%p\n", g_dumpSize, g_pDumpBuffer);
    OutputDebugStringA(msg);
    return 0;
}

// Frees the dump buffer allocated by CreateTargetDump.
extern "C" __declspec(dllexport) void FreeTargetDump()
{
    if (g_pDumpBuffer)
    {
        VirtualFree(g_pDumpBuffer, 0, MEM_RELEASE);
        g_pDumpBuffer = NULL;
        g_dumpSize = 0;
    }
}
