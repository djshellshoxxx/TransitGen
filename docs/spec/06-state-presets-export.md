# TransitGen Specification — 06 State, Presets, Export

## 1. Plugin state (host project save)
Saved as a `juce::ValueTree` written to binary XML via `getStateInformation`.
```xml
<TransitGen stateVersion="1" pluginVersion="1.0.0">
  <PARAMS> ...APVTS parameters by ID... </PARAMS>
  <CURVE preset="Ramp Up"> <P t="0" v="0.1" c="0.3"/> ... </CURVE>
  <SEEDS history="4821,77,1203"/>
  <LOCK on="0"/>
  <STYLE id="1" version="1"/>
  <FROZEN on="0"> <PLAN>base64 of FillPlan v1 binary</PLAN> </FROZEN>
  <UI scale="1.0" ab="A"/>
</TransitGen>
```
- **Loading:** unknown elements are ignored, missing elements get defaults, and a `stateVersion` higher than supported means load what we can and show a "saved with a newer version" notice.
- **Migrations:** a chain of `migrate_vN_to_vN+1(ValueTree&)` functions. Every release that changes the state format adds one, plus a test loading a fixture state from each older version.
- `setStateInformation` runs on the message thread and publishes to the audio thread per 01 §7. It must never block audio.

## 2. Presets
- File: `.tgpreset`, the same XML as §1 minus `<UI>` and `<SEEDS>`.
- Locations:
  - factory presets are embedded in the binary;
  - user presets go in `~/Documents/TransitGen/Presets/` (Windows: `%USERPROFILE%\Documents\TransitGen\Presets\`).
- Browser: categories = style names + "User". Search by name. Prev/next arrows. Save / Save As / Delete / Rename / Show in folder. A preset marked "modified" gets a `*` suffix.
- **Factory set (v1.0):** 6 styles × 5 presets = 30 (e.g. "Dubstep – Classic Drop Roll", "Trap – Triplet Hat Stop").

## 3. Frozen plan
- **Freeze** (v1.0 button in the plan view ⋯ menu, and implied by timeline edits in v1.x) stores the current plan in the state. While frozen:
  - the engine plays the stored plan for every fill (ignoring the seed, style and knobs, except Mix/Output);
  - the knobs that would change generation are greyed out.
- The plan is scaled when the fill length differs from the frozen length: positions × (newLen / frozenLen), then re-snapped to the grid. If validation fails, the frozen plan is ignored with a warning.
- Binary format v1: a little-endian header `TGPL`, `u16 version`, then fields in the declared order of 01 §3 (doubles as IEEE-754 binary64, floats as binary32). Explicit, never a raw `memcpy` of the struct.

## 4. Export (v1.x)
### 4.1 Audio: "Drag last fill"
- While a fill plays, the engine also writes its **output** into a pre-allocated record buffer (length = longest fill at the current tempo + 1 beat).
- At the end of the fill the buffer is published to the UI (double buffer). The UI offers a drag handle that writes `TransitGen_<style>_<seed>_<bpm>bpm.wav` (24-bit, project sample rate) to a temp folder and starts an external drag-and-drop.
- The file includes 1 beat of the post-fill audio so the transition lands in context.

### 4.2 MIDI: "Export plan as MIDI"
A Type-1 MIDI file, 960 PPQ, tempo = the current BPM.
| Track | Content |
|---|---|
| Source | one note per source event: C1 Pass (omitted), C#1 Stutter, D1 Reverse, D#1 TapeStop, E1 TapeStart, F1 Silence. Velocity = 64 + 63·e at the event start. |
| Gate | one note per gate step that is on (G1) |
| Crush | CC 20 = bits (mapped 1..16 → 0..127), CC 21 = downsample |
| Filter | CC 74 = cutoff lane, CC 71 = resonance lane, written every 1/32 beat |

Use: users can see the structure in their DAW, or use the CCs to drive other plugins in sync.

## 5. Tests
- State round trip: save → load → save is byte-identical.
- Each older state fixture loads correctly (once the first release ships).
- Frozen plan binary round trip, plus fuzzing of corrupted data (must be rejected safely).
- Every factory preset loads and generates a valid plan.
