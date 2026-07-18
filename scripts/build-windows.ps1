param(
    [string]$QtDir = $env:QT_ROOT_DIR,
    [string]$BuildDir = "build/windows-release",
    [string]$Configuration = "Release",
    [string]$MinGwDir = "",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))

if ([string]::IsNullOrWhiteSpace($QtDir)) {
    throw "Specify -QtDir or set QT_ROOT_DIR (for example C:\Qt\6.8.3\msvc2022_64)."
}
$qtPath = [IO.Path]::GetFullPath($QtDir)
if (-not (Test-Path -LiteralPath (Join-Path $qtPath "bin/Qt6Core.dll"))) {
    throw "Qt 6 was not found at $qtPath"
}

if (-not [string]::IsNullOrWhiteSpace($MinGwDir)) {
    $mingwPath = [IO.Path]::GetFullPath($MinGwDir)
    $env:Path = "$mingwPath;$env:Path"
}
$env:Path = "$(Join-Path $qtPath 'bin');$env:Path"

$configure = @(
    "-S", $repoRoot,
    "-B", $buildPath,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DBUILD_TESTING=ON",
    "-DCMAKE_PREFIX_PATH=$qtPath"
)
if (-not [string]::IsNullOrWhiteSpace($MinGwDir)) {
    $configure += "-DCMAKE_C_COMPILER=$(Join-Path $mingwPath 'gcc.exe')"
    $configure += "-DCMAKE_CXX_COMPILER=$(Join-Path $mingwPath 'g++.exe')"
}

cmake @configure
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build $buildPath --parallel --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if (-not $SkipTests) {
    $env:QT_QPA_PLATFORM = "offscreen"
    ctest --test-dir $buildPath --build-config $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
}

Write-Host "Windows build is ready: $buildPath"
