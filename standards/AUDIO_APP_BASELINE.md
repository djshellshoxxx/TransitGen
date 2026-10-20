# Circuit Drift Labs Audio Application Baseline

**Status: approved baseline, supplied by the project owner.**  
**Applies to:** CDL audio plug-ins and standalone audio applications. Requirements marked “when applicable” must be recorded as applicable, not applicable, or deferred in each project’s feature checklist. This document is the single source of truth for shared audio-app features and visual identity. Product-specific behavior, host constraints, and approved exceptions belong in the consuming project’s specification.

## 1. Product profiles and applicability

Use the profile that matches the product:

- **Plug-in:** VST3, AU, CLAP, or another supported plug-in format. The host owns audio-device selection and usually transport. Do not add standalone device controls to a plug-in unless the framework and format support them meaningfully.
- **Standalone audio app:** Owns its audio and MIDI device setup and must expose the relevant configuration in Options.
- **Instrument or audio generator:** Must support factory/user presets and offline WAV export of generated audio when the product produces renderable audio.
- **Effect or utility:** Must provide bypass and any relevant input/output monitoring. WAV export is required only when the product has an appropriate offline rendering workflow.

At project start, create a compliance table mapping each requirement below to an implementation location, test/evidence, and applicability. Do not imply that a feature is implemented merely because this standard requires it.

## 2. Shared CDL visual identity

### 2.1 Brand palette

The default **CDL theme** uses these owner-supplied tokens:

| Token | Value | Use |
|---|---|---|
| Background | `#0E1116` | Main window background |
| Panel | `#171B22` | Raised sections and header |
| Border | `#2A303A` | Edges and separators |
| Primary accent | `#E8532A` | Active values, selected states, primary actions |
| Secondary accent | `#4FB6C4` | Secondary indicators, modulation and links |
| Text primary | `#E6E8EC` | Main labels and values |
| Text muted | `#8A929E` | Hints, units and inactive labels |
| Success | `#7BC96F` | Positive status and signal-on states |
| Warning | `#F2C14E` | Warning and near-clip states |
| Shadow | `rgba(0,0,0,0.55)` | Subtle depth; 8 px blur, 2 px downward offset |

Individual products may re-tint one accent when their approved product identity calls for it. Keep the neutral palette, contrast, control language and CDL identity consistent. Do not invent extra official brand colors.

### 2.2 Themes, logo and typography

- Ship the CDL theme as the default, plus a distinct dark theme, a high-contrast theme, and at least two additional selectable themes. Themes may vary color tokens, but must preserve readable contrast and consistent control meaning.
- Put theme selection in Options. Persist the user's choice and apply it consistently to every window and panel.
- Use the official CDL logo asset wherever supplied. No logo asset is included with this specification; do not recreate, approximate, or substitute a generated logo. Until an approved asset is present in the repository, retain the specified 2 px burnt-orange diagonal notch at the main window's top-left as the CDL signature mark.
- Primary UI font: Inter, with Space Grotesk then system sans-serif fallbacks. Use weight 500 for labels and 600 for section headings. Numeric readouts use JetBrains Mono or IBM Plex Mono, weight 500, with tabular numerals. If fonts cannot be bundled or licensed, use the fallback and preserve legibility.
- Labels are approximately 11 px uppercase with 0.08 em tracking; values are 13–14 px. Use anti-aliased text.
- Show the product name and Options access in a 32 px header, with a separator beneath it. Show the version at bottom-right in muted 9 px monospace text.
- Signal output indicator: muted gray when silent, brightens toward white as level approaches 0 dBFS, and turns red on clipping. Never display a clip state from stale or fabricated meter data.

### 2.3 Shared control language

- Knobs are the preferred compact control. Default diameter 48 px; small 36 px; large 64 px. Use a flat-shaded dark body, 1 px inner border, 2 px accent indicator, 270-degree value arc outside the body with a 4 px gap, and a 4 px center dot. Double-click resets the parameter; right-click opens its context menu.
- A knob label sits below it. Show its numeric value on hover or while adjusting.
- Sliders use a 4 px rounded track, accent fill, 16 by 24 px thumb, and logical ticks where useful. Use a fader when the control's domain makes it materially clearer.
- If a panel would otherwise contain more than eight sliders, use knobs for suitable continuous controls to conserve space; retain faders where they are the appropriate interface.
- Buttons and toggles are 28 px high with 4 px corner radius. Off uses panel color and muted text; on uses a restrained accent tint, accent text and border. Momentary actions may flash for 100 ms.
- Meters use a smooth segmented presentation from teal through orange and yellow to red. Hold the peak for 1.5 seconds, then decay at 20 dB/s. Show a monospace numeric peak when useful.
- Every control supports a right-click menu with copy value, validated paste value, direct numerical entry, MIDI learn, and unlink MIDI mapping when the operation is relevant. Clearly indicate invalid values and preserve the last valid setting.
- Provide descriptive tooltips for interactive controls and non-obvious status displays. The user can turn tooltips off in Options.

## 3. Common controls and navigation

Place the common action row near the top of the main controls:

- **Stop/Panic:** Stop audio generated by the application and clear its active/stuck MIDI notes. In a plug-in, this must not stop the host transport or other tracks.
- **Reset:** Restore product controls and parameters to documented defaults. Preserve audio-device choices, MIDI assignments, theme, and user files unless the user explicitly chooses a full settings reset.
- **Randomize:** Randomize only the documented, user-facing sound or pattern parameters; honor each parameter's valid range and lock/exclude controls when provided. Do not randomize device, file, license, or safety settings.
- **Help:** Open product help and feature documentation.
- **Bypass:** Provide a clearly indicated bypass control for effects and any product where bypass is meaningful.
- **Preset selector:** Show available presets and allow the user to select one. Include **Export WAV** in this row only when the product has an applicable render workflow.

Where the product has play controls, show green Play, yellow Pause, and red Stop controls in the appropriate location. Play pulses green only while playback is active. Make all states available to assistive technology and do not use color as the sole state indicator.

Use navigation tabs instead of a row of buttons when more than three buttons are needed to reach distinct control areas. Use collapsible sections when space is constrained. Secondary windows are acceptable for controls that do not fit naturally in the primary workflow.

## 4. Presets and state

Every VST-style audio plug-in must support presets.

- Ship at least 10 useful factory presets that demonstrate distinct, representative uses of the product. Name and organize them clearly; do not count empty or near-identical variations toward the minimum.
- Support loading presets from the preset directory, selecting them from a dropdown, user-created presets, and user import/export.
- Saving a configuration as a custom preset must preserve all sound-affecting parameters required to recall it.
- Store presets as readable JSON or compressed JSON. Validate schema, parameter IDs, types, ranges, and version before applying imported data. Handle malformed, oversized, or incompatible files without crashing or partially corrupting the current state.
- Any valid preset added to the configured preset directory must appear in the selector after refresh or the next directory scan. Do not silently overwrite files; confirm replacement or choose a unique filename.
- Keep host state save/restore and preset-file operations consistent. State loading must not perform file/network I/O or unbounded work on a real-time audio callback.
- Provide A/B comparison buffers when the product architecture supports them. Clearly distinguish the comparison snapshot from a saved preset.

## 5. Options and device configuration

Every application has an Options page or dialog. Include applicable controls for:

- Audio driver/device, output, input, sample rate (44.1–192 kHz where supported), and buffer size (32–2048 samples where supported).
- MIDI input/output devices and MIDI mapping or learn. Plug-ins should use host-provided MIDI routing and expose mappings they own.
- Global tooltip on/off, theme selection, and a way to rerun first-run onboarding.
- Global animation on/off for meters, waveforms, decorative lights, and other motion. This is especially important for low-power use and accessibility.
- A 440 Hz sine reference tone at -12 dBFS for testing standalone signal paths, with an obvious stop control. Do not emit it unexpectedly from a plug-in.

Only expose options supported by the runtime and format. Persist user preferences safely and report device/configuration errors in a useful way.

## 6. Window behavior and visualizations

- Windows are resizable to expose more workspace, but individual control dimensions remain reasonable and generally fixed; resizing reflows or reveals content rather than stretching controls arbitrarily.
- When content exceeds the available window, use scroll bars. Honor high-DPI scaling and maintain keyboard focus during resize and navigation.
- Each audio app includes a basic visualization relevant to its operation, such as an input/output waveform, generated waveform, pattern display, or signal envelope. Its source and meaning must be clear. Provide an Options switch to hide it.
- Each app includes a low-overhead CPU-use gauge where the platform or framework exposes a meaningful estimate. Let users turn it off in Options. Label estimates accurately; do not claim plug-in CPU isolation when only host-wide usage is available.
- Animations respect the global animation setting. Avoid decorative activity that competes with useful signal information. A matrix/log animation is optional and must be non-intrusive, based on genuine app data, and disabled with animations.

## 7. Help and onboarding

- Provide accessible help that describes shipped features and each parameter's purpose, units and range. Keep it synchronized with the actual UI.
- Include CDL styling and the official logo when an approved asset is available. Include a clickable link to [Circuit Drift Labs](https://djshellshoxxx.github.io/circuitdriftlabs/). Provide a support section that can be populated as the support process is established.
- Run an interactive introduction on first launch where appropriate. Allow users to rerun it from Options. Do not repeatedly interrupt returning users.
- Help must distinguish supported functions and formats from planned work.

## 8. Audio, MIDI and application reliability

- Keep audio and MIDI setup appropriate to the host or standalone runtime. Handle device loss, sample-rate/block-size changes, channel layouts, MIDI offsets, and shutdown without hanging or leaking resources.
- Parameter states remain independent unless the product's documented mode requires an interaction. Inputs and UI actions must be deterministic and must not lock up the audio thread.
- Real-time callbacks must have bounded work and must not perform file/network I/O, allocate unpredictably, or wait on blocking locks unless a reviewed real-time design proves it safe.
- Provide clear feedback for invalid user input, missing devices, failed import/export, clipping, and recovery. Preserve the last valid state on recoverable errors.

## 9. Engineering and verification

- Before DSP, UI, architecture, or troubleshooting work, consult relevant indexed skills and troubleshooting notes in this repository. Read applicable standards and the consuming project's instructions.
- Keep reusable DSP independent of product-specific UI and plug-in wrappers. Specify API, ownership, threading, error behavior, dependencies, compatibility and tests for any shared module.
- Use lower-cost models for routine boilerplate and higher-reasoning review for complex DSP, real-time synchronization, and difficult failures. Do not repeat the same failing approach without new evidence.
- For release readiness, audit requirements, missing UI, unwired controls, incomplete signal paths, stability, performance, packaging and supported formats. Run targeted combination and fuzz tests for parameters, MIDI and preset imports. Correct failures and record exact commands and outcomes.
- Test feasible cases rather than claiming exhaustive enumeration of all parameter combinations. Mark tests blocked by unavailable hardware, host, or platform as unverified. Use **BETA READY** only when project-specific acceptance gates are met and evidence is recorded.
- Keep features modular and testable. Record project-specific exceptions and deferred requirements with rationale, owner, and follow-up milestone.

## 10. Per-project compliance checklist

For every consuming audio project, record:

| Requirement | Applicability | Implementation location | Verification evidence | Exception/deferred item |
|---|---|---|---|---|
| Theme, palette, logo and typography | Required | | | |
| Options and device/MIDI setup | As applicable | | | |
| Presets and state restore | Required for VST-style plug-ins | | | |
| Common action row | Required, individual actions as applicable | | | |
| Audio export | When render workflow applies | | | |
| Waveform/signal visualization | Required | | | |
| CPU gauge | Required where meaningful estimate is available | | | |
| Help and first-run introduction | Required | | | |
| Resize, scroll, navigation and accessibility | Required | | | |
| Audio/MIDI lifecycle and reliability | Required | | | |
| Tests and release evidence | Required | | | |
