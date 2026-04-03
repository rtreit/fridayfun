using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace DebugLoader;

internal static partial class NativeMethods
{
    [LibraryImport("kernel32.dll", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    public static partial IntPtr LoadLibraryW(string lpFileName);

    [LibraryImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool FreeLibrary(IntPtr hModule);

    [LibraryImport("kernel32.dll", SetLastError = true, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr GetProcAddress(IntPtr hModule, string lpProcName);
}

internal static class Program
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int HookAction();
    // The DLL communicates the target PID via this environment variable.
    private const string TargetPidEnvVar = "DEBUGHOOK_TARGET_PID";

    static int Main(string[] args)
    {
        if (args.Length < 1 || !int.TryParse(args[0], out int targetPid))
        {
            Console.Error.WriteLine("Usage: DebugLoader <target_pid>");
            return 1;
        }

        // Verify the target process exists before loading the hook.
        try
        {
            using var proc = Process.GetProcessById(targetPid);
            Console.WriteLine($"Target process: {proc.ProcessName} (PID {targetPid})");
        }
        catch (ArgumentException)
        {
            Console.Error.WriteLine($"Error: No process with PID {targetPid} found.");
            return 1;
        }

        // Tell the DLL which process to open a handle to.
        Environment.SetEnvironmentVariable(TargetPidEnvVar, targetPid.ToString());

        string dllPath = Path.Combine(AppContext.BaseDirectory, "DebugHook.dll");
        if (!File.Exists(dllPath))
        {
            Console.Error.WriteLine($"Error: DLL not found at {dllPath}");
            return 1;
        }

        Console.WriteLine($"Loading DLL: {dllPath}");
        IntPtr hModule = NativeMethods.LoadLibraryW(dllPath);
        if (hModule == IntPtr.Zero)
        {
            int err = Marshal.GetLastWin32Error();
            Console.Error.WriteLine($"LoadLibrary failed (error {err}): {new Win32Exception(err).Message}");
            return 1;
        }

        Console.WriteLine($"DLL loaded at 0x{hModule:X}. DLL_PROCESS_ATTACH has fired.");

        // Resolve exported functions from the hook DLL.
        var pSuspend = NativeMethods.GetProcAddress(hModule, "SuspendTargetProcess");
        var pResume = NativeMethods.GetProcAddress(hModule, "ResumeTargetProcess");

        if (pSuspend == IntPtr.Zero || pResume == IntPtr.Zero)
        {
            Console.Error.WriteLine("Warning: Could not resolve suspend/resume exports.");
        }

        var suspendFn = pSuspend != IntPtr.Zero
            ? Marshal.GetDelegateForFunctionPointer<HookAction>(pSuspend) : null;
        var resumeFn = pResume != IntPtr.Zero
            ? Marshal.GetDelegateForFunctionPointer<HookAction>(pResume) : null;

        Console.WriteLine("\nCommands:  (p)ause  (r)esume  (q)uit");

        bool running = true;
        while (running)
        {
            Console.Write("> ");
            string? input = Console.ReadLine()?.Trim().ToLowerInvariant();
            switch (input)
            {
                case "p" or "pause":
                    if (suspendFn is null) { Console.WriteLine("Suspend not available."); break; }
                    int sr = suspendFn();
                    Console.WriteLine(sr == 0 ? "Process paused." : $"Suspend failed (NTSTATUS 0x{sr:X}).");
                    break;

                case "r" or "resume":
                    if (resumeFn is null) { Console.WriteLine("Resume not available."); break; }
                    int rr = resumeFn();
                    Console.WriteLine(rr == 0 ? "Process resumed." : $"Resume failed (NTSTATUS 0x{rr:X}).");
                    break;

                case "q" or "quit":
                    running = false;
                    break;

                default:
                    Console.WriteLine("Unknown command. Use (p)ause, (r)esume, or (q)uit.");
                    break;
            }
        }

        NativeMethods.FreeLibrary(hModule);
        Console.WriteLine("DLL unloaded.");
        return 0;
    }
}
