# Shared CDL guidance review

## Copied folders

The `standards/`, `skills/`, and `troubleshooting/` folders were imported from `circuitdriftlabs-shared`. TransitGen's pre-existing `skills/` files were retained; the import had no duplicate paths.

## Accuracy fixes made for this project

- The shared `standards/README.md` described all standards as intake templates even though it contains an approved audio baseline. It now identifies the canonical approved baseline and provisional checklists.
- The shared `skills/README.md` said the skill contents had not been supplied. It now lists the available, TransitGen-relevant skills and describes when to use them.
- The shared `troubleshooting/README.md` said there were no verified entries. It now accurately describes the imported issue reports and clarifies that prior-project reports must be validated against TransitGen's environment.

## Guidance used

- `standards/AUDIO_APP_BASELINE.md` applies because TransitGen is an audio plug-in. Its 10-preset minimum, Options, Help, visualization, UI resizing, and accessibility expectations are tracked in `docs/spec/09-shared-baseline-compliance.md`.
- `skills/audio-dsp-review/` and `skills/audio-numerics-review/` apply to callback and generator changes.
- `skills/juce-review/` applies to processor/editor lifecycle and parameter bindings.
- `skills/plugin-improve/` offers useful backup and regression practices; its prerequisite requires a finished plugin, which TransitGen is not, so that gate is inapplicable.
- `skills/build-automation/` and `skills/plugin-testing/` apply to CI and host validation.
- Troubleshooting reports remain examples only; no report was treated as proof of a TransitGen defect.

## Current implementation scope

M4 now has a resizable energy-curve editor and a deterministic view of the next generated plan's Source, Gate, Crush and Filter lanes. Preview triggering, undo/redo, complete accessible navigation, and shared baseline items remain open. The next preset work must resolve the shared JSON preset format against TransitGen spec 06's XML format before it becomes a public file contract.
