<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Control interaction and editing contract

This is the baseline for current-control repairs in WP-007 and new controls
in the universal toolkit plan. [D-023](decisions.md#d-023--separate-owner-state-from-user-intent-notifications)
defines the state and notification choice. [controls.md](controls.md) names
each current type's actual properties, operations and signals.
The WP-007 [current-control applicability audit](universal-toolkit/implementation/evidence/WP-007-FND-10-applicability.md)
maps every catalog family to this contract and its focused evidence.

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

Controls that stage an edit distinguish a **draft** from the committed public
property. Cancel restores the committed display without a user-change signal.
A valid commit changes the public property and sends its documented
interaction signal once. A parser does not silently replace an invalid draft
with an unrelated value. It either keeps the draft visible with an error
state or explicitly documents rejection and restoration. Programmatic
assignment can replace the committed value even while user editing is
unavailable; each staged control defines what happens to an active draft.
Live text controls expose edits through their public `text` property as they
occur; `TextArea`'s cross-version focus and notification behavior is specified
in the table below.

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
| `Segmented`, `PillSwitcher`, `TabStrip`, `DockTabStrip` | Selection is owner assignable; activation signals report accepted user choice. Individual entries expose selected and focused state. Dock tabs use model-owned panel order and active selection; Left/Right plus Home/End navigate, and Space/Return/Enter or accessible press activate the focused panel. | App reconciles an index when the model removes/reorders entries and supplies a default choice if needed. `DockModel` owns dock-tab arrangement and active-panel state. |
| `NumberField` | `defaultValue`/`reset()`, `mixed`, invalid visible draft, read-only through `editable: false`, disabled guard and user-only `valueModified`. | App chooses numeric domain and default; document undo/redo remains outside the control. |
| `ZoomControl` | Owner `value` assignments are silent; zoom buttons and the keyboard/accessible reset button share guarded user operations. `valueModified` fires only for a changed value. An enabled reset button always sends `resetRequested`, including at 100%, as a command activation. | App owns the actual zoom state and may synchronize it while disabled; it handles reset commands and decides any document default. |
| `Slider`, `RangeSlider` | Template values and bounds; user movement signals are separate from owner assignment. RangeSlider has keyboard endpoint operations. | App supplies reset and mixed representations if needed; touch target and native value-interface checks are tracked separately. |
| `TextField`, `TextArea`, `SearchField` | TextArea is live text editing: user input changes owner-visible `text` immediately, read-only/disabled block user edits but permit owner assignment, Escape does not roll back, and inherited `editingFinished` marks focus loss rather than a user change. No user-only TextArea edit signal is guaranteed across the supported Qt 6.8+ range (`textEdited` arrived in Qt 6.9). TextField clear is gated; SearchField debounce begins on user edit, while owner text sync is silent. | App owns validation/default policy and handles submitted search results. TextArea apps needing a committed draft must keep that draft in their own model; apps needing cross-version user-only notifications must add an application-side input policy. |
| `ColorSwatches` | `current` is owner assigned; tap, key and accessible press emit guarded `selected(color)`. | App applies the chosen colour and owns defaults and a full custom picker. |
| `ColorButton` | Owner `color` assignment is silent. Pointer, Space and accessible press open the swatch popover through the template click path; disabled controls reject them. A swatch choice updates `color` and emits `colorSelected` once. | App supplies the palette, defaults and full custom picker. |
| `ColorField` | Invalid hex remains visible with `inputInvalid` until corrected or canceled with Escape. Valid user commits emit `colorEdited`; owner `color` and `applyHex()` changes are silent. Read-only and disabled states block user edits and picker requests. | App owns the full picker and any reset/default or mixed-colour policy. Native assistive announcements and broad layout qualification remain open. |
| `DataTable` | Owner `currentIndex` and arrow navigation are silent; tap, Return/Enter and accessible row press select and emit guarded `activated`. Realized rows report names and selection. Headers can be focused for keyboard/assistive sort and fixed-width resize. | App owns sort/filter and selection reconciliation when the model changes. Native virtual row traversal and large-table assistive behavior remain open. |
| `ListRow` | Owner `selected` assignment is silent; Space and accessible press use template click. Row text or tooltip names the accessible list item. | App owns list selection, including whether a click changes `selected`. |
| `Ruler` markers | Draggable markers have Slider roles, per-id focus, localized names and position descriptions. Left/Right and accessible increase/decrease emit the same clamped `markerMoved(id, position)` operation; `space.unit` pixels is the keyboard step. Focus is restored when the owner replaces the marker array in response. Delete emits `tabRemoved(id)` for a focused draggable tab only. Non-draggable markers are static text; disabled rulers and non-draggable markers reject user edits. Owner assignment to `markers` is silent. | App owns marker storage and applies `markerMoved`, `tabAdded` and `tabRemoved` results. A tab-add command remains available by pointer double-click only. |
| `MenuItem`, `MenuBar` | Menu items expose mnemonic-clean accessible names, MenuItem roles, checked state and disabled focusability. Space/Return, Alt mnemonics and accessible press activate through the template; `triggered` reports accepted user actions, while owner checked-state updates are silent. MenuBar and its entries expose explicit accessible roles and names. | App owns command effects, radio exclusivity and action state synchronization. Mnemonic dispatch and signal paths are covered in the Qt QML fixture; native screen-reader delivery still needs platform testing. |
| `ComboBox` | Owner `currentIndex` and editable `editText` assignment are silent; keyboard and pointer choice emit `activated`. In editable mode Return/Enter emits `accepted` for a validator-acceptable draft, while invalid input and disabled input do not. Popup rows report selected state, grow for scaled text, scroll within short hosts, and remeasure on control or ancestor movement. | App supplies its model/default, decides whether accepted novel text becomes a model entry, and reconciles invalidated selections. Native assistive behavior remains under platform qualification. |
| `Dialog`, `CommandPalette` | Dialog title supplies its accessible name (with “Dialog” fallback); its primary button is disabled for pointer, keyboard and accessible press when `primaryEnabled` is false. Command rows expose names and selected state; disabled model entries remain visible, are skipped by arrows, and reject user activation. Owner command-list changes do not emit `activated`; keyboard, pointer and accessible press share the accepted activation path. | Dialog accepted/rejected/tertiary are operation outcomes, including when the owner calls the corresponding template operation. Native screen-reader presentation of popup content remains platform-qualified work. |
| `DockDivider` | Focusable `Accessible.Splitter`; vertical seams use Left/Right and horizontal seams use Up/Down. Accessible increase/decrease and keyboard input emit the same signed `dragged(delta)` used by pointer movement, in `Theme.space.lg` increments. Disabled dividers reject all three paths. | `DockZone` applies the delta to `DockModel` lane extents or slot shares. Direction describes local seam motion; edge-zone callbacks may invert the corresponding extent change. Native assistive operation delivery remains platform-qualified work. |
| `Viewport` | Focusable named Pane; arrows/page/home/end pan the scroll surface and unconsumed edge keys can reach an ancestor Scroll. Plus/Minus and accessible increase/decrease share guarded zoom operations; owner `zoom` assignment is silent. Focus stays with a document child when it owns input. | The application supplies the document content, zoom limits and any document-level shortcuts. Native assistive delivery of the zoom actions remains platform-qualified work. |
| `DockFloating` resize handles | Eight named, focusable `Accessible.Splitter` targets resize applicable axes with arrow keys; accessible increase/decrease and pointer drag share the same `DockModel.setFloatingRect` operation and minimum-size clamp. `Theme.size.seamTarget` expands edges and corners without changing the floating frame's painted geometry. | On small frames, corner targets clamp to half the frame dimensions and an edge target is disabled if it cannot fit the remaining edge segment without overlap. Touch targets may therefore fall below 48px when the owner's minimum floating dimensions cannot fit eight distinct targets. Native assistive operation delivery remains platform-qualified work. |
| `StatusField` | Owner `text` updates are silent. Actionable fields expose a Button name and tooltip; pointer, Tab/Space/Return/Enter and accessible press share the template click path and emit one guarded `clicked`. Disabled fields reject user actions. `actionable: false` removes the button and Tab stop and exposes static text. | The app owns the meaning of the status and any action. Native screen-reader delivery remains platform-qualified work. |

## Verification for a new control

Test the owner assignment and a pointer, keyboard and accessible operation
against the same state and signal spy. Include disabled and applicable
read-only, mixed, invalid, reset and cancellation cases. Verify geometry in
Compact and Comfortable and the touch target in Touch; at large text, the
value, label, action and error state must remain reachable. For a layout or
focus-ring claim, save `qtk-preview --dump` geometry, grab a PNG and open it.
Native screen-reader, IME, monitor and physical touch claims require a real
platform test in addition to these QML fixtures.
