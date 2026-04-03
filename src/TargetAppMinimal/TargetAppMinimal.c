/*
 * TargetAppMinimal — A tiny target process with NO C Runtime.
 *
 * This avoids ucrtbase.dll and vcruntime140.dll entirely, loading only
 * the mandatory kernel modules (ntdll, kernel32, kernelbase).
 * Result: smallest possible Windows process footprint.
 *
 * Build with cl.exe:
 *   cl /O1 /GS- TargetAppMinimal.c /link /NODEFAULTLIB /ENTRY:Entry
 *       kernel32.lib /SUBSYSTEM:CONSOLE /MERGE:.rdata=.text
 */
#pragma comment(lib, "kernel32.lib")

/* We can't use any CRT headers, so declare what we need from Windows. */
typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef void*               HANDLE;
typedef const char*         LPCSTR;
typedef unsigned long long  ULONGLONG;

#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define CTRL_C_EVENT       0
#define CTRL_BREAK_EVENT   1
#define INFINITE           0xFFFFFFFF
#define FALSE              0
#define TRUE               1

__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL   __stdcall WriteConsoleA(HANDLE h, const void* buf,
                                                     DWORD nChars, DWORD* written, void* reserved);
__declspec(dllimport) DWORD  __stdcall GetCurrentProcessId(void);
__declspec(dllimport) BOOL   __stdcall SetConsoleCtrlHandler(
    BOOL (__stdcall *HandlerRoutine)(DWORD), BOOL Add);
__declspec(dllimport) void   __stdcall Sleep(DWORD dwMilliseconds);
__declspec(dllimport) void   __stdcall ExitProcess(unsigned int uExitCode);

/* Satisfy the linker's float-usage marker and intrinsic requirements */
int _fltused = 0;

#pragma function(memset)
void* memset(void* dst, int val, unsigned long long size)
{
    unsigned char* d = (unsigned char*)dst;
    while (size--) *d++ = (unsigned char)val;
    return dst;
}

#pragma function(memcpy)
void* memcpy(void* dst, const void* src, unsigned long long size)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (size--) *d++ = *s++;
    return dst;
}

/* ─── Minimal helpers (no CRT available) ─── */

static void write_str(HANDLE h, const char* s)
{
    DWORD len = 0;
    const char* p = s;
    while (*p++) len++;
    DWORD written;
    WriteConsoleA(h, s, len, &written, 0);
}

static void write_num(HANDLE h, ULONGLONG n)
{
    char buf[20];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else { while (n > 0) { buf[i++] = '0' + (char)(n % 10); n /= 10; } }
    /* reverse */
    char tmp;
    for (int j = 0; j < i / 2; j++)
    {
        tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    DWORD written;
    WriteConsoleA(h, buf, (DWORD)i, &written, 0);
}

/* ─── Ctrl+C handler ─── */

static volatile BOOL g_running = TRUE;

static BOOL __stdcall ConsoleHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT)
    {
        g_running = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* ─── Record struct for memory inspection ─── */

#pragma pack(push, 1)
typedef struct {
    int id;
    char name[64];
    double value;
} Record;
#pragma pack(pop)

/* Simple strlen without CRT */
static DWORD my_strlen(const char* s)
{
    DWORD len = 0;
    while (*s++) len++;
    return len;
}

/* HeapAlloc for our tiny allocation */
__declspec(dllimport) HANDLE __stdcall GetProcessHeap(void);
__declspec(dllimport) void*  __stdcall HeapAlloc(HANDLE hHeap, DWORD dwFlags, DWORD dwBytes);
__declspec(dllimport) BOOL   __stdcall HeapFree(HANDLE hHeap, DWORD dwFlags, void* lpMem);

/* ─── Entry point (replaces main/WinMain) ─── */

void __stdcall Entry(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    write_str(hOut, "=== TargetApp (Minimal / No CRT) ===\r\n");
    write_str(hOut, "PID: ");
    write_num(hOut, GetCurrentProcessId());
    write_str(hOut, "\r\nWaiting for debugger... (Ctrl+C to quit)\r\n\r\n");

    /* Allocate records on the process heap */
    const int count = 5;
    HANDLE heap = GetProcessHeap();
    Record* records = (Record*)HeapAlloc(heap, 0, sizeof(Record) * count);

    const char* names[] = {"alpha", "bravo", "charlie", "delta", "echo"};
    double multiplier = 3.14;
    for (int i = 0; i < count; i++)
    {
        records[i].id = (i + 1) * 100;
        memset(records[i].name, 0, 64);
        DWORD nlen = my_strlen(names[i]);
        memcpy(records[i].name, names[i], nlen);
        records[i].value = (i + 1) * multiplier;
    }

    write_str(hOut, "Records allocated (");
    write_num(hOut, sizeof(Record) * count);
    write_str(hOut, " bytes)\r\n");

    for (int i = 0; i < count; i++)
    {
        write_str(hOut, "  [");
        write_num(hOut, i);
        write_str(hOut, "] id=");
        write_num(hOut, records[i].id);
        write_str(hOut, " name=");
        write_str(hOut, records[i].name);
        write_str(hOut, "\r\n");
    }
    write_str(hOut, "\r\n");

    /* Spin with heartbeat */
    ULONGLONG tick = 0;
    while (g_running)
    {
        tick++;
        if (tick % 5000000 == 0)
        {
            write_str(hOut, "\rHeartbeat: ");
            write_num(hOut, tick / 5000000);
            write_str(hOut, "  ");
        }
    }

    write_str(hOut, "\r\nShutting down.\r\n");
    HeapFree(heap, 0, records);
    ExitProcess(0);
}
