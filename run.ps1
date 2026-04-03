<#
.SYNOPSIS
    Builds all components and launches the debug loader against a target process.

.PARAMETER TargetPid
    PID of the process to attach to. If omitted, launches TargetApp automatically.
#>
param(
    [int]$TargetPid = 0
)

$ErrorActionPreference = "Continue"
$repoRoot = $PSScriptRoot

# --- Locate CMake ---
$cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $cmake) {
    $cmake = Join-Path "${env:ProgramFiles}" "Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (-not (Test-Path $cmake)) {
        # Try Community edition
        $cmake = Join-Path "${env:ProgramFiles}" "Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
    if (-not (Test-Path $cmake)) {
        Write-Error "CMake not found. Install it or add it to PATH."
        exit 1
    }
}
Write-Host "Using CMake: $cmake" -ForegroundColor Cyan

# --- Build DebugHook DLL ---
Write-Host "`n=== Building DebugHook ===" -ForegroundColor Green
$hookDir = Join-Path $repoRoot "src\DebugHook"
& $cmake -B "$hookDir\build" -S $hookDir -A x64 2>&1 | Out-Null
& $cmake --build "$hookDir\build" --config Release 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error "DebugHook build failed."; exit 1 }
Write-Host "  OK"

# --- Build DebugLoader ---
Write-Host "`n=== Building DebugLoader ===" -ForegroundColor Green
$loaderDir = Join-Path $repoRoot "src\DebugLoader"
$buildOutput = dotnet build $loaderDir --nologo -v q 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host $buildOutput; Write-Error "DebugLoader build failed."; exit 1 }
Write-Host "  OK"

# --- Copy DLL next to loader ---
$dllSrc = Join-Path $hookDir "build\Release\DebugHook.dll"
$loaderBin = Join-Path $loaderDir "bin\Debug\net8.0"
Copy-Item $dllSrc $loaderBin -Force
Write-Host "  Copied DebugHook.dll -> $loaderBin"

# --- Build TargetApp (needed if we auto-launch) ---
$targetDir = Join-Path $repoRoot "src\TargetApp"
$targetExe = Join-Path $targetDir "build\Release\TargetApp.exe"

if ($TargetPid -eq 0) {
    Write-Host "`n=== Building TargetApp ===" -ForegroundColor Green
    & $cmake -B "$targetDir\build" -S $targetDir -A x64 2>&1 | Out-Null
    & $cmake --build "$targetDir\build" --config Release 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "TargetApp build failed."; exit 1 }
    Write-Host "  OK"

    Write-Host "`n=== Launching TargetApp ===" -ForegroundColor Green
    $targetProc = Start-Process $targetExe -PassThru
    Start-Sleep -Seconds 1
    $TargetPid = $targetProc.Id
    Write-Host "  TargetApp running (PID $TargetPid)"
}

# --- Launch DebugLoader ---
Write-Host "`n=== Attaching DebugLoader to PID $TargetPid ===" -ForegroundColor Green
$loaderExe = Join-Path $loaderBin "DebugLoader.exe"
& $loaderExe $TargetPid

# --- Cleanup auto-launched target ---
if ($targetProc -and -not $targetProc.HasExited) {
    Write-Host "`nStopping TargetApp (PID $($targetProc.Id))..."
    $targetProc.Kill()
}
