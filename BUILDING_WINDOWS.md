# Building on Windows

Step-by-step guide for building **AttributePainter** on Windows with MSVC.

---

## Prerequisites

| Tool | Version | Download |
|---|---|---|
| Visual Studio | 2019 or 2022 | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads/) |
| VS Workload | *Desktop development with C++* | (select during VS install) |
| CMake | 3.20+ | [cmake.org/download](https://cmake.org/download/) — tick **Add to PATH** |
| Nuke | 17.0 | [thefoundry.tv](https://www.foundry.com/products/nuke) |

> **CMake on PATH** — the installer has a checkbox for this. If you forgot,
> add `C:\Program Files\CMake\bin` to your user `PATH` variable manually.

---

## Quick Start

```powershell
# 1. Clone
git clone https://github.com/yourorg/NukeAttributePainter.git
cd NukeAttributePainter

# 2. Check prerequisites (also generates stubs if Nuke isn't installed)
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1

# 3. Build
scripts\build.bat
```

The resulting DLL will be in `build\Release\AttributePainter.dll`.

---

## With a Real Nuke Install

```bat
scripts\build.bat /nuke "C:\Program Files\Nuke17.0"
```

The script auto-detects Visual Studio via `vswhere.exe` and sets up the MSVC
environment before calling CMake.  You do **not** need to run it from a
Developer Command Prompt.

### Install directly to Nuke

```bat
scripts\build.bat /nuke "C:\Program Files\Nuke17.0" /install
```

This copies `AttributePainter.dll` (and the `.pdb` debug symbols) to
`<NUKE_ROOT>\plugins\`.

---

## Stub Build (no Nuke install needed)

Useful for compile-checking on a CI machine or a workstation without Nuke.

```powershell
# Generate minimal SDK stubs, then build against them
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -Stubs -Build
```

The plugin will compile and link but **cannot be loaded into Nuke** because the
stub `DDImage.dll` is empty. Use this mode only for syntax/warning checking.

---

## Build Options

```
scripts\build.bat [options]

  /nuke  <dir>   Nuke install path  (default: C:\Program Files\Nuke17.0)
  /usd   <dir>   USD root           (default: <nuke>\usd)
  /type  <type>  Release | Debug | RelWithDebInfo  (default: Release)
  /arch  <arch>  x64 | x86          (default: x64)
  /jobs  <n>     Parallel job count (default: %NUMBER_OF_PROCESSORS%)
  /install       Copy .dll to Nuke plugins folder after build
  /clean         Delete build\ directory before configuring
  /help          Show this message
```

---

## Manual CMake (if you prefer)

```bat
:: Open a Developer Command Prompt for VS 2022 (or run vcvarsall.bat x64 first)

cmake -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DNUKE_ROOT="C:\Program Files\Nuke17.0" ^
    -DUSD_ROOT="C:\Program Files\Nuke17.0\usd"

cmake --build build --config Release
```

---

## Troubleshooting

### `DDImage.lib not found`
Nuke 17 ships `DDImage.lib` in the root of the install folder — the same
directory as `Nuke.exe`. Make sure `-DNUKE_ROOT` points there:
```
C:\Program Files\Nuke17.0\
  Nuke.exe
  DDImage.lib          ← here
  DDImage.dll
  include\DDImage\Op.h ← and NDK headers here
```
If you only have the Nuke executable (no `.lib`), you need the **Nuke NDK**,
which is a separate download from The Foundry's customer portal.

### `vswhere.exe not found`
`vswhere.exe` is installed with Visual Studio 2017+. If it's missing, your VS
installation may be corrupted.  Re-run the VS installer and repair.

### `Cannot open include file: 'GL/glew.h'`
Nuke ships its own GLEW in `<NUKE_ROOT>\include\GL\glew.h`. If you're
building against real Nuke, this should resolve automatically. If building
with stubs, run `setup.ps1 -Stubs` to regenerate them.

### MSVC `C4251` / `C4275` warnings
These are expected when mixing DLL boundaries with STL types in the Nuke NDK.
They are suppressed via `/wd4251 /wd4275` in `CMakeLists.txt`.

### Plugin loads but crashes
1. Check you built with the **same** VS runtime as Nuke.
   Nuke 17 uses the VS 2022 runtime (`VCRUNTIME140.dll`).
2. Open the `.dll` in **Dependency Walker** or `dumpbin /dependents` to check
   for missing runtime DLLs.
3. Check the Nuke terminal / `~/.nuke/nuke_crash_*` log for the exact error.

---

## CI / GitHub Actions

The project uses **GitHub Actions** for CI. Windows builds run on
`windows-2022` hosted runners and use the PowerShell stub generator so no
real SDK install is required.

See `.github/workflows/ci.yml` for the full pipeline.
