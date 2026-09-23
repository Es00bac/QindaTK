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

## Limits

The tests exercise QML behavior under the headless Qt 6.11.1 test runner.
Native screen-reader announcements/actions, visual review under a desktop
theme, and behavior on other supported Qt versions remain unverified. Other
controls still need their own default/reset, mixed/invalid, read-only/disabled,
focus, and user/programmatic notification contracts.
