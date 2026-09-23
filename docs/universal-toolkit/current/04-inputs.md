<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Text, numeric, choice and color input

Status: **CURRENT — source review dated 2026-09-22.** These entries describe the
working tree, not promised capabilities or a runtime certification. Use
`import QindaTK as Tk` for main-module types. `T` below means QtQuick.Templates.

Each entry describes purpose, consumer behavior and limits. **Declared members**
list locally declared properties, including computed implementation helpers;
inherited Qt properties/signals are not repeated exhaustively. A `ro` suffix
means read-only at the declaration. Method lists can include internal helpers;
the consumer contract is the prose, the existing reference and the linked source.
Object names list source assignments; expressions ending in an ID/index generate
per-item names. Test links show source coverage, not a test run in this audit.

[Book index](../README.md) · [Type index](00-type-index.md) ·
[Source findings](../reports/04-source-audit.md)

## CheckBox

Template checkbox including partial/tristate state.

**Source:** [src/qml/CheckBox.qml](../../../src/qml/CheckBox.qml) · **Base:** `T.CheckBox` · **Kind:** QML component.

text, checked/checkState and tristate are inherited; small and tooltip are added. Indicator 14/12 px and row 20/16 px compact. Template toggled is the event; multi-object mixed-value editing needs an external binding contract.

**Declared members:** `small: bool`, `tooltip: string`, `boxSize: real` (ro), `rowHeight: real` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## Switch

Template on/off toggle with animated thumb.

**Source:** [src/qml/Switch.qml](../../../src/qml/Switch.qml) · **Base:** `T.Switch` · **Kind:** QML component.

text/checked/toggled come from the template; small scales the track and row. Compact row 20 px, small 16. Theme controls visual transition duration; the application owns the setting being switched.

**Declared members:** `small: bool`, `tooltip: string`, `trackHeight: real` (ro), `trackWidth: real` (ro), `knobSize: real` (ro), `rowHeight: real` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## RadioButton

Template radio choice with a themed circular indicator.

**Source:** [src/qml/RadioButton.qml](../../../src/qml/RadioButton.qml) · **Base:** `T.RadioButton` · **Kind:** QML component.

small and tooltip extend the template; checked/toggled and exclusivity follow Qt template behavior. Compact row 20 or 16 small. Grouping across separate parents requires an explicit owner/group contract.

**Declared members:** `small: bool`, `tooltip: string`, `circleSize: real` (ro), `rowHeight: real` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## TextField

Single-line text input with validation appearance and optional accessories.

**Source:** [src/qml/TextField.qml](../../../src/qml/TextField.qml) · **Base:** `T.TextField` · **Kind:** QML component.

iconName, clearable, small, error, mono, tooltip and trailing slot extend the template. clearText emits cleared; Escape clears when enabled. Height 24/20 compact. Input validators and IME primitives are inherited; there is no toolkit-wide form validation controller.

**Declared members:** `iconName: string`, `clearable: bool`, `small: bool`, `error: bool`, `mono: bool`, `tooltip: string`, `trailing: alias`, `controlHeight: real` (ro), `iconSize: real` (ro), `edgePadding: real` (ro), `showClear: bool` (ro).

**Declared signals:** `cleared()`.

**Source methods:** `clearText()`. See source for parameters and helper visibility.

**Object names:** `"fieldClear"`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## SearchField

TextField specialized for search and clearing.

**Source:** [src/qml/SearchField.qml](../../../src/qml/SearchField.qml) · **Base:** `Tk.TextField` · **Kind:** QML component.

debounce defaults to 150 ms; searched(text) fires after it, on Enter, or immediately when cleared. It supplies query entry, not indexing, fuzzy ranking or asynchronous result ownership.

**Declared members:** `debounce: int`.

**Declared signals:** `searched(string text)`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## TextArea

Wrapping multiline text input whose natural height grows with content.

**Source:** [src/qml/TextArea.qml](../../../src/qml/TextArea.qml) · **Base:** `T.TextArea` · **Kind:** QML component.

rows sets the minimum visible lines; mono/error/tooltip style it. `text` changes immediately during typing, `editingFinished` marks focus loss (including after owner synchronization while focused), and Escape does not roll text back. It has no distinct draft/commit/cancel or cross-version user-only edit signal; Qt's inherited `textEdited` exists only since 6.9 while QindaTK supports 6.8. Read-only/disabled block user edits but permit owner assignment. Place in Scroll for bounded height. Inherited text editing is useful for notes; paginated word processing, revisions and large-document virtualization are separate capabilities.

**Declared signals:** `editingFinished()` (inherited; emitted when focus is lost, not a user-change notification).

**Declared members:** `rows: int`, `mono: bool`, `error: bool`, `tooltip: string`, `minimumHeight: real` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## NumberField

Inspector number editor supporting typing, stepping and dragging.

**Source:** [src/qml/NumberField.qml](../../../src/qml/NumberField.qml) · **Base:** `T.Control` · **Kind:** QML component.

value/from/to/stepSize/decimals plus prefix/suffix/label; editable, scrub, scrubSensitivity, stepButtons and live govern interaction. Ctrl+wheel and arrows step, Shift accelerates, Alt gives fine increments. valueModified is for user edits; editingFinished closes an edit. No expression parser or mixed-value model is supplied.

**Declared members:** `value: real`, `from: real`, `to: real`, `stepSize: real`, `decimals: int`, `prefix: string`, `suffix: string`, `label: string`, `editable: bool`, `scrub: bool`, `scrubSensitivity: real`, `stepButtons: bool`, `small: bool`, `error: bool`, `live: bool`, `tooltip: string`, `inputItem: alias` (ro), `editing: bool` (ro), `controlHeight: real` (ro), `edgePadding: real` (ro).

**Declared signals:** `valueModified(real value)`, `editingFinished()`.

**Source methods:** `clamp()`, `format()`, `commit()`, `step()`, `syncText()`, `applyInput()`. See source for parameters and helper visibility.

**Object names:** `index === 0 ? "numberStepUp" : "numberStepDown"`, `"numberLabel"`, `"numberInput"`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## Slider

Horizontal template value slider with optional numeric readout.

**Source:** [src/qml/Slider.qml](../../../src/qml/Slider.qml) · **Base:** `T.Slider` · **Kind:** QML component.

showValue, decimals, suffix, small and tooltip extend inherited value/from/to/stepSize. valueModified relays moved. Compact visual row is 16 px or 14 small, commonly placed in a 24 px inspector row. No logarithmic audio taper or gesture transaction API is supplied.

**Declared members:** `showValue: bool`, `decimals: int`, `suffix: string`, `small: bool`, `tooltip: string`, `rowHeight: real` (ro), `handleSize: real` (ro), `trackThickness: real` (ro), `readout: string` (ro).

**Declared signals:** `valueModified(real value)`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## RangeSlider

Two-handle horizontal range editor.

**Source:** [src/qml/RangeSlider.qml](../../../src/qml/RangeSlider.qml) · **Base:** `T.RangeSlider` · **Kind:** QML component.

Inherited first/second values are ordered by the template; showValues/decimals/suffix/small style the readout. rangeModified(first,second) reports movement. Matches Slider geometry. Domain range validity beyond template bounds belongs to the model.

**Declared members:** `showValues: bool`, `decimals: int`, `suffix: string`, `small: bool`, `tooltip: string`, `rowHeight: real` (ro), `handleSize: real` (ro), `trackThickness: real` (ro), `readout: string` (ro).

**Declared signals:** `rangeModified(real first, real second)`.

**Object names:** `"rangeSlider"`, `"rangeSliderTrack"`, `"rangeSliderBand"`, `"rangeSliderFirstHandle"`, `"rangeSliderSecondHandle"`, `"rangeSliderValue"`.

**Coverage pointers:** [tst_media.qml](../../../tests/qml/tst_media.qml).

## ComboBox

Template dropdown with themed, scrollable choices and optional editing.

**Source:** [src/qml/ComboBox.qml](../../../src/qml/ComboBox.qml) · **Base:** `T.ComboBox` · **Kind:** QML component.

model/textRole/iconRole/currentIndex/currentText and activated select records; placeholderText handles no selection. Compact height 24/20, popup up to 12 rows. Async search, multi-select chips and arbitrary hierarchical picking are not built in.

**Declared members:** `small: bool`, `iconRole: string`, `placeholderText: string`, `tooltip: string`, `controlHeight: real` (ro), `edgePadding: real` (ro), `roleName: string` (ro).

**Source methods:** `textFor()`, `iconFor()`. See source for parameters and helper visibility.

**Object names:** `"comboPopup"`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## ColorField

Color swatch with optional editable hexadecimal value.

**Source:** [src/qml/ColorField.qml](../../../src/qml/ColorField.qml) · **Base:** `T.Control` · **Kind:** QML component.

color, showHex, small, error and tooltip; applyHex accepts short, RGB and alpha-first ARGB hex forms. colorEdited reports edits, pickRequested asks the app to open a chooser. Height 24/20. It is not a complete color picker or profile-aware color model.

**Declared members:** `color: color`, `showHex: bool`, `small: bool`, `error: bool`, `tooltip: string`, `controlHeight: real` (ro), `swatchSize: real` (ro), `edgePadding: real` (ro), `hexText: string` (ro).

**Declared signals:** `colorEdited(color color)`, `pickRequested()`.

**Source methods:** `applyHex()`. See source for parameters and helper visibility.

**Object names:** `"colorSwatch"`, `"colorHex"`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## ColorButton

Toolbar swatch button that opens a palette popover.

**Source:** [src/qml/ColorButton.qml](../../../src/qml/ColorButton.qml) · **Base:** `T.AbstractButton` · **Kind:** QML component.

color/colors/columns/small/ghost configure it; choosing updates color and emits colorSelected, while Custom emits customRequested. Use the host for a full picker. Swatches are QColor-like values without document profiles.

**Declared members:** `color: color`, `colors: var`, `columns: int`, `small: bool`, `ghost: bool`, `tooltip: string`, `controlHeight: real` (ro), `swatchSize: real` (ro).

**Declared signals:** `colorSelected(color color)`, `customRequested()`.

**Object names:** `"colorButtonSwatch"`, `"colorButtonPopover"`.

**Coverage pointers:** [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml).

## ColorSwatches

Grid of named color choices.

**Source:** [src/qml/ColorSwatches.qml](../../../src/qml/ColorSwatches.qml) · **Base:** `Item` · **Kind:** QML component.

colors accepts strings or name/color records; current marks a value; columns and swatchSize control layout. Each cell uses at least `Theme.size.controlSm` for its target (48px in Touch), with the authored swatch artwork centred inside it. selected(color) and customRequested report actions; swatches support keyboard activation. Large managed libraries, spot colors and persistent palettes require additional models.

**Declared members:** `colors: var`, `current: color`, `columns: int`, `customButton: bool`, `swatchSize: real`.

**Declared signals:** `selected(color color)`, `customRequested()`.

**Source methods:** `entry()`. See source for parameters and helper visibility.

**Object names:** `"swatchGrid"`, `"swatch_" + index`, `"swatchCustom"`.

**Coverage pointers:** [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml).
