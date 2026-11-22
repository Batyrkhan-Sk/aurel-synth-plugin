# Third-party dependencies

Policy: AUREL may become a commercial closed-source product. No GPL/AGPL code may be added, except JUCE itself, whose licence path (AGPLv3 or commercial JUCE 9 EULA) is chosen at release time. Every dependency is listed here with its licence before it is merged.

| Dependency | Version | How it is included | Licence | Shipped in plugin? |
|---|---|---|---|---|
| JUCE | 9.0.6 | git submodule `external/JUCE` (shallow) | AGPLv3 / commercial JUCE 9 EULA | Yes (from M1) |
| Catch2 | v3.16.1 | CMake FetchContent (tests only) | BSL-1.0 | No |
| NumPy | 2.4.6 | pip, `.venv` (analysis only) | BSD-3-Clause | No |
| SciPy | 1.17.1 | pip, `.venv` (analysis only) | BSD-3-Clause | No |
| Matplotlib | 3.11.2 | pip, `.venv` (analysis only) | PSF-based (Matplotlib licence) | No |
| pytest | 9.2.0 | pip, `.venv` (analysis only) | MIT | No |
| pluginval | 1.0.4 | Prebuilt binary downloaded by `Scripts/pluginval.ps1` (SHA-256 pinned) into `build/tools/` | GPLv3 | No. A separate validation tool: never linked or distributed |

JUCE vendors its own dependencies, including the VST3 SDK; its inventory is `external/JUCE/JUCE.spdx.json`.
