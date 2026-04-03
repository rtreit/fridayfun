#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple struct to give us something interesting to inspect in memory.
struct Record
{
    int id;
    char name[64];
    double value;
};

static volatile bool g_running = true;

static BOOL WINAPI ConsoleHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT)
    {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int main()
{
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    printf("=== TargetApp ===\n");
    printf("PID: %lu\n", GetCurrentProcessId());
    printf("Waiting for debugger... (Ctrl+C to quit)\n\n");

    // Allocate some records so there's data to read from another process.
    const int count = 5;
    Record* records = (Record*)malloc(sizeof(Record) * count);

    const char* names[] = {"alpha", "bravo", "charlie", "delta", "echo"};
    for (int i = 0; i < count; i++)
    {
        records[i].id = (i + 1) * 100;
        strncpy_s(records[i].name, sizeof(records[i].name), names[i], _TRUNCATE);
        records[i].value = (i + 1) * 3.14;
    }

    printf("Records allocated at 0x%p (%d bytes)\n", records, (int)(sizeof(Record) * count));
    for (int i = 0; i < count; i++)
    {
        printf("  [%d] id=%d name=%-10s value=%.2f\n",
               i, records[i].id, records[i].name, records[i].value);
    }
    printf("\n");

    // Spin with a counter so there's changing state to observe.
    unsigned long long tick = 0;
    while (g_running)
    {
        tick++;
        if (tick % 5000000 == 0)
        {
            printf("\rHeartbeat: %llu  ", tick / 5000000);
            fflush(stdout);
        }
    }

    printf("\nShutting down.\n");
    free(records);
    return 0;
}
