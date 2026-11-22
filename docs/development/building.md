# Building AUREL

## Requirements

| Tool | Version used | Notes |
|---|---|---|
| Windows | 11 | |
| Visual Studio 2022 | 17.13 (MSVC 19.43) | Workload *Desktop development with C++*. Community edition is fine. |
| CMake | ≥ 3.22 (4.3.1 used) | Must be on `PATH`. |
| Git | any recent | JUCE is a submodule. |
| Python | 3.11+ | Measurement harness only. |

No Developer Command Prompt is needed: the *Visual Studio 17 2022* generator locates MSVC itself.

## First-time setup

```powershell
git clone <repo> aurel
cd aurel
git submodule update --init --depth 1   # JUCE 9.0.6
./Scripts/setup-python.ps1              # creates .venv with pinned packages
```

The first configure fetches Catch2 v3.16.1 (network required once) and builds JUCE's `juceaide` helper, which takes about 3 minutes. Later configures are fast.

## Build and test

```powershell
./Scripts/build.ps1                    # Debug, full (JUCE + tests + tools)
./Scripts/build.ps1 -Config Release
./Scripts/test.ps1                     # CTest (Catch2) + Python measurement tests
./Scripts/test.ps1 -Config Release
```

Fast loop for DSP work (skips JUCE entirely):

```powershell
./Scripts/build.ps1 -Core
./Scripts/test.ps1 -Core
```

Raw CMake equivalents:

```powershell
cmake --preset vs2022
cmake --build --preset debug        # or: release
ctest --preset debug
.venv/Scripts/python.exe -m pytest Scripts/analysis
```

## Presets

| Configure preset | Build dir | Contents |
|---|---|---|
| `vs2022` | `build/vs2022` | Everything (JUCE, plugin from M1, tests, tools) |
| `vs2022-core` | `build/vs2022-core` | DSP core, tests, tools. No JUCE |

## VS Code

Install the recommended extensions (CMake Tools, C/C++). CMake Tools picks up `CMakePresets.json` automatically; select the `vs2022` configure preset and a build preset from the status bar.

## Build settings worth knowing

* **Static MSVC runtime** (`/MT`, `/MTd`): binaries depend only on `KERNEL32.dll`, so users need no VC++ redistributable. Verified with `dumpbin /dependents`.
* **Warnings are errors** for AUREL's own targets (`/W4 /WX`). JUCE and Catch2 are not affected. Disable temporarily with `-DAUREL_WARNINGS_AS_ERRORS=OFF`.
* **`/fp:precise`** (MSVC default) is deliberate: `/fp:fast` allows the compiler to remove NaN checks.
