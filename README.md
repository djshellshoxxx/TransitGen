# TransitGen

A glitch fill and transition generator plugin (VST3 / AU / Standalone).

Draw an energy curve, pick a genre style and a length, and TransitGen generates a tempo-synced glitch fill or transition. Each fill has a seed, so you can lock it, re-roll it and get the same result on every playback.

## Build

Requirements: CMake ≥ 3.28, a C++20 compiler (GCC 13, Clang 17+, MSVC 2022, Xcode 15+). JUCE 8 and Catch2 are fetched by CMake.

```sh
# Core library + unit tests only (no JUCE)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build

# Plugin: VST3 + Standalone (+ AU on macOS), plus the headless plugin tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTG_BUILD_PLUGIN=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release
```

The plugins end up in `build/plugin/TransitGen_artefacts/Release/`. On Linux, JUCE needs:
`libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev` (curl and the web browser are disabled).

Validation: `pluginval --strictness-level 10 --validate-in-process --skip-gui-tests build/plugin/TransitGen_artefacts/Release/VST3/TransitGen.vst3` (run it under `xvfb-run` on a headless Linux machine).
