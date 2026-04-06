# Process Hollowing Test Samples

This repository contains educational process hollowing samples for testing detection capabilities. These tools are designed for security researchers, red teams, and blue teams to test their defensive measures.

## ⚠️ WARNING
These tools are for educational and authorized testing purposes only. Do not use these tools against systems you do not own or have explicit permission to test.

## Files Included

1. **process_hollowing.cpp** - C++ implementation
2. **process_hollowing.py** - Python implementation  
3. **process_hollowing.ps1** - PowerShell implementation
4. **compile.bat** - Compilation script for C++ version

## Process Hollowing Technique Overview

Process hollowing (also known as RunPE) is a technique where:

1. A legitimate process is created in a suspended state
2. The original executable image is unmapped from memory
3. Malicious code is written to the process memory space
4. The process execution is resumed, running the injected code

## Detection Points

When testing your detection tools, look for these indicators:

### API Calls to Monitor
- `CreateProcess` with `CREATE_SUSPENDED` flag
- `NtUnmapViewOfSection` or `ZwUnmapViewOfSection`
- `VirtualAllocEx` with execute permissions
- `WriteProcessMemory` to allocated regions
- `SetThreadContext` or `NtSetContextThread`
- `ResumeThread` after suspension

### Memory Indicators
- Processes with unmapped main modules
- Executable memory regions not backed by files
- Mismatched process names vs. running code
- Unusual memory protection changes
- Process hollows showing different image paths in PEB vs. loaded modules

### Behavioral Indicators
- Parent-child process relationships that don't make sense
- Processes spawned suspended and then resumed
- Network connections from unexpected processes
- File system activity from processes that shouldn't be performing it

## Usage

### C++ Version
```bash
# Compile with Visual Studio
compile.bat

# Or with MinGW
g++ -o process_hollowing.exe process_hollowing.cpp -static

# Run
process_hollowing.exe [target_executable_path]
process_hollowing.exe C:\Windows\System32\calc.exe
```

### Python Version
```bash
# Requires Python on Windows with ctypes
python process_hollowing.py [target_executable_path]
python process_hollowing.py C:\Windows\System32\calc.exe
```

### PowerShell Version
```powershell
# Run as Administrator for full functionality
.\process_hollowing.ps1 -TargetPath "C:\Windows\System32\calc.exe"
.\process_hollowing.ps1 -TargetPath "C:\Windows\System32\notepad.exe" -Verbose
```

## Default Behavior

- **Default Target**: `C:\Windows\System32\notepad.exe`
- **Payload**: Simple MessageBox shellcode that displays "Hello world!"
- **Execution**: The tool will pause before resuming execution, giving you time to attach monitoring tools

## Testing Your Detections

1. **Before Running**: Set up your monitoring tools (EDR, custom scripts, etc.)
2. **Run the Tool**: Execute one of the implementations
3. **Analyze Results**: Check if your tools detected the hollowing attempt
4. **Tune Detection**: Adjust your detection rules based on observed behavior

### Common Detection Bypasses to Test
- Different target processes (system vs. user processes)
- Various payload types and sizes
- Different timing between steps
- Alternative API usage patterns

## Educational Value

These samples demonstrate:
- Low-level Windows API usage
- Process and thread manipulation
- Memory management techniques
- PE structure understanding
- Detection evasion concepts

## Defensive Recommendations

1. **Monitor API Calls**: Track the sequence of APIs used in process hollowing
2. **Memory Analysis**: Scan for executable memory not backed by legitimate files
3. **Process Tree Analysis**: Look for unusual parent-child relationships
4. **Behavioral Analysis**: Monitor for processes performing unexpected actions
5. **Image Verification**: Compare loaded modules with expected process behavior

## Legal and Ethical Considerations

- Only use on systems you own or have explicit permission to test
- Ensure compliance with your organization's security testing policies
- Do not use for malicious purposes
- Consider the legal implications in your jurisdiction

## Contributing

If you find issues or want to add additional detection scenarios, please contribute responsibly and ensure all submissions maintain the educational focus.

## References

- [MITRE ATT&CK T1055.012](https://attack.mitre.org/techniques/T1055/012/) - Process Injection: Process Hollowing
- Windows API Documentation
- PE Format Specifications
- Various security research papers on process injection techniques