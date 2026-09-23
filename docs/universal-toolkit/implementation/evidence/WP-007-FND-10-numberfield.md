<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-10 — NumberField editing semantics slice

**Scope:** one existing input control. This is a focused FND-10 repair, not
acceptance of WP-007 or a uniform editing contract for every control.

## Change

`Tk.NumberField` now exposes `defaultValue` and `reset()`. `reset()` applies a
clamped default as an explicit edit and emits `valueModified` if the effective
value changes or a mixed value is resolved. Assigning `value` remains silent.
`mixed` displays a placeholder and survives focus alone; a valid edit resolves
it, including when the resulting number equals the representative `value`.

Malformed or empty edited drafts remain visible and set read-only
`inputInvalid`; Escape restores the effective value and clears that state.
`editable: false` and `enabled: false` now gate keyboard, scrub, wheel and
step-button edits consistently. This repair does not provide a reset button or
define default/reset semantics for other controls.

## Evidence

- Read the configured build policy: `portageq envvar MAKEOPTS` returned
  `-j24 -l24`; `cmake --build --preset dev` inherited it without overrides.
- `cmake --preset dev` and `cmake --build --preset dev` completed in this
  worktree.
- `ctest --preset dev -R '^tst_qml$'` passed, including new QML assertions for
  reset versus programmatic notifications, mixed focus/commit, invalid-draft
  recovery, and read-only/disabled edit rejection.
- `cmake --build --preset dev --target all_qmllint` exited successfully. It
  emitted the repository's existing unqualified-access warnings in other
  QML controls.
- `python tools/scripts/gen_catalog.py` regenerated the NumberField API entry
  in `docs/catalog.json`.

After integration, the full suite passed 19/19 on Qt 6.11.1 and 19/19 on
Qt 6.8.3. A 2× text preview with mixed, invalid, read-only and disabled
fields was dumped and captured on the software renderer. It reported
`NumberField#mixedField 8,8 404x36` with its visible
`Text#numberMixedPlaceholder 48,0 334x36`, followed by an invalid field
at `8,52 404x36`; no rows overlapped. The opened PNG showed the invalid
draft and its danger border. A high-contrast dark OpenGL RHI capture of the
same fixture was opened; all four labels and values remained legible. The
ignored captures are `build/numberfield-states-software.png` and
`build/numberfield-states-hc-gpu.png` in the integration worktree. The mixed
placeholder now hides while a draft is typed; the QML regression asserts
that transition.

## Limits

The tests exercise QML behavior under headless Qt on both supported versions.
Native screen-reader announcements/actions and visual review under a QindaQt
desktop theme remain unverified. Other
controls still need their own default/reset, mixed/invalid, read-only/disabled,
focus, and user/programmatic notification contracts.
