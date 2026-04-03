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

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint CreateDumpDelegate(out IntPtr ppBuffer, out UIntPtr pSize, uint dumpType);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void FreeDumpDelegate();
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
        var pCreateDump = NativeMethods.GetProcAddress(hModule, "CreateTargetDump");
        var pFreeDump = NativeMethods.GetProcAddress(hModule, "FreeTargetDump");

        if (pSuspend == IntPtr.Zero || pResume == IntPtr.Zero)
            Console.Error.WriteLine("Warning: Could not resolve suspend/resume exports.");
        if (pCreateDump == IntPtr.Zero || pFreeDump == IntPtr.Zero)
            Console.Error.WriteLine("Warning: Could not resolve dump exports.");

        var suspendFn = pSuspend != IntPtr.Zero
            ? Marshal.GetDelegateForFunctionPointer<HookAction>(pSuspend) : null;
        var resumeFn = pResume != IntPtr.Zero
            ? Marshal.GetDelegateForFunctionPointer<HookAction>(pResume) : null;
        var createDumpFn = pCreateDump != IntPtr.Zero
            ? Marshal.GetDelegateForFunctionPointer<CreateDumpDelegate>(pCreateDump) : null;
        var freeDumpFn = pFreeDump != IntPtr.Zero
            ? Marshal.GetDelegateForFunctionPointer<FreeDumpDelegate>(pFreeDump) : null;

        Console.WriteLine("\nCommands:  (p)ause  (r)esume  (d)ump  (q)uit");

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

                case "d" or "dump":
                    if (createDumpFn is null || freeDumpFn is null)
                    {
                        Console.WriteLine("Dump not available.");
                        break;
                    }
                    // MiniDumpNormal = 0x0, MiniDumpWithFullMemory = 0x2
                    Console.Write("Dump type — (n)ormal or (f)ull memory? [n]: ");
                    string? dumpChoice = Console.ReadLine()?.Trim().ToLowerInvariant();
                    uint dumpType = (dumpChoice is "f" or "full") ? 0x2u : 0x0u;

                    Console.Write("Creating dump... ");
                    uint hr = createDumpFn(out IntPtr pBuf, out UIntPtr size, dumpType);
                    if (hr != 0)
                    {
                        Console.WriteLine($"failed (error {hr}).");
                        break;
                    }
                    ulong sizeBytes = (ulong)size;
                    Console.WriteLine($"done. {sizeBytes:N0} bytes at 0x{pBuf:X}.");

                    Console.Write("Save to file? Enter path (or press Enter to discard): ");
                    string? savePath = Console.ReadLine()?.Trim();
                    if (!string.IsNullOrEmpty(savePath))
                    {
                        try
                        {
                            byte[] buf = new byte[sizeBytes];
                            Marshal.Copy(pBuf, buf, 0, (int)sizeBytes);
                            File.WriteAllBytes(savePath, buf);
                            Console.WriteLine($"Saved to {savePath}");
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"Save failed: {ex.Message}");
                        }
                    }
                    freeDumpFn();
                    Console.WriteLine("Dump buffer freed.");
                    break;

                default:
                    Console.WriteLine("Unknown command. Use (p)ause, (r)esume, (d)ump, or (q)uit.");
                    break;
            }
        }

        NativeMethods.FreeLibrary(hModule);
        Console.WriteLine("DLL unloaded.");
        return 0;
    }
}
