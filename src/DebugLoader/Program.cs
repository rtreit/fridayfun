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
        Console.WriteLine("Press Enter to unload the DLL and exit...");
        Console.ReadLine();

        NativeMethods.FreeLibrary(hModule);
        Console.WriteLine("DLL unloaded.");
        return 0;
    }
}
