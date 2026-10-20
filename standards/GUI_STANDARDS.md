# GUI Architecture and Standards

**Status: provisional engineering guidance; visual brand details await THEME_STYLE_SPEC.md.**

Keep DSP independent from GUI code. Use bounded UI-to-processor communication; prohibit blocking or allocation-inducing GUI work in the audio callback. Provide stable parameter IDs and safe state synchronization. Support supported DPI/scaling, keyboard focus and visible values; verify host editor lifecycle and resizing behavior. Prefer reusable UI components behind presentation interfaces.

Once approved theme values exist, map them to design tokens and inspect GUI/help examples against the theme.
