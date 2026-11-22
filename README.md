# AUREL

A virtual analog synthesizer plugin (VST3), built with C++20 and JUCE.

**Status:** M0 (project foundation) complete. No synthesis or plugin target yet; see the roadmap in [docs/architecture/0001-architecture.md](docs/architecture/0001-architecture.md).

## Quick start (Windows)

```powershell
git submodule update --init --depth 1
./Scripts/setup-python.ps1
./Scripts/build.ps1
./Scripts/test.ps1
```

Details: [docs/development/building.md](docs/development/building.md).

## Layout

| Path | Contents |
|---|---|
| `Source/DSP/` | `aurel_dsp`: JUCE-free DSP core |
| `Tools/Common/` | helpers shared by tests and tools (WAV I/O) |
| `Tools/Render/` | `aurel_render`: offline renderer for measurement |
| `Tests/` | Catch2 suite: unit and real-time-safety tests |
| `Scripts/` | build / test / setup scripts |
| `Scripts/analysis/` | Python measurement harness |
| `docs/` | architecture, development, testing |
| `external/JUCE/` | JUCE 9.0.6 (submodule) |

## Documentation

* [Architecture (ADR 0001)](docs/architecture/0001-architecture.md)
* [Building](docs/development/building.md)
* [Testing and measurement](docs/testing/README.md)
* [Third-party dependencies](docs/development/third-party.md)
