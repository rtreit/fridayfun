@echo off
echo Compiling Process Hollowing Test Tool...
echo.

REM Check if Visual Studio tools are available
where cl.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: Visual Studio compiler not found in PATH
    echo Please run this from a Visual Studio Developer Command Prompt
    echo Or install MinGW and use: g++ -o process_hollowing.exe process_hollowing.cpp -static
    pause
    exit /b 1
)

REM Compile with Visual Studio
cl.exe /EHsc process_hollowing.cpp /Fe:process_hollowing.exe

if %ERRORLEVEL% equ 0 (
    echo.
    echo Compilation successful!
    echo Run with: process_hollowing.exe [target_executable_path]
    echo Default target: C:\Windows\System32\notepad.exe
) else (
    echo.
    echo Compilation failed!
)

echo.
pause