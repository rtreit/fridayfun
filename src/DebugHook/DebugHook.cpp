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
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD,
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
