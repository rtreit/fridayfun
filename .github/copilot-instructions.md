# Copilot Instructions

## Architecture

This is a two-component Windows debugging toolkit:

- **DebugLoader** (`src/DebugLoader/`) — C# .NET 8 console app that validates a target PID and loads the native hook DLL via `LoadLibraryW`. It communicates the target PID to the DLL through the `DEBUGHOOK_TARGET_PID` environment variable.
- **DebugHook** (`src/DebugHook/`) — Native C++ DLL (built with CMake) whose `DllMain` handles `DLL_PROCESS_ATTACH` to call `OpenProcess` on the target PID, and `DLL_PROCESS_DETACH` to close the handle. Exports `GetTargetProcessHandle()` for callers to retrieve the opened handle.

The loader sets an env var, then calls `LoadLibraryW` — this triggers the DLL's `DllMain(DLL_PROCESS_ATTACH)` which reads the env var and opens the process handle. The two components share no direct code; communication is via the env var and the exported function.

## Build

```powershell
# Native DLL (requires VS 2022 with C++ workload)
cd src/DebugHook
cmake -B build -A x64
cmake --build build --config Release

# C# loader
cd src/DebugLoader
dotnet build

# Copy DLL next to loader output
copy src\DebugHook\build\Release\DebugHook.dll src\DebugLoader\bin\Debug\net8.0\
```

If `cmake` is not on PATH, use the VS-bundled copy at:
`C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`

## Run

```powershell
DebugLoader.exe <target_pid>
```

## Conventions

- The C# project uses `LibraryImport` source-generated P/Invoke (not `DllImport`), with `AllowUnsafeBlocks` enabled.
- The C# project targets x64 only (`PlatformTarget` in csproj) to match the native DLL.
- The native DLL uses `extern "C" __declspec(dllexport)` for exported functions to avoid C++ name mangling.
- Debug output from the DLL goes to `OutputDebugStringA` (visible in debuggers like WinDbg/DbgView), not stdout.
