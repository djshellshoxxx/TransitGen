# TransitGen shared audio baseline compliance

This table tracks the approved requirements from `standards/AUDIO_APP_BASELINE.md`. “Deferred” means the feature is not implemented; it is not a claim of compliance. Plug-in-only requirements apply unless identified as standalone-specific.

| Requirement | Applicability | Current implementation | Verification / follow-up |
|---|---|---|---|
| CDL visual theme and product identity | Required | Shared color tokens are used in the new curve editor. The rest of the editor still uses JUCE defaults; theme selection and logo asset are absent. | Apply tokens throughout the UI; add theme selection only with persistent options. No approved logo asset is present. |
| Presets and host state | Required | Host project state save/restore exists. File preset browser, user presets, import/export, and factory preset set are absent. | Reconcile JSON baseline with spec 06 XML `.tgpreset`; add at least 10 distinct factory presets, then reach project target of 30. Validate before changing state. |
| Options | Required, host-specific | No Options view. Device configuration belongs to the host for the plug-in. | Add applicable UI settings (theme, tooltips, animation, help, onboarding); do not duplicate host device controls. |
| Common action row | Required as applicable | Re-roll, lock, Preview and Help exist. Host bypass exists, but the editor has no bypass control. Reset and randomize are absent. | Add an editor bypass control and define reset/randomize behavior before wiring them. |
| Audio export | Not applicable to current effect workflow | No offline render/export workflow. | Reassess if a render workflow is added. |
| Visualization | Required | Editable energy curve, deterministic Source/Gate/Crush/Filter view of the next generated plan, and a post-processor output peak meter are implemented. | Validate meter behavior in the plug-in test suite and in supported hosts. |
| CPU gauge | Required where meaningful estimate exists | Absent. | Investigate a trustworthy plug-in-side estimate; label its scope and allow hiding it. |
| Help and first-run introduction | Required | Help dialog covers shipped controls; first-run introduction is absent. | Add first-run introduction with a rerun path in Options. |
| Resize, scrolling, navigation, accessibility | Required | Editor has bounded resizable dimensions, curve undo/redo, parameter names/tooltips, keyboard shortcuts and a focus ring on the curve editor. Parameter rows retain fixed sizing; full keyboard navigation and assistive validation remain incomplete. | Add first-run focus guidance and test keyboard/DPI/focus behavior in supported hosts; record JUCE host limitations. |
| Audio/MIDI lifecycle and reliability | Required | Core and wrapper tests exist for processing/state; preview is queued as a one-shot with a wrapper test. | Run CI, pluginval, sample-rate/block-size/channel and MIDI offset checks on supported systems. |
| Tests and release evidence | Required | Core/plugin tests and 3-OS CI workflow exist. Local CMake was unavailable in this environment. | Record successful CI and pluginval/DAW evidence; avoid release readiness claims until then. |
