<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Control interaction and editing contract

This is the baseline for current-control repairs in WP-007 and new controls
in the universal toolkit plan. [D-023](decisions.md#d-023--separate-owner-state-from-user-intent-notifications)
defines the state and notification choice. [controls.md](controls.md) names
each current type's actual properties, operations and signals.

## State and notifications

The application owns the public value, selection or checked property. Loading
a document, undoing, synchronizing a model or assigning a binding updates that
property and ordinary property-change notifications. It does not synthesize a
new user operation. A separate interaction signal (`activated`, `selected`,
`valueModified`, `rangeModified`, and similar per-control names) is emitted
only by an accepted operation. A real user can initiate one through pointer,
keyboard or an accessible action; those paths share the same validation and
disabled/read-only guard. A single accepted operation produces one interaction
signal. A rejected or unchanged operation is silent unless a control
explicitly documents a command-like activation of the already selected item.

Editing controls distinguish a **draft** from the committed public property.
Cancel restores the committed display without a user-change signal. A valid
commit changes the public property and sends its documented interaction
signal once. A parser does not silently replace an invalid draft with an
unrelated value. It either keeps the draft visible with an error state or
explicitly documents rejection and restoration. Programmatic assignment can
replace the committed value even while user editing is unavailable; the
control must define what happens to an active draft.

`reset()` is an explicit operation. Where a control has a `defaultValue`, its
reset behavior and signal policy are documented with that control. An app that
owns a command or document default can reset an ordinary control by assigning
the owner property; QindaTK does not invent a default for every field.
`mixed` applies to an aggregate value from multiple selected objects. It is
cleared by the first accepted user edit; merely focusing the control does not
convert the aggregate to one value. A control that cannot represent a mixed
value must not present an ordinary value as though all objects agree.

`readOnly` preserves focus, selection and copying where the underlying
template supports them, while blocking edits and custom edit affordances.
`enabled: false` blocks pointer, keyboard and accessible operations, removes
interactive tab stops, and uses the themed disabled appearance. These gates
do not block an owner from assigning state. Model entries with
`enabled: false` are skipped by keyboard navigation and inaccessible as
actions while keeping their labels visible.

## Focus and assistive behavior

Every interactive control has a role and a name path. Icon-only actions need
`tooltip`; the same text supplies their accessible name. A focused action has
a visible `Tk.Theme.color.focus` indication. A composite's delegate, rather
than just an unrelated root item, reports selection and focus when the user
operates that delegate. `Accessible.pressAction`, increase/decrease and
equivalent paths call the same operation as keyboard and pointer input.
Popups keep the current choice visible, remain reachable in the host, and
return focus to their opener or parent popup on cancellation.

## Current-control applicability

The table distinguishes a state that the toolkit implements from a state that
belongs to the application. It is a snapshot of the WP-007 audit, not a claim
that every family is fully qualified on every native accessibility backend.

| Family | Toolkit state and applicable edit behavior | Owner responsibility or remaining qualification |
| --- | --- | --- |
| `CheckBox`, `Switch`, `RadioButton` | Template `checked`; CheckBox also has `checkState`/partial state. Space and accessible press use template click. `clicked`/`toggled` are user input signals in the tested Qt paths. | App supplies any reset default. Native mixed-state announcement needs platform testing. |
| `Segmented`, `PillSwitcher`, `TabStrip` | `currentIndex` is owner assignable; activation signals report user choice. Individual entries can be disabled; selected and focus state are exposed on delegates. | App reconciles an index when the model removes/reorders entries and supplies a default choice if needed. |
| `NumberField` | `defaultValue`/`reset()`, `mixed`, invalid visible draft, read-only through `editable: false`, disabled guard and user-only `valueModified`. | App chooses numeric domain and default; document undo/redo remains outside the control. |
| `Slider`, `RangeSlider` | Template values and bounds; user movement signals are separate from owner assignment. RangeSlider has keyboard endpoint operations. | App supplies reset and mixed representations if needed; touch target and native value-interface checks are tracked separately. |
| `TextField`, `TextArea`, `SearchField` | Inherited read-only text editing. TextField clear is gated; SearchField debounce begins on user edit, while owner text sync is silent. | App supplies validation/default policy and handles submitted search results. |
| `ColorSwatches` | `current` is owner assigned; tap, key and accessible press emit guarded `selected(color)`. | App applies the chosen colour and owns defaults and a full custom picker. |
| `ColorField` | Invalid hex remains visible with `inputInvalid` until corrected or canceled with Escape. Valid user commits emit `colorEdited`; owner `color` and `applyHex()` changes are silent. Read-only and disabled states block user edits and picker requests. | App owns the full picker and any reset/default or mixed-colour policy. Native assistive announcements and broad layout qualification remain open. |
| `DataTable` | Owner `currentIndex` and arrow navigation are silent; tap, Return/Enter and accessible row press select and emit guarded `activated`. Realized rows report names and selection. | App owns sort/filter and selection reconciliation when the model changes. Header keyboard/assistive actions and virtual row traversal remain open. |
| `ListRow` | Owner `selected` assignment is silent; Space and accessible press use template click. Row text or tooltip names the accessible list item. | App owns list selection, including whether a click changes `selected`. |
| `ComboBox` | Public selection and template popup behavior exist. | Popup and accessible selection behavior remain under WP-007 audit. |

## Verification for a new control

Test the owner assignment and a pointer, keyboard and accessible operation
against the same state and signal spy. Include disabled and applicable
read-only, mixed, invalid, reset and cancellation cases. Verify geometry in
Compact and Comfortable and the touch target in Touch; at large text, the
value, label, action and error state must remain reachable. For a layout or
focus-ring claim, save `qtk-preview --dump` geometry, grab a PNG and open it.
Native screen-reader, IME, monitor and physical touch claims require a real
platform test in addition to these QML fixtures.
