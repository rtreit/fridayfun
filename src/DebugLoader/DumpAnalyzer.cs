using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace DebugLoader;

/// <summary>
/// Parses a minidump from an in-memory buffer and prints structured analysis.
/// </summary>
internal static class DumpAnalyzer
{
    // MDMP header — 32 bytes at offset 0.
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpHeader
    {
        public uint Signature;       // "MDMP"
        public ushort Version;
        public ushort ImplementationVersion;
        public uint NumberOfStreams;
        public uint StreamDirectoryRva;
        public uint CheckSum;
        public uint TimeDateStamp;
        public ulong Flags;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpDirectory
    {
        public uint StreamType;
        public uint DataSize;
        public uint Rva;
    }

    // Stream types we care about.
    private const uint ThreadListStream = 3;
    private const uint ModuleListStream = 4;
    private const uint MemoryListStream = 5;
    private const uint SystemInfoStream = 7;
    private const uint Memory64ListStream = 9;
    private const uint MemoryInfoListStream = 16;

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpSystemInfo
    {
        public ushort ProcessorArchitecture;
        public ushort ProcessorLevel;
        public ushort ProcessorRevision;
        public byte NumberOfProcessors;
        public byte ProductType;
        public uint MajorVersion;
        public uint MinorVersion;
        public uint BuildNumber;
        public uint PlatformId;
        public uint CSDVersionRva;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpThread
    {
        public uint ThreadId;
        public uint SuspendCount;
        public uint PriorityClass;
        public uint Priority;
        public ulong Teb;
        public uint StackStartOfMemoryRange_Lo;
        public uint StackStartOfMemoryRange_Hi;
        public uint StackMemoryDataSize;
        public uint StackMemoryRva;
        public uint ThreadContextDataSize;
        public uint ThreadContextRva;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpLocationDescriptor
    {
        public uint DataSize;
        public uint Rva;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct VsFixedFileInfo
    {
        public uint dwSignature;
        public uint dwStrucVersion;
        public uint dwFileVersionMS;
        public uint dwFileVersionLS;
        public uint dwProductVersionMS;
        public uint dwProductVersionLS;
        public uint dwFileFlagsMask;
        public uint dwFileFlags;
        public uint dwFileOS;
        public uint dwFileType;
        public uint dwFileSubtype;
        public uint dwFileDateMS;
        public uint dwFileDateLS;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpModule
    {
        public ulong BaseOfImage;
        public uint SizeOfImage;
        public uint CheckSum;
        public uint TimeDateStamp;
        public uint ModuleNameRva;
        public VsFixedFileInfo VersionInfo;
        public MinidumpLocationDescriptor CvRecord;
        public MinidumpLocationDescriptor MiscRecord;
        public ulong Reserved0;
        public ulong Reserved1;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpMemoryDescriptor
    {
        public ulong StartOfMemoryRange;
        public uint DataSize;
        public uint Rva;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct MinidumpMemoryInfoEntry
    {
        public ulong BaseAddress;
        public ulong AllocationBase;
        public uint AllocationProtect;
        public uint Alignment1;
        public ulong RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
        public uint Alignment2;
    }

    public static void Analyze(IntPtr pBuffer, ulong bufferSize)
    {
        if (bufferSize < (ulong)Marshal.SizeOf<MinidumpHeader>())
        {
            Console.WriteLine("Buffer too small for MDMP header.");
            return;
        }

        var header = Marshal.PtrToStructure<MinidumpHeader>(pBuffer);
        string sig = Encoding.ASCII.GetString(BitConverter.GetBytes(header.Signature));
        if (sig != "MDMP")
        {
            Console.WriteLine($"Invalid signature: {sig}");
            return;
        }

        var timestamp = DateTimeOffset.FromUnixTimeSeconds(header.TimeDateStamp).UtcDateTime;

        Console.WriteLine();
        Console.WriteLine("═══════════════════════════════════════════════════════════");
        Console.WriteLine("  MINIDUMP ANALYSIS");
        Console.WriteLine("═══════════════════════════════════════════════════════════");
        Console.WriteLine($"  Dump size:     {bufferSize:N0} bytes");
        Console.WriteLine($"  Version:       {header.Version}.{header.ImplementationVersion}");
        Console.WriteLine($"  Streams:       {header.NumberOfStreams}");
        Console.WriteLine($"  Timestamp:     {timestamp:u}");
        Console.WriteLine($"  Flags:         0x{header.Flags:X}");

        // Read stream directory.
        var streams = new List<MinidumpDirectory>();
        for (uint i = 0; i < header.NumberOfStreams; i++)
        {
            IntPtr dirPtr = pBuffer + (int)header.StreamDirectoryRva
                          + (int)(i * Marshal.SizeOf<MinidumpDirectory>());
            streams.Add(Marshal.PtrToStructure<MinidumpDirectory>(dirPtr));
        }

        foreach (var stream in streams)
        {
            switch (stream.StreamType)
            {
                case SystemInfoStream:
                    PrintSystemInfo(pBuffer, stream);
                    break;
                case ThreadListStream:
                    PrintThreads(pBuffer, stream);
                    break;
                case ModuleListStream:
                    PrintModules(pBuffer, stream);
                    break;
                case MemoryListStream:
                    PrintMemoryRegions(pBuffer, stream);
                    break;
                case MemoryInfoListStream:
                    PrintMemoryInfo(pBuffer, stream);
                    break;
            }
        }

        Console.WriteLine("═══════════════════════════════════════════════════════════");
        Console.WriteLine();
    }

    private static void PrintSystemInfo(IntPtr pBuffer, MinidumpDirectory stream)
    {
        var info = Marshal.PtrToStructure<MinidumpSystemInfo>(pBuffer + (int)stream.Rva);
        string arch = info.ProcessorArchitecture switch
        {
            0 => "x86",
            5 => "ARM",
            9 => "x64",
            12 => "ARM64",
            _ => $"Unknown ({info.ProcessorArchitecture})"
        };

        Console.WriteLine();
        Console.WriteLine("─── SYSTEM INFO ───────────────────────────────────────────");
        Console.WriteLine($"  Architecture:  {arch}");
        Console.WriteLine($"  Processors:    {info.NumberOfProcessors}");
        Console.WriteLine($"  OS Version:    {info.MajorVersion}.{info.MinorVersion}.{info.BuildNumber}");
        Console.WriteLine($"  Processor:     Family {info.ProcessorLevel} Rev {info.ProcessorRevision:X4}");
    }

    private static void PrintThreads(IntPtr pBuffer, MinidumpDirectory stream)
    {
        IntPtr basePtr = pBuffer + (int)stream.Rva;
        uint count = (uint)Marshal.ReadInt32(basePtr);

        Console.WriteLine();
        Console.WriteLine("─── THREADS ───────────────────────────────────────────────");
        Console.WriteLine($"  {"TID",-10} {"TEB",-18} {"Stack Base",-18} {"Stack Size",-12} {"Priority"}");

        int offset = 4; // skip count field
        for (uint i = 0; i < count; i++)
        {
            var thread = Marshal.PtrToStructure<MinidumpThread>(basePtr + offset);
            ulong stackBase = ((ulong)thread.StackStartOfMemoryRange_Hi << 32)
                            | thread.StackStartOfMemoryRange_Lo;

            Console.WriteLine($"  0x{thread.ThreadId,-8:X} 0x{thread.Teb,-16:X} 0x{stackBase,-16:X} {thread.StackMemoryDataSize,-12:N0} {thread.Priority}");
            offset += Marshal.SizeOf<MinidumpThread>();
        }
    }

    private static void PrintModules(IntPtr pBuffer, MinidumpDirectory stream)
    {
        IntPtr basePtr = pBuffer + (int)stream.Rva;
        uint count = (uint)Marshal.ReadInt32(basePtr);

        Console.WriteLine();
        Console.WriteLine("─── MODULES ───────────────────────────────────────────────");
        Console.WriteLine($"  {"Base Address",-18} {"Size",-12} {"Name"}");

        int offset = 4; // skip count field
        for (uint i = 0; i < count; i++)
        {
            var mod = Marshal.PtrToStructure<MinidumpModule>(basePtr + offset);
            string name = ReadMinidumpString(pBuffer, mod.ModuleNameRva);

            Console.WriteLine($"  0x{mod.BaseOfImage,-16:X} {mod.SizeOfImage,-12:N0} {name}");
            offset += Marshal.SizeOf<MinidumpModule>();
        }
    }

    private static void PrintMemoryRegions(IntPtr pBuffer, MinidumpDirectory stream)
    {
        IntPtr basePtr = pBuffer + (int)stream.Rva;
        uint count = (uint)Marshal.ReadInt32(basePtr);

        Console.WriteLine();
        Console.WriteLine("─── MEMORY REGIONS ────────────────────────────────────────");
        Console.WriteLine($"  {"Address",-18} {"Size",-12}");

        int offset = 4;
        for (uint i = 0; i < count; i++)
        {
            var desc = Marshal.PtrToStructure<MinidumpMemoryDescriptor>(basePtr + offset);
            Console.WriteLine($"  0x{desc.StartOfMemoryRange,-16:X} {desc.DataSize,-12:N0}");
            offset += Marshal.SizeOf<MinidumpMemoryDescriptor>();
        }
    }

    private static void PrintMemoryInfo(IntPtr pBuffer, MinidumpDirectory stream)
    {
        IntPtr basePtr = pBuffer + (int)stream.Rva;

        // MINIDUMP_MEMORY_INFO_LIST: SizeOfHeader, SizeOfEntry, NumberOfEntries
        uint sizeOfHeader = (uint)Marshal.ReadInt32(basePtr);
        uint sizeOfEntry = (uint)Marshal.ReadInt32(basePtr + 4);
        ulong numberOfEntries = (ulong)Marshal.ReadInt64(basePtr + 8);

        // Limit output for readability.
        ulong displayCount = Math.Min(numberOfEntries, 30);

        Console.WriteLine();
        Console.WriteLine("─── MEMORY INFO (virtual regions) ─────────────────────────");
        Console.WriteLine($"  {numberOfEntries} total regions (showing first {displayCount})");
        Console.WriteLine($"  {"Base Address",-18} {"Region Size",-14} {"State",-12} {"Protect",-10} {"Type"}");

        int offset = (int)sizeOfHeader;
        for (ulong i = 0; i < displayCount; i++)
        {
            var entry = Marshal.PtrToStructure<MinidumpMemoryInfoEntry>(basePtr + offset);
            string state = entry.State switch
            {
                0x1000 => "COMMIT",
                0x2000 => "RESERVE",
                0x10000 => "FREE",
                _ => $"0x{entry.State:X}"
            };
            string protect = FormatProtection(entry.Protect);
            string type = entry.Type switch
            {
                0x20000 => "PRIVATE",
                0x40000 => "MAPPED",
                0x1000000 => "IMAGE",
                0 => "-",
                _ => $"0x{entry.Type:X}"
            };

            Console.WriteLine($"  0x{entry.BaseAddress,-16:X} {entry.RegionSize,-14:N0} {state,-12} {protect,-10} {type}");
            offset += (int)sizeOfEntry;
        }

        if (numberOfEntries > displayCount)
            Console.WriteLine($"  ... {numberOfEntries - displayCount} more regions omitted");
    }

    private static string FormatProtection(uint protect)
    {
        return protect switch
        {
            0x01 => "NOACCESS",
            0x02 => "R",
            0x04 => "RW",
            0x08 => "WRITECOPY",
            0x10 => "X",
            0x20 => "RX",
            0x40 => "RWX",
            0x80 => "XCOPY",
            0 => "-",
            _ => $"0x{protect:X}"
        };
    }

    /// <summary>
    /// Reads a MINIDUMP_STRING (length-prefixed UTF-16) at the given RVA.
    /// </summary>
    private static string ReadMinidumpString(IntPtr pBuffer, uint rva)
    {
        IntPtr strPtr = pBuffer + (int)rva;
        uint length = (uint)Marshal.ReadInt32(strPtr); // byte length
        return Marshal.PtrToStringUni(strPtr + 4, (int)(length / 2)) ?? "<null>";
    }
}
