# TransitGen — Market and Technical Research

_Research date: 2026-10-01_

## 1. The idea in one sentence

A plugin that knows **where you are in the song** (from the DAW's transport) and **generates a complete glitch fill or transition** for a chosen length (1 beat to 8 bars). An energy curve you draw and a genre style shape the fill, and you can lock it, edit it and export it.

---

## 2. Competitor landscape

| Product | Price | What it does well | Gap our plugin fills |
|---|---|---|---|
| **iZotope Stutter Edit 2** | ~$99–199 | Hundreds of "Gestures" (stutter + filter + crush + delay combos) triggered by MIDI notes; low CPU; good preset library. | Built for live MIDI-triggered performance. Reviewers say the Curve Editor "takes significant time to master" and presets can't be saved per module. One KVR user reports the recorded result sounded different from the live take. No generation and no genre/energy-curve logic. Hasn't had a major update since 2019. |
| **Baby Audio Transit 2** (with Andrew Huang) | $129 or $14.99/mo rent-to-own | The current market leader for transitions: 28 effect modules, 7 in series, 6 motion modes (Macro, LFO, Follower, Sidechain, Gate, Sequencer), 830+ presets. | It's a **macro knob over an effect chain**. You still design and automate the transition yourself. Not built around stutter/beat-repeat slicing, no length or position awareness ("make me a 2-bar fill here"), no generation. |
| **Sugar Bytes Effectrix 2** | ~$99 | 32-step grid with 14 effects (loopers, granular, vinyl, crusher, etc.). | Manual step painting. No energy curve, no fill length awareness, no generation. |
| **Cableguys ShaperBox 3 + "Builds & Risers" pack** | ~$99–179 | 86 editable build/riser presets; curves you can edit. | Volume/filter/pan shaping, not glitch slicing. Presets are static; nothing is generated. |
| **Image-Line Gross Beat** | ~$99 | Time and volume envelopes for stutters, tape-stops and half-time. | Manual envelope drawing; aimed mainly at FL Studio users. |
| **Illformed Glitch 2** | **Now free** | Classic random step-glitch. | Random each time, not reproducible, no transition logic. |
| **Augmented Signals "Glitch in Time"** | Free | Six glitch engines (Stutter, Crush, Modulator, Gater, Stretcher, Scatter). | Same as above: an effect, not a generator. |
| **Glitchmachines Fracture** | Free | One-click randomise. | Random only. |

### What this tells us

1. **Basic glitch effects are now free** (Glitch 2, Glitch in Time), so a "glitch effect" alone won't sell.
2. **Transit 2 proves people pay $129 for transition tools specifically**, but it still makes you design the transition yourself.
3. **Nobody generates a finished fill from intent** ("2 bars, DnB, rising chaos, cut to silence before the drop"). That's the gap.
4. **Reproducibility and export are a recurring pain point** on KVR forums (random results that can't be recreated, recorded results that differ from live).

---

## 3. Positioning

> "Transit makes you design transitions. Stutter Edit makes you perform them. **TransitGen writes them for you**, and lets you edit, lock and export the result."

**Who it's for (in priority order):**
1. Bedroom and semi-pro electronic producers (EDM, DnB, dubstep, trap, hyperpop, techno) who make lots of tracks and find transitions a chore.
2. Beat makers and loop-based producers who need variation every 8–16 bars.
3. Content and game audio creators who need quick stingers and transitions.

**Suggested price:** $49–79 at launch (undercuts Transit 2 and Stutter Edit 2), with an intro discount. Consider a free "Lite" version for marketing.

---

## 4. Core feature set

### MVP (version 1.0)
1. **Transport sync:** reads tempo, time signature and bar position from the DAW.
2. **How a fill gets triggered (3 ways):**
   - **Auto-phrase mode:** "fire a fill in the last N beats of every 8/16/32 bars". You get variation with zero automation.
   - **Trigger parameter:** one automatable on/off parameter. Draw a block in your automation lane and the fill fills that space.
   - **MIDI note trigger:** for live use or Stutter Edit-style workflows.
3. **Fill length:** 1/4 beat to 8 bars.
4. **Energy curve editor:** a single drawable curve (0–100% intensity over the fill's length) plus preset shapes (ramp up, ramp down, build-then-cut, pulse, chaos).
5. **Genre style engine:** each style is a set of rules that map energy to effects. Examples:
   - *Dubstep:* 1/8 → 1/16 → 1/32 stutter ramp, low-pass sweep up, tape-stop, 1 beat of silence before the drop.
   - *DnB:* fast 1/16 and 1/32 rolls, reverse snare, high-pass build.
   - *Trap:* 1/12 and 1/24 triplet rolls (hi-hat style), pitch-down stutters.
   - *Hyperpop:* bitcrush, pitch-up stutters, extreme gates.
   - *Techno:* subtle gate patterns, delay throws, filter.
6. **Effect engine (v1):** buffer stutter/repeat (with pitch), reverse, tape-stop/speed-up, gate, filter (LP/HP/BP with resonance), bitcrush/sample-rate reduce, silence/dropout.
7. **Seeded randomness:** every generated fill gets a seed number. Same seed = identical fill every time. Buttons for "Re-roll" and "Lock".
8. **Wet/dry mix and output gain.**

### Version 1.x
- Timeline editor: see the generated events as blocks and drag, delete or change them.
- **Export** the fill as MIDI or as a drag-and-drop audio file.
- User-saveable genre styles.
- Delay throw and reverb swell modules.
- Fill history (step back through previous rolls).

### Version 2.0 (stretch)
- Transient detection so stutters lock onto the kick/snare instead of a rigid grid.
- "Learn" a style from a reference fill.
- Sidechain input to react to another track.

---

## 5. Technical research

### Framework choice

| Option | Pros | Cons |
|---|---|---|
| **JUCE 8 (C++)** — *recommended* | Industry standard. Builds VST3, **AU (needed for Logic Pro)**, AAX and standalone from one codebase. Huge forum and tutorial base. **Free "Starter" licence up to $20k/yr revenue, no splash screen required.** | C++ is harder than Rust. Paid licence once you pass $20k revenue. |
| **nih-plug (Rust)** | Modern, safe; VST3 + CLAP; ISC licence for the framework. | **No AU support**, so no Logic users. Its VST3 bindings are **GPLv3**, which is a problem for a closed-source commercial plugin (CLAP-only avoids this but has a small market). |

**Recommendation: JUCE 8, built with CMake.** Logic support alone decides it for a commercial plugin.

### Key DSP and engineering notes

1. **Transport position:** in JUCE, call `getPlayHead()->getPosition()` inside `processBlock()` to read `ppqPosition` (position in quarter notes), BPM and time signature. Assume tempo is constant within one block, and convert PPQ to sample offsets for sample-accurate event timing.
2. **Capture buffer:** stutters repeat audio that has *already happened*. Keep a rolling circular buffer of the last ~2 bars (at 60 BPM, 2 bars of 4/4 = 8 s → about 768k samples per channel at 96 kHz; cheap).
3. **Reverse effects** need audio from the past, so play the buffer backwards from the trigger point. No added latency needed.
4. **Click-free slicing:** apply short (1–5 ms) fades at every slice boundary. This is the #1 quality issue in cheap stutter plugins.
5. **Deterministic randomness:** use our own seeded RNG (for example xorshift or PCG), never `rand()`. Generate the whole fill's event list *when it is triggered* (not sample by sample), so the same seed always gives the same result, even on offline bounce.
6. **Offline render safety:** some DAWs render faster than real time or in different block sizes. Event timing must depend only on PPQ position, not on wall-clock time or block count.
7. **Thread safety:** generate fills on the audio thread only with pre-allocated memory (no `new`/`malloc` in `processBlock`), or generate on the message thread and hand over via a lock-free FIFO.

### Architecture sketch

```
DAW transport ──► PositionTracker ──► FillScheduler (auto-phrase / param / MIDI)
                                            │
                                            ▼
                   EnergyCurve + GenreStyle + Seed ──► FillGenerator
                                            │   (produces an event list:
                                            │    time, effect, params)
                                            ▼
Audio in ──► CaptureBuffer ──► EffectEngine (stutter, reverse, tape, gate,
                                            filter, crush, silence)
                                            │
                                            ▼
                                       Wet/Dry ──► Audio out
```

---

## 6. Risks

| Risk | Mitigation |
|---|---|
| Transit 2 adds a "generate" feature | Move fast; make reproducibility, export and genre intelligence the core identity, not an add-on. |
| Generated fills sound generic or samey | Invest in the genre rule sets plus seeded variation; ship with styles made by working producers. |
| DAW compatibility bugs (transport quirks in FL Studio, Ableton, Logic) | Test in all major DAWs early; JUCE's AudioPluginHost for quick tests; pluginval for automated validation. |
| Free glitch plugins make the market price-sensitive | Price at $49–79 and sell on time saved, not on effects. |

---

## 7. Suggested next steps

1. Create the repo with a JUCE 8 + CMake project skeleton (VST3 + AU + Standalone targets).
2. Build the **capture buffer + stutter + transport sync** first and test it in a DAW. This is the riskiest part.
3. Add the event-list `FillGenerator` with seeded RNG and one genre style (dubstep is the easiest to hear).
4. Add the energy curve UI.
5. Expand to the other effects and styles.

---

## Sources
- [Baby Audio Transit 2 — Sound On Sound news](https://www.soundonsound.com/news/baby-audio-release-transit-2)
- [Baby Audio Transit 2 — Bonedo review](https://www.bonedo.de/artikel/baby-audio-transit-2-test)
- [Transit 2 pricing — Equipboard](https://equipboard.com/items/baby-audio-transit-2)
- [Transit 2 alternatives — Rysup Audio](https://rysupaudio.com/blogs/news/baby-audio-transit-2-alternatives)
- [iZotope Stutter Edit 2 — Production Expert](https://www.pro-tools-expert.com/production-expert-1/2020/6/14/izotope-stutter-edit-2)
- [iZotope Stutter Edit 2 — Bonedo review](https://www.bonedo.de/artikel/izotope-stutter-edit-2-test/)
- [Stutter Edit alternatives — KVR forum](https://www.kvraudio.com/forum/viewtopic.php?t=507454)
- [Stutters: plugin vs manual editing — KVR forum](https://www.kvraudio.com/forum/viewtopic.php?t=539285&start=15)
- [Stutter Edit 2 vs Glitch2, Effectrix, ShaperBox — KVR forum](https://www.kvraudio.com/forum/viewtopic.php?p=8110379)
- [Cableguys ShaperBox Builds & Risers — rekkerd.org](https://rekkerd.org/?p=248865)
- [Effectrix 2 — Dubspot plugin file](https://blog.dubspot.com/de/plugins/effectrix-2)
- [Best free glitch VSTs 2026 — Augmented Signals](https://augmentedsignals.com/blog/best-free-glitch-vst-plugins-2026/)
- [Glitch² — Illformed](https://illformed.com/)
- [Best glitch VST plugins 2026 — Music Industry How To](https://www.musicindustryhowto.com/glitch-vst-plugins/)
- [JUCE 8 EULA changes — JUCE forum](https://forum.juce.com/t/amendments-to-the-juce-end-user-licence-agreement-for-juce-8/61265)
- [JUCE sample-accurate timing — JUCE forum](https://forum.juce.com/t/sample-accurate-timing/42871)
- [nih-plug — GitHub](https://github.com/robbert-vdh/nih-plug)
