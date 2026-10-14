# TransitGen Specification — 08 Roadmap

Each milestone ends when its acceptance criteria pass in CI (and in a DAW where stated).

| # | Milestone | Deliverables | Acceptance |
|---|---|---|---|
| M0 | Skeleton | CMake, `core/` library, test harness, CI on 3 OSes | CI green; `transitgen_tests` runs |
| M1 | **Real-time engine** (02) | TransportTracker, FillScheduler, CaptureBuffer, PlanPlayer, all source effects, modifiers, crossfades, TestPlanProvider | All 02 tests pass, including block-size invariance, click detection, idle bit-exactness, no allocation; ASan/UBSan clean |
| M2 | Generator (03) + styles (04) | RNG, energy curve, `generate()`, style JSON parser, 6 factory styles | Golden plans identical on 3 OSes; 100k fuzz cases valid; locality tests pass; < 50 µs |
| M3 | JUCE plugin shell | APVTS params (01 §6), state (06 §1), engine + generator wired in, a generic editor | pluginval strictness 10 passes; Auto-Phrase audibly correct in Reaper + Ableton; offline bounce nulls against real-time playback |
| M4 | UI (05) | Full layout, plan view, curve editor, seed bar, undo/redo | 05 §6 acceptance |
| M5 | Presets + polish | Preset browser, 30 factory presets, A/B, tooltips, freeze | 06 §5 tests; manual DAW matrix (07 §3) passes |
| M6 | Beta | Signed/notarized installers, demo mode, licence keys | 20+ beta testers; no crash reports open for 2 weeks |
| M7 | **v1.0 release** | Website/store page, demo video, docs | Release checklist complete |
| M8 | v1.x | Timeline editing, audio + MIDI export, user styles, Delay Throw, Reverb Swell | per-feature specs (to be added as 09+) |
| M9 | v2 | Transient-aware slicing, sidechain, style learning, AAX | per-feature specs |

**Critical path:** M1 → M2 → M3. The engine (M1) carries the most technical risk and is built first.
