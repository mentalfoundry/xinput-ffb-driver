#Requires -Version 5.1
<#
.SYNOPSIS
  Build script for Force Feedback Driver for XInput.
  Compiles x86 + x64 DLLs via MSBuild, then packages them into an MSI via WiX 7.

.OUTPUTS
  dist\xiffd-setup.msi
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = $PSScriptRoot

function Write-Step { param([string]$Msg) Write-Host "`n=== $Msg ===" -ForegroundColor Cyan }
function Fail       { param([string]$Msg) Write-Host "FAILED: $Msg" -ForegroundColor Red; exit 1 }

# ── 1. Locate MSBuild ────────────────────────────────────────────────────────
Write-Step "Locating MSBuild"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found. Install Visual Studio." }

$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { Fail "No Visual Studio installation with MSBuild found." }

$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) { Fail "MSBuild.exe not found at: $msbuild" }
Write-Host "MSBuild: $msbuild"

# ── 2. Locate WiX ───────────────────────────────────────────────────────────
Write-Step "Locating WiX"
$wixCmd = Get-Command wix -ErrorAction SilentlyContinue
$wix = if ($wixCmd) { $wixCmd.Source } else { $null }
if (-not $wix) {
    $wix = Get-ChildItem 'C:\Program Files\WiX Toolset*\bin\wix.exe' -ErrorAction SilentlyContinue |
           Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $wix) { Fail "wix.exe not found. Install WiX Toolset v7+." }
Write-Host "WiX: $wix"

# ── 3. Build x86 DLL ────────────────────────────────────────────────────────
Write-Step "Building x86 (Win32) DLL"
& $msbuild "$Root\xiffd.sln" /p:Configuration=Release /p:Platform=Win32 /m /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) { Fail "x86 build failed." }

$x86dll = "$Root\build\Release\Win32\xiffd.dll"
if (-not (Test-Path $x86dll)) { Fail "Expected x86 output not found: $x86dll" }
Write-Host "x86 DLL: $x86dll"

# ── 4. Build x64 DLL ────────────────────────────────────────────────────────
Write-Step "Building x64 DLL"
& $msbuild "$Root\xiffd.sln" /p:Configuration=Release /p:Platform=x64 /m /nologo /verbosity:minimal
if ($LASTEXITCODE -ne 0) { Fail "x64 build failed." }

$x64dll = "$Root\build\Release\x64\xiffd.dll"
if (-not (Test-Path $x64dll)) { Fail "Expected x64 output not found: $x64dll" }
Write-Host "x64 DLL: $x64dll"

# ── 5. Ensure WiX UI extension is available ──────────────────────────────────
Write-Step "Checking WiX UI extension"
$extList = & $wix extension list 2>&1 | Out-String
if ($extList -notmatch 'WixToolset\.UI\.wixext') {
    Write-Host "Installing WixToolset.UI.wixext..."
    & $wix extension add WixToolset.UI.wixext
    if ($LASTEXITCODE -ne 0) { Fail "Failed to install WixToolset.UI.wixext." }
} else {
    Write-Host "WixToolset.UI.wixext already installed."
}

# ── 6. Build MSI ─────────────────────────────────────────────────────────────
Write-Step "Building installer (MSI)"
$distDir = "$Root\dist"
if (-not (Test-Path $distDir)) { New-Item -ItemType Directory -Path $distDir | Out-Null }

$msiOut = "$distDir\xiffd-setup.msi"

Push-Location "$Root\xiwix"
try {
    & $wix build xi.wxs `
        -ext WixToolset.UI.wixext `
        -culture en-US `
        -loc xi.en.wxl `
        -arch x64 `
        -out $msiOut
    if ($LASTEXITCODE -ne 0) { Fail "WiX build failed." }
} finally {
    Pop-Location
}

if (-not (Test-Path $msiOut)) { Fail "MSI not produced at: $msiOut" }

Write-Step "Build complete"
Write-Host "Installer: $msiOut" -ForegroundColor Green
