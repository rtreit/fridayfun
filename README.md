# fridayfun — Debug Loader

A lightweight Windows process debugging toolkit. A C# loader loads a native DLL whose
`DLL_PROCESS_ATTACH` handler opens a handle to a target process for inspection.

## Prerequisites

### .NET 8 SDK

Download and install from https://dotnet.microsoft.com/download/dotnet/8.0

Verify: `dotnet --version` should show `8.x.x`.

### Visual Studio 2022 with C++ toolchain

Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community edition is fine).
In the installer, select these workloads/components:

- **Desktop development with C++** — includes MSVC compiler, Windows SDK, and C++ standard libraries
- **C++ CMake tools for Windows** — installs CMake bundled with VS

If you already have VS installed, open **Visual Studio Installer → Modify** and add the above.

### CMake on PATH (optional)

The VS installer puts CMake here, which is not on PATH by default:

```
C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\
```

You can either:
- Add that directory to your system PATH, or
- Install CMake standalone from https://cmake.org/download/ and check "Add to PATH" during install, or
- Use the full path in build commands

Verify: `cmake --version` should show `3.20+`.

## Components

| Project | Language | Description |
|---|---|---|
| `src/DebugLoader` | C# (.NET 8) | CLI that validates a target PID and loads the hook DLL via `LoadLibraryW`. |
| `src/DebugHook` | C++ (CMake) | Native DLL — opens a process handle in `DLL_PROCESS_ATTACH`, closes it in `DLL_PROCESS_DETACH`. |
| `src/TargetApp` | C++ (CMake) | Simple target process that prints its PID and keeps running for the debugger to attach to. |
| `src/TargetAppMinimal` | C (no CRT) | Ultra-minimal target process — no C runtime, ~4.5 KB binary, ~3.9 MB working set. Smallest possible minidump footprint. |
| `src/TargetAppRust` | Rust | Rust implementation of the target process. Uses std but optimized for size (LTO, strip, panic=abort). |
| `src/offensive-security/process-hollowing` | C++/Python/PowerShell | Educational process hollowing samples for testing detection capabilities. |

## Build Everything

From the repo root:

```powershell
# 1. Build the target app (pick one)
cd src\TargetApp
cmake -B build -A x64
cmake --build build --config Release

# 1b. Build the minimal no-CRT target (requires VS Developer Command Prompt)
cd src\TargetAppMinimal
cl /O1 /GS- TargetAppMinimal.c /link /NODEFAULTLIB /ENTRY:Entry kernel32.lib /SUBSYSTEM:CONSOLE /MERGE:.rdata=.text /OUT:TargetAppMinimal.exe

# 1c. Build the Rust target
cd src\TargetAppRust
cargo build --release

# 2. Build the hook DLL
cd ..\DebugHook
cmake -B build -A x64
cmake --build build --config Release

# 3. Build the C# loader
cd ..\DebugLoader
dotnet build

# 4. Copy the DLL next to the loader output
copy ..\..\src\DebugHook\build\Release\DebugHook.dll bin\Debug\net8.0\
```

## Run

**Step 1 — Start the target process** in one terminal:

```powershell
src\TargetApp\build\Release\TargetApp.exe
```

It will print its PID and wait:

```
=== TargetApp ===
PID: 12345
Waiting for debugger... (Ctrl+C to quit)

Records allocated at 0x0000020B4C6F0040 (400 bytes)
  [0] id=100 name=alpha      value=3.14
  [1] id=200 name=bravo      value=6.28
  ...
```

**Step 2 — Attach the debug loader** in a second terminal, using the PID from step 1:

```powershell
src\DebugLoader\bin\Debug\net8.0\DebugLoader.exe 12345
```

Output:

```
Target process: TargetApp (PID 12345)
Loading DLL: ...\DebugHook.dll
DLL loaded at 0x7FFFA6FF0000. DLL_PROCESS_ATTACH has fired.
Press Enter to unload the DLL and exit...
```

The hook DLL has now opened a handle to the target process with read/write/query access.

**Step 3 — Press Enter** in the loader terminal to unload the DLL (closes the handle) and exit.

**Step 4 — Press Ctrl+C** in the target terminal to shut it down.

## Offensive Security Tools

The repository also includes educational offensive security samples designed for testing detection capabilities:

### Process Hollowing Samples

Located in `src/offensive-security/process-hollowing/`, these tools demonstrate process hollowing techniques in multiple languages:

- **C++ implementation** - Native Windows API calls
- **Python version** - Using ctypes for Windows API access
- **PowerShell script** - .NET interop for Windows APIs

**⚠️ WARNING**: These tools are for educational and authorized testing purposes only. Do not use against systems you do not own or have explicit permission to test.

#### Usage Example:
```powershell
# PowerShell version (run as Administrator)
cd src\offensive-security\process-hollowing\
.\process_hollowing.ps1 -TargetPath "C:\Windows\System32\calc.exe"

# Python version
python process_hollowing.py "C:\Windows\System32\notepad.exe"

# C++ version (compile first)
.\compile.bat
.\process_hollowing.exe "C:\Windows\System32\calc.exe"
```

These samples include:
- Safe MessageBox payload for testing
- Detailed logging of each technique step
- Pause before execution for monitoring setup
- Comprehensive detection guidance in the README

See the [Process Hollowing README](src/offensive-security/process-hollowing/README.md) for detailed usage instructions and detection guidance.
