# Building and packaging

## Windows application

Supported build hosts are Windows 10/11 x64. The code uses C++20, Qt Widgets and native Windows APIs.

Required tools:

- Git;
- CMake 3.24 or newer;
- Ninja (for the helper script) or Visual Studio 2022;
- Qt 6.8 or newer with Qt Widgets;
- Visual Studio 2022 C++ tools, or the MinGW toolchain shipped with Qt.

### Helper script

For an MSVC Qt installation:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-windows.ps1 `
  -QtDir C:\Qt\6.8.3\msvc2022_64
```

For Qt's MinGW distribution:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-windows.ps1 `
  -QtDir C:\Qt\6.8.3\mingw_64 `
  -MinGwDir C:\Qt\Tools\mingw1310_64\bin
```

The script configures `build/windows-release`, builds the application and runs tests with the Qt offscreen platform plugin. Use `-SkipTests` only for diagnosis or packaging after an already tested build.

### Manual CMake build

Visual Studio example:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 `
  -DBUILD_TESTING=ON
cmake --build build/windows --config Release --parallel
ctest --test-dir build/windows -C Release --output-on-failure
```

### Portable package

After a successful build:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-portable.ps1 `
  -BuildDir build/windows-release `
  -QtDir C:\Qt\6.8.3\msvc2022_64
```

The output defaults to `artifacts/Trezor-PC-Monitor-portable`. `windeployqt` copies the required Qt runtime. The packaging script also includes licenses and the prebuilt firmware. Optional arguments add the PresentMon MSI and `libusb-1.0.dll` to the package:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-portable.ps1 `
  -QtDir C:\Qt\6.8.3\msvc2022_64 `
  -PresentMonInstaller .\downloads\PresentMon.msi `
  -LibUsbDll .\downloads\libusb-1.0.dll
```

GitHub release builds fetch those two dependencies from their official release repositories. They are not committed to this repository.

## Firmware

Firmware builds are intentionally isolated from the desktop CMake project. Run `scripts/build-firmware.sh` on Linux with Nix and Git. The script reads `UPSTREAM_TREZOR_REF`, clones the official Trezor firmware repository, overlays `firmware/` and `protocol/`, then invokes the legacy build.

See [FIRMWARE.md](FIRMWARE.md) for artifact formats and flashing safety.

## Determinism and versions

- The desktop version is stored in `VERSION` and in the root CMake project.
- The firmware base tree is pinned by a full commit hash in `UPSTREAM_TREZOR_REF`.
- Asset pack compilation is deterministic for identical project contents and converter settings.
- Release tags use `vMAJOR.MINOR.PATCH` and must match `VERSION`.

## CI

`ci.yml` builds and tests the Windows application for every push and pull request. `firmware.yml` provides a manual, pinned firmware build on Linux/Nix. Tags matching `v*` invoke `release.yml`, which tests the app, creates a portable ZIP, adds both firmware image formats, generates SHA-256 checksums and publishes a GitHub release.
