param(
    [string]$BuildDir = "build/windows-release",
    [string]$OutputDir = "artifacts/Trezor-PC-Monitor",
    [string]$QtDir = $env:QT_ROOT_DIR,
    [string]$FirmwareDir = "firmware/prebuilt",
    [string]$PresentMonInstaller = "",
    [string]$LibUsbDll = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$outputPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
$firmwarePath = [IO.Path]::GetFullPath((Join-Path $repoRoot $FirmwareDir))

if (-not (Test-Path -LiteralPath $buildPath)) { throw "Build directory not found: $buildPath" }
if ([string]::IsNullOrWhiteSpace($QtDir)) { throw "Specify -QtDir or set QT_ROOT_DIR" }
$qtPath = [IO.Path]::GetFullPath($QtDir)
$deployQt = Join-Path $qtPath "bin/windeployqt.exe"
if (-not (Test-Path -LiteralPath $deployQt)) { throw "windeployqt.exe not found: $deployQt" }

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
cmake --install $buildPath --prefix $outputPath --config Release
if ($LASTEXITCODE -ne 0) { throw "CMake install failed" }

$executable = Join-Path $outputPath "trezor-pc-monitor.exe"
& $deployQt --release --compiler-runtime --no-translations $executable
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

New-Item -ItemType Directory -Force -Path (Join-Path $outputPath "portable-data/logs") | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") -Destination $outputPath -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "app/fonts/SPLEEN-LICENSE.txt") `
    -Destination (Join-Path $outputPath "Spleen-LICENSE.txt") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "COPYING") `
    -Destination (Join-Path $outputPath "COPYING.txt") -Force

if (Test-Path -LiteralPath (Join-Path $firmwarePath "pcmonitor-inner.bin")) {
    $destination = Join-Path $outputPath "firmware"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -LiteralPath (Join-Path $firmwarePath "pcmonitor-inner.bin") `
        -Destination $destination -Force
}
if (-not [string]::IsNullOrWhiteSpace($PresentMonInstaller) -and
    (Test-Path -LiteralPath $PresentMonInstaller)) {
    $setup = Join-Path $outputPath "setup"
    New-Item -ItemType Directory -Force -Path $setup | Out-Null
    Copy-Item -LiteralPath $PresentMonInstaller -Destination $setup -Force
}
if (-not [string]::IsNullOrWhiteSpace($LibUsbDll) -and
    (Test-Path -LiteralPath $LibUsbDll)) {
    Copy-Item -LiteralPath $LibUsbDll `
        -Destination (Join-Path $outputPath "libusb-1.0.dll") -Force
}

Write-Host "Portable package is ready: $outputPath"
