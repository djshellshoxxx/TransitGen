# TransitGen Specification — 00 Overview

**Status:** Draft v0.1 · **Owner:** djshellshoxxx · **Date:** 2026-10-01

## 1. Product summary
TransitGen is an audio effect plugin (VST3 / AU / Standalone) that **generates tempo-synced glitch fills and transitions**. You choose *where* a fill happens (trigger), *how long* it is (length), *how it develops* (energy curve) and *what it sounds like* (genre style). TransitGen generates a **fill plan** (a list of timed effect events) from those inputs plus a **seed**, then plays that plan on the incoming audio, sample-accurately.

## 2. Design principles (apply to every spec)
1. **Deterministic.** The same inputs + seed always give bit-identical output, in real time or offline, at any buffer size.
2. **Transparent when idle.** Outside a fill, output = input, bit-exact.
3. **Musical by default.** Every factory style + the default curve must sound usable with no tweaking.
4. **Real-time safe.** No allocation, locks, I/O or unbounded loops on the audio thread.
5. **Zero latency** in v1.
6. **The core is independent of JUCE.** All DSP and generation code lives in `core/` (plain C++20, no JUCE), so it can be unit-tested headless. JUCE only wraps it.

## 3. Spec index
| # | Document | Scope |
|---|---|---|
| 00 | Overview (this) | Summary, principles, glossary, scope |
| 01 | [Core contract](01-core-contract.md) | Architecture, timing conventions, FillPlan data model, parameter list |
| 02 | [Real-time fill engine](02-fill-engine.md) | **Hardest part.** Transport tracking, scheduling, capture buffer, plan playback, all effect DSP, click-free transitions |
| 03 | [Fill generator](03-fill-generator.md) | Seeded RNG, energy curve, generation algorithm |
| 04 | [Genre styles](04-genre-styles.md) | Style file format + 6 factory styles |
| 05 | [UI / UX](05-ui.md) | Layout, controls, interactions, visuals |
| 06 | [State, presets, export](06-state-presets-export.md) | Serialization, versioning, presets, fill export |
| 07 | [Build, test, release](07-build-test-release.md) | CMake/JUCE, CI, test strategy, DAW matrix, packaging, licensing |
| 08 | [Roadmap](08-roadmap.md) | Milestones with acceptance criteria |

## 4. Glossary
| Term | Meaning |
|---|---|
| **Beat** | One quarter note (= 1.0 PPQ). All musical time in TransitGen is in beats. |
| **PPQ** | The host's position in quarter notes since song start. |
| **Fill** | A time region `[startBeat, startBeat + lengthBeats)` during which a FillPlan plays. |
| **FillPlan** | An immutable list of timed events + automation lanes that describe one fill. |
| **Source event** | An event that decides *what audio* is heard (pass, stutter, reverse, tape, silence). Exactly one is active at any time during a fill. |
| **Modifier** | Processing applied after the source (gate, filter, crush). |
| **Energy curve** | User-drawn function `E(t)`, t ∈ [0,1] across the fill, giving values 0..1 that drive generation. |
| **Style** | A data file of rules that turns energy into events. |
| **Seed** | A 32-bit integer that makes generation reproducible. |

## 5. Scope
**v1.0 (MVP):** three trigger modes, 6 styles, source effects (Pass, Stutter, Reverse, TapeStop, TapeStart, Silence), modifiers (Gate, Filter, Crush), energy curve editor, seed / re-roll / lock / history, plan view, presets, stereo/mono.
**v1.x:** timeline editing (freezes the plan), audio + MIDI export, user styles, Delay Throw, Reverb Swell, fill history browser.
**v2:** transient-aware slicing, sidechain, style learning, AAX.
**Out of scope:** an instrument/synth mode, video sync, a mobile version.
