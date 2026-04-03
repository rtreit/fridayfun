#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE g_hTargetProcess = NULL;

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
