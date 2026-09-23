<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-10 — ColorField interaction slice

**Scope:** ColorField hex editing, accessible picker action, and user versus
programmatic notifications. This is one bounded control slice, not acceptance
of WP-007 or the entire FND-10 input inventory.

## Reproduced gap

Before the repair, `ColorField` called `applyHex()` on Return and when the
editor lost focus. An invalid draft was immediately replaced with the current
colour and there was no read-only invalid-state property for an application to
inspect. The new focused regression initially failed on the first assertion:

```text
FAIL! ControlsInput::test_colorfield_invalid_draft_stays_visible_until_escape()
Actual: #112233
Expected: x
```

## Change

Invalid typed hex remains in `colorHex` after Return and focus loss, sets the
read-only `inputInvalid` property, and paints the input border with the danger
role. Escape restores the committed colour and clears the state. `readOnly`
and `enabled: false` prevent edits and picker requests. The swatch exposes a
focusable accessible Button action and responds to Space, Return, and Enter.
The text editor exposes its current colour and the invalid recovery hint.

`applyHex(text)` and direct `color` assignment are silent programmatic update
paths. A changed valid hex commit made by the user emits `colorEdited(color)`;
invalid or unchanged input does not.

`ColorButton` was inspected as the separate toolbar picker control. Its
`T.AbstractButton` click, accessible Button name, swatch selection and custom
request paths remain covered by the existing ColorButton test; this change
does not claim broader picker acceptance.

## Verification

- The pre-repair regression above reproduced the dropped-draft behavior.
- Built `tst_qml` and `qindatk` in this worktree with `cmake --preset dev`
  configuration and no command-local parallelism overrides.
- The focused QML run passed ColorField invalid-draft recovery, user-only
  `colorEdited`, read-only/disabled gates, silent programmatic changes, and the
  existing ColorButton swatch-selection test on Qt 6.11.1:
  `8 passed, 0 failed`.
- `tools/scripts/gen_catalog.py` regenerated `docs/catalog.json` with the
  ColorField properties and interaction methods.
- Integration moved the ColorField cases into a dedicated QML test file to
  keep `tst_controls_input.qml` below the 500 nonblank line limit. The picker
  swatch gained a visible theme focus ring. Invalid programmatic `applyHex()`
  input leaves both the committed value and invalid-draft state unchanged.
- The integration fixture was dumped and grabbed at 320×160, and its PNG was
  opened. The dump shows `ColorField#reviewColorField 24,24 180x24`,
  `Item#colorSwatch 0,4 16x16`, the focus-ring rectangle at `-2,-2 20x20`,
  and `TextInput#colorHex 20,0 150x24 "oops"`. The capture visibly shows the
  cyan swatch ring and danger border around the retained invalid draft.
  Ignored artifacts are `build/wp007-colorfield-review.{qml,png}`.

Native screen-reader announcements and visual review with an active screen
reader were not tested. This slice does not certify remaining WP-007 controls.
