# fridayfun — Debug Loader

A lightweight process debugging toolkit. A C# loader executable uses `LoadLibrary` to
inject a native DLL whose `DLL_PROCESS_ATTACH` handler opens a handle to a target process.

## Components

| Project | Language | Description |
|---|---|---|
| `src/DebugLoader` | C# (.NET 8) | CLI that validates the target PID and loads the hook DLL. |
| `src/DebugHook` | C++ (CMake) | Native DLL that opens a debug handle on attach. |

## Build

```
# Build the native DLL
cd src/DebugHook
cmake -B build -A x64
cmake --build build --config Release

# Build the C# loader
cd src/DebugLoader
dotnet build

# Copy the DLL next to the loader
copy src\DebugHook\build\Release\DebugHook.dll src\DebugLoader\bin\Debug\net8.0\
```

## Usage

```
DebugLoader.exe <target_pid>
```

The loader sets `DEBUGHOOK_TARGET_PID` in its own environment, then calls `LoadLibraryW`
on `DebugHook.dll`. The DLL's `DllMain` reads the env var and calls `OpenProcess` with
VM read/write/operation and query rights.

Press Enter to unload the DLL (closes the handle) and exit.
