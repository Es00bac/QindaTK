<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Text, actions and indicators

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

## Label

Plain interface text with themed body typography and elision.

**Source:** [src/qml/Label.qml](../../../src/qml/Label.qml) · **Base:** `Text` · **Kind:** QML component.

muted, disabled, accent and mono select semantic presentation; inherited
text/wrapMode control content. Compact body is 12 px. `selectable: true`
shows a read-only `TextEdit` overlay (`selectableText`) with mouse/keyboard
selection, Ctrl+A and Ctrl+C. It does not claim initial focus. Ordinary Text
implicit sizing and the unselected appearance are preserved. Constrained
no-wrap labels select/copy the displayed elided string.

**Declared members:** `muted: bool`, `disabled: bool`, `accent: bool`, `mono: bool`, `selectable: bool`.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml), [tst_smoke.qml](../../../tests/qml/tst_smoke.qml).

## Caption

Small secondary text for hints, metadata and status.

**Source:** [src/qml/Caption.qml](../../../src/qml/Caption.qml) · **Base:** `Tk.Label` · **Kind:** QML component.

Inherits Label with caption size and muted color; compact default is 10 px. Supply text and use inherited wrapping/elision. It has no input or editing behavior.

**Declared members:** No additional local properties; use the inherited API.

## Overline

Uppercase tracked section text.

**Source:** [src/qml/Overline.qml](../../../src/qml/Overline.qml) · **Base:** `Tk.Label` · **Kind:** QML component.

title supplies text transformed to uppercase; wide increases letter spacing. Compact caption typography and semibold weight suit headers. It is a display label, not a collapsible section by itself.

**Declared members:** `title: string`, `wide: bool`.

**Coverage pointers:** [tst_smoke.qml](../../../tests/qml/tst_smoke.qml).

## Mono

Monospaced interface readout.

**Source:** [src/qml/Mono.qml](../../../src/qml/Mono.qml) · **Base:** `Tk.Label` · **Kind:** QML component.

Inherits Label with the mono family and small typography, 11 px in compact mode. Suitable for time displays and aligned values; formatting/parsing numeric units remains the caller's job.

**Declared members:** No additional local properties; use the inherited API.

## Heading

Themed heading label with three levels.

**Source:** [src/qml/Heading.qml](../../../src/qml/Heading.qml) · **Base:** `Tk.Label` · **Kind:** QML component.

level 1 selects display, 2 title, and 3 large typography, using semibold weight. These are interface roles; document paragraph styles and outline semantics are not provided.

**Declared members:** `level: int`.

## Button

Template-based text action with optional leading/trailing icons.

**Source:** [src/qml/Button.qml](../../../src/qml/Button.qml) · **Base:** `T.Button` · **Kind:** QML component.

Variants default/accent/ghost/outline/danger; compact heights 24, small 20, large 28. busy shows a Spinner and owns enabled through available. clicked and checked behavior come from the template; bind application capability through available when using busy.

**Declared members:** `iconName: string`, `trailingIconName: string`, `variant: string`, `small: bool`, `large: bool`, `busy: bool`, `available: bool`, `tooltip: string`, `controlHeight: real` (ro), `iconSize: real` (ro), `filled: bool` (ro), `fillColor: color` (ro), `outlineColor: color` (ro), `textColor: color` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## IconButton

Square action button with icon and hover/focus treatment.

**Source:** [src/qml/IconButton.qml](../../../src/qml/IconButton.qml) · **Base:** `T.AbstractButton` · **Kind:** QML component.

iconName, iconSize, small, danger, ghost and iconColor customize presentation; default compact size 24 or 20 small. Set tooltip to name the action accessibly. Template clicked/checkable/checked behavior is available.

**Declared members:** `icon_: string`, `iconName: alias`, `iconSize: real`, `small: bool`, `danger: bool`, `ghost: bool`, `tooltip: string`, `iconColor: color`.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml).

## Chip

Dense pill action for tools and prominent toolbar choices.

**Source:** [src/qml/Chip.qml](../../../src/qml/Chip.qml) · **Base:** `T.AbstractButton` · **Kind:** QML component.

active aliases checked; emphasis selects plain/tinted/solid; chevron indicates a chooser. Compact height 32, small 28, rounded island action 36. It selects/activates an action, but does not own a tool registry.

**Declared members:** `iconName: string`, `chevron: bool`, `active: alias`, `emphasis: string`, `rounded: bool`, `small: bool`, `tooltip: string`, `controlHeight: real` (ro), `iconSize: real` (ro), `lit: bool` (ro), `fillColor: color` (ro), `outlineColor: color` (ro), `labelColor: color` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## Segmented

Joined set of mutually exclusive choices.

**Source:** [src/qml/Segmented.qml](../../../src/qml/Segmented.qml) · **Base:** `Item` · **Kind:** QML component.

model accepts strings or text/iconName/value/tooltip records; currentIndex selects, currentValue reads the value, activated(index) reports user activation. small changes 24 to 20 px; stretch shares assigned width. Page visibility and business state remain external.

**Declared members:** `model: var`, `currentIndex: int`, `currentValue: var` (ro), `small: bool`, `stretch: bool`, `tooltip: string`, `enabled_: bool`, `controlHeight: real` (ro), `count: int` (ro).

**Declared signals:** `activated(int index)`.

**Source methods:** `entry()`. See source for parameters and helper visibility.

**Object names:** `"segment_" + index`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## PillSwitcher

Rounded page or workspace selector with optional icons and labels.

**Source:** [src/qml/PillSwitcher.qml](../../../src/qml/PillSwitcher.qml) · **Base:** `Item` · **Kind:** QML component.

model records may include accent and objectName; showLabels/showActiveMarker customize it. currentIndex/currentValue and activated(index,value) support state synchronization; arrow keys wrap. Height is 28 or 24 small. It does not create pages or persist workspaces.

**Declared members:** `model: var`, `currentIndex: int`, `small: bool`, `showLabels: bool`, `showActiveMarker: bool`, `tooltip: string`, `count: int` (ro), `currentValue: var` (ro), `itemHeight: real` (ro).

**Declared signals:** `activated(int index, var value)`.

**Source methods:** `entry()`, `activate()`, `move()`, `itemAt()`. See source for parameters and helper visibility.

**Object names:** `entry !== null && entry.objectName !== undefined && entry.objectName.length > 0 ? entry.objectName : "pillSwitcherItem_" + index`.

**Coverage pointers:** [tst_pill_switcher.qml](../../../tests/qml/tst_pill_switcher.qml).

## Badge

Compact count or status pill, optionally reduced to a dot.

**Source:** [src/qml/Badge.qml](../../../src/qml/Badge.qml) · **Base:** `Rectangle` · **Kind:** QML component.

text and default/accent/danger/success/warning/info variant encode status; dot selects the small indicator. Full pill is derived to 16 px in compact mode. Status should also be conveyed by text or accessible context rather than color alone.

**Declared members:** `text: string`, `variant: string`, `dot: bool`, `tooltip: string`, `tint: color` (ro), `pillHeight: real` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## ProgressBar

Task completion or indeterminate activity indicator.

**Source:** [src/qml/ProgressBar.qml](../../../src/qml/ProgressBar.qml) · **Base:** `T.ProgressBar` · **Kind:** QML component.

Inherits value/from/to/indeterminate; thin selects the thinner track. Reduced motion disables the moving indeterminate treatment. This is task progress, not a measured signal level.

**Declared members:** `thin: bool`, `tooltip: string`, `trackHeight: real` (ro).

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## Spinner

Small busy indicator based on a rotating icon.

**Source:** [src/qml/Spinner.qml](../../../src/qml/Spinner.qml) · **Base:** `T.BusyIndicator` · **Kind:** QML component.

size, running, color and tooltip configure it. Default icon-sized geometry; reduced motion leaves a static visible symbol. Cancellation, retries and task ownership require a separate job model.

**Declared members:** `size: real`, `color: color`, `tooltip: string`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## KeyCap

Visual rendering of a shortcut as separate keycaps.

**Source:** [src/qml/KeyCap.qml](../../../src/qml/KeyCap.qml) · **Base:** `Row` · **Kind:** QML component.

sequence is split on '+'; symbolic maps modifier names to glyphs. It draws the shortcut and does not register it. Current parsing is simple string splitting, not a complete QKeySequence parser; its Row positioner also departs from the repository layout rule.

**Declared members:** `sequence: string`, `symbolic: bool`, `keys: var` (ro).

**Object names:** `"keyCap"`, `"keyCapKey"`, `"keyCapLabel"`.

**Coverage pointers:** [tst_media.qml](../../../tests/qml/tst_media.qml).
