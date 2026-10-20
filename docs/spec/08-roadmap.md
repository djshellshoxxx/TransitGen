# TransitGen Specification — 08 Roadmap

Each milestone ends when its acceptance criteria pass in CI (and in a DAW where stated).

| # | Milestone | Status and deliverables | Acceptance |
|---|---|---|---|
| M0 | Skeleton | Implemented: CMake, `core/`, tests, and a Windows/macOS/Linux CI workflow. | CI must pass on all three runners; local execution remains to be recorded. |
| M1 | **Real-time engine** (02) | Implemented: transport, scheduling, capture, plan playback, source effects, modifiers and crossfades. | Verify all 02 tests, block-size invariance, click detection, exact idle output, allocation checks, and sanitizers in CI. |
| M2 | Generator (03) + styles (04) | Implemented: deterministic generator, energy curves, style JSON parser and six factory styles. | Verify golden plans, fuzz, locality, and timing gates in CI; do not treat PR-description results as this branch's current CI evidence. |
| M3 | JUCE plugin shell | Implemented: APVTS parameters, host state, engine wiring and generic editor. Host/format validation is pending. | pluginval strictness 10, DAW trigger behavior, automation/state round trip and real-time/offline comparison. |
| M4 | UI (05) | In progress: shared CDL palette tokens, resizable editor and editable energy-curve view. Plan lanes, full layout, undo/redo, preview and accessibility work remain. | Complete the 05 interaction and accessibility criteria; measure the stated UI performance gates. |
| M5 | Presets + polish | Not started: shared baseline requires at least 10 factory presets, user preset import/export, Options, Help, and applicable common controls. Project spec 06 calls for 30 presets and a `.tgpreset` format; reconcile its XML format with the shared JSON requirement before implementation. | Preset/state validation, all preset generation tests and manual DAW matrix (07 §3). |
| M6 | Beta | Signed/notarized installers, demo mode, licence keys | 20+ beta testers; no crash reports open for 2 weeks |
| M7 | **v1.0 release** | Website/store page, demo video, docs | Release checklist complete |
| M8 | v1.x | Timeline editing, audio + MIDI export, user styles, Delay Throw, Reverb Swell | per-feature specs (to be added as 09+) |
| M9 | v2 | Transient-aware slicing, sidechain, style learning, AAX | per-feature specs |

**Current path:** M4 curve editing → plan visualization and editor workflow → M5 preset/state and shared baseline work → M3 host validation. Keep the verified M1/M2 core independent from JUCE UI changes.
