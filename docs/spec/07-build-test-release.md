# TransitGen Specification — 07 Build, Test, Release

## 1. Toolchain
| Item | Choice |
|---|---|
| Language | C++20 |
| Build | CMake ≥ 3.25, Ninja |
| Framework | JUCE 8.x pinned to an exact tag via `FetchContent` (or a git submodule at `external/JUCE`) |
| Tests | Catch2 v3 (FetchContent) |
| Compilers | MSVC 2022 (Windows), Apple Clang / Xcode 15+ (macOS), GCC 13 / Clang 17+ (Linux) |
| Formats | VST3 (Win/macOS/Linux), AU (macOS), Standalone (all). AAX in v2 (needs a PACE/iLok developer account and signing). CLAP optional later via `clap-juce-extensions`. |
| macOS | Universal binary (arm64 + x86_64), minimum macOS 11 |
| Windows | x64, minimum Windows 10 |
| Plugin IDs | Manufacturer code `Djsx`, plugin code `Trgn`, bundle id `com.djshellshoxxx.transitgen` |

Compiler flags for `core/`: `-ffp-contract=off` (MSVC: `/fp:precise`), never fast-math (03 §2). Warnings are errors in CI.

## 2. CMake targets
| Target | Contents |
|---|---|
| `transitgen_core` | static library, no JUCE |
| `transitgen_tests` | Catch2 tests for core |
| `TransitGen` | `juce_add_plugin` (only when `TG_BUILD_PLUGIN=ON`) → `TransitGen_VST3`, `_AU`, `_Standalone` |
| `transitgen_styles` | `juce_add_binary_data` (or a CMake script) embedding `styles/*.json` and factory presets |

## 3. Test strategy
| Level | What | Where |
|---|---|---|
| Unit | core DSP, generator, transport, styles (02 §test plan, 03 §8, 06 §5) | CI, every push |
| Sanitizers | ASan + UBSan build of the unit tests (Linux, macOS) | CI |
| Determinism | golden plans on all 3 OSes; block-size invariance | CI |
| Plugin validation | **pluginval** strictness 10 on VST3 + AU; `auval -v aufx Trgn Djsx` on macOS | CI |
| Real-time safety | allocation counter in tests; optional RADSan/`-fsanitize=realtime` build (Clang 20+) | CI (nightly) |
| Performance | benchmark: engine at 48 kHz stereo, 64-sample blocks, densest style. Fail if > 2 % of one core on the CI runner baseline. | CI |
| Manual DAW matrix | Ableton Live 12, FL Studio, Logic Pro, Bitwig, Reaper, Cubase, Studio One. Checklist: load, automate every param, Auto-Phrase timing, loop region, tempo automation, offline bounce = real-time playback (null test), state save/reload, resize UI, mono track | each release candidate |

## 4. CI (GitHub Actions)
- Matrix: `windows-latest`, `macos-latest`, `ubuntu-24.04`.
- Steps: configure → build → unit tests → pluginval → upload artifacts.
- Cache FetchContent downloads and use ccache/sccache.
- Release workflow on a tag `v*`: build, sign, notarize, package, and create a GitHub Release with the installers.

## 5. Packaging and signing
| OS | Installer | Signing |
|---|---|---|
| macOS | `.pkg` via `pkgbuild`/`productbuild` (VST3 → `/Library/Audio/Plug-Ins/VST3`, AU → `/Library/Audio/Plug-Ins/Components`, app → `/Applications`) | Developer ID Application + Installer certs (Apple Developer Program $99/yr), `notarytool` + staple |
| Windows | Inno Setup `.exe` (VST3 → `C:\Program Files\Common Files\VST3`) | Authenticode code-signing cert (OV/EV, or Azure Trusted Signing) |
| Linux | `.tar.gz` with VST3 + Standalone (community/beta) | none |

## 6. Licensing / copy protection (v1.0)
- Offline licence keys: a JSON payload (email, product, issued date, edition) signed with **Ed25519**. The public key is embedded in the plugin and verification runs on the message thread at startup.
- Unlicensed = **demo mode**: fully working, but a 1-second dropout every 60 seconds during fills only, plus a banner. No time limit.
- No online activation and no iLok in v1, deliberately: this keeps support low and avoids annoying users.
- The key generator script lives in a **separate private repo**, never in this one.

## 7. Versioning
- SemVer `MAJOR.MINOR.PATCH`. The plugin version is reported to hosts; the state version is separate (06 §1).
- Changelog in `CHANGELOG.md` (Keep a Changelog format).
