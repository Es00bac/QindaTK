<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# HYGIENE — shared-control and build fixes

**State: built and verified on `work/hygiene-cf` (merged with `round/cf1`).**

Three cross-cutting fixes, written code-first and then built and tested by
the round's build-and-fix stage. Decisions:
[D-198](../../../decisions.md) (NumberField never writes `value` or `mixed`)
and [D-199](../../../decisions.md) (Qt modules are found only in the Qt in
use). D-200 is unused.

| Requirement | Now in the code | Tests |
|---|---|---|
| NumberField keeps host bindings | The field shows the read-only `clampedValue`, steps and scrubs from it, and reports edits and `reset()` only through `valueModified(clamped)`. The WP-051 copy-in workarounds (TextFrameEditor, PageSetupDialog, TableEditor), the Knob/Fader/AlignDistributePanel `Binding` elements and StrokeStyleEditor's NumberField sync are plain bindings; model-less hosts echo the edit. | `tst_controls_input.qml`: `test_numberfield_clamps_for_display_and_steps`, `test_numberfield_keeps_host_binding`, `test_numberfield_never_writes_value_or_mixed`; `tst_numberfield_touch_step.qml`; the host suites below |
| ComboBox elides its label | A non-editable combo draws `comboDisplayText` elided over the input, whose hidden full text keeps `implicitWidth`. Editable mode unchanged. | `test_combobox_elides_label_and_keeps_full_implicit_width`, `tst_combo_editing.qml` |
| CMake finds modules in the Qt in use | `qindatk_find_qt_module()` for Quick3D, ShaderTools, Widgets, PrintSupport, QuickPrivate: only the Qt6_DIR tree, exactly `Qt6_VERSION`, stale foreign `Qt6<Module>_DIR` dropped. | Configure proofs below |

## Build-and-fix results (2026-09-25)

Build: `cmake --build build/cf -- -j12 -l12` against system Qt 6.11.1, with
Quick3D and ShaderTools found (no `DISABLE_FIND_PACKAGE` flags). Exit 0.

| Run | Result |
|---|---|
| `tst_qml -input tests/qml` (whole QML suite, offscreen, software) | 913 passed, 2 failed |
| `ctest --test-dir build/cf -E '^tst_qml$'` (C++ tests + example smokes) | 316 of 317 passed |
| `tst_controls_input.qml` | 57 passed |
| `tst_numberfield_touch_step.qml` | 3 passed |
| `tst_combo_editing.qml` | 3 passed |
| `tst_dock_placement.qml` | 10 passed |
| `tst_vector_appearance_example.qml` | 6 passed |
| `tst_print_preflight.qml` | 4 passed |
| Configure against the Qt 6.8.3 SDK (`-DCMAKE_PREFIX_PATH` only) | exit 0; "SceneView3D renderer unavailable", "GPU volume shaders unavailable"; 0 `/usr/lib64/cmake/Qt6*` entries in the cache |

The three remaining failures are WP-057's and are not caused by this
branch: `NotebookBlocks::test_bound_editor_edits_the_persistent_model`,
`NotebookBlocks::test_drafts_survive_delegate_recycling` and
`tst_notebook_services::codeBlocksEvaluateThroughTheScriptRuntime`. All
three are fixed on `work/wp057-fix` (ce9fcfb) and will pass once that merges.

Fixes made in this stage:

- **ComboBox**: the new `id` on the contentItem stopped Qt deferring it, and
  an editable combo then reported `"12"` as acceptable under a 0–9 validator
  (`tst_combo_editing` line 48). The id is gone; the elided Text reads
  `parent.font`. An AGENT-GUARD records why.
- **PrintPreview**: at comfortable density the 320px settings panel put two
  35px combos side by side and elided every label away. The paired rows now
  sit two up only when each editor keeps `Theme.size.fieldWidth`; otherwise
  they stack (`--dump`: `ComboBox#printPaper 0,0 200x29`, was `35x29`).
- **Tests corrected** because they assumed the old self-writing field:
  `tst_vector_appearance_example` assigned `width.value = 6` itself, and
  `tst_dock_placement` read the raw unclamped draft `y.value` (100) where
  the field shows and steps from `clampedValue`.

## Screenshots (inspected)

- `HYGIENE-inputs-{compact,comfortable}.png` from `HYGIENE-inputs.qml`: the
  gallery's Inputs and Structure sections (NumberField echo handlers,
  ComboBox labels).
- `HYGIENE-print-preflight-{compact,comfortable}.png`: settings combos with
  full labels, stacked at comfortable.
- `HYGIENE-gallery-{compact,comfortable}.png`,
  `HYGIENE-studio-shell-{compact,comfortable}.png`: the changed examples.

Observed and left alone (not this branch): the gallery opens scrolled to
the mail composer, because RichTextEditor's inner area sets `focus: true`
and the gallery Scroll follows focus. About 20 controls set `focus: true`
the same way, so the fix belongs to one focus-policy change, not here.
