# ──────────────────────────────────────────────────────────────
# build_installer.ps1  –  Build RDM_X + package into installer
# ──────────────────────────────────────────────────────────────
# Usage:  .\installer\build_installer.ps1
# Requires: CMake (standalone or via Visual Studio), .NET 8 SDK, Inno Setup 6
# ──────────────────────────────────────────────────────────────

param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot   # repo root (one level up from installer/)

# ── Helper: find an executable ────────────────────────────────
function Find-Tool {
    param([string]$Name, [string[]]$SearchPaths)

    # Check PATH first
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # Check known locations
    foreach ($p in $SearchPaths) {
        # Support wildcard paths (e.g. for VS version numbers)
        $found = Get-ChildItem $p -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  RDM_X Installer Build" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ── 1. Build native C++ DLL via CMake ─────────────────────────
Write-Host "[1/3] Building native C++ core (rdm_x_core.dll)..." -ForegroundColor Yellow

$Cmake = Find-Tool "cmake.exe" @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if (-not $Cmake) {
    Write-Error "FAILED: cmake not found. Install CMake or Visual Studio with C++ CMake tools."
    exit 1
}
Write-Host "  Using cmake: $Cmake" -ForegroundColor DarkGray

$BuildDir = Join-Path $Root "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Push-Location $BuildDir
try {
    & $Cmake .. -A Win32
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    & $Cmake --build . --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
}
finally {
    Pop-Location
}

$NativeDll = Join-Path $BuildDir "$Configuration\rdm_x_core.dll"
if (-not (Test-Path $NativeDll)) {
    Write-Error "FAILED: rdm_x_core.dll not found at $NativeDll"
    exit 1
}
Write-Host "  -> rdm_x_core.dll built successfully" -ForegroundColor Green

# ── 2. Publish WPF application ────────────────────────────────
Write-Host "[2/3] Publishing WPF application..." -ForegroundColor Yellow

$WpfDir = Join-Path $Root "wpf"
Push-Location $WpfDir
try {
    dotnet publish -c $Configuration -r win-x86 --no-self-contained -o "bin\$Configuration\net8.0-windows\publish"
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }
}
finally {
    Pop-Location
}

$PublishDir = Join-Path $WpfDir "bin\$Configuration\net8.0-windows\publish"
$AppExe = Join-Path $PublishDir "RDM_X.exe"
if (-not (Test-Path $AppExe)) {
    Write-Error "FAILED: RDM_X.exe not found at $AppExe"
    exit 1
}
Write-Host "  -> WPF app published successfully" -ForegroundColor Green

# ── 3. Compile Inno Setup installer ──────────────────────────
Write-Host "[3/3] Compiling Inno Setup installer..." -ForegroundColor Yellow

$Iscc = Find-Tool "iscc.exe" @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
)
if (-not $Iscc) {
    Write-Error "FAILED: Inno Setup compiler (ISCC.exe) not found. Install from https://jrsoftware.org/isdl.php"
    exit 1
}
Write-Host "  Using ISCC: $Iscc" -ForegroundColor DarkGray

$IssFile = Join-Path $Root "installer\RDM_X_Setup.iss"
& $Iscc $IssFile
if ($LASTEXITCODE -ne 0) {
    Write-Error "FAILED: Inno Setup compilation failed with exit code $LASTEXITCODE"
    exit 1
}

Write-Host "" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "  BUILD COMPLETE" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

$OutputExe = Join-Path $Root "installer\output\RDM_X_Setup_1.0.0.exe"
if (Test-Path $OutputExe) {
    $Size = [math]::Round((Get-Item $OutputExe).Length / 1MB, 2)
    Write-Host "  Installer: $OutputExe ($Size MB)" -ForegroundColor Green
}
else {
    Write-Host "  Installer output directory: $(Join-Path $Root 'installer\output')" -ForegroundColor Green
}
