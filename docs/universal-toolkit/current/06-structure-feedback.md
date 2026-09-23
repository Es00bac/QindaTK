<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Panels, rows, dialogs and feedback

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

## Island

Rounded floating control band for tools above a canvas.

**Source:** [src/qml/Island.qml](../../../src/qml/Island.qml) · **Base:** `Rectangle` · **Kind:** QML component.

Default content forms a centered Flex row; spacing and paddingH/paddingV set geometry using theme roles. Anchor over a work surface. It is an overlay composition, not a floating operating-system window or a tool-state manager.

**Declared members:** `content: alias`, `spacing: real`, `paddingH: real`, `paddingV: real`.

## Panel

Header and body chrome for an inspector or tool panel.

**Source:** [src/qml/Panel.qml](../../../src/qml/Panel.qml) · **Base:** `Item` · **Kind:** QML component.

title/iconName, header visibility, collapse/close/float capabilities, active, padding and color; actions is a header slot. collapseToggled/closeRequested/floatRequested report gestures. Header 24 px compact; body/header objectNames support tests. Placement becomes persistent only when integrated with DockModel.

**Declared members:** `content: alias`, `actions: alias`, `headerItem: Item` (ro), `bodyItem: Item` (ro), `title: string`, `iconName: string`, `grip: bool`, `headerVisible: bool`, `collapsible: bool`, `collapsed: bool`, `closable: bool`, `floatable: bool`, `floating: bool`, `active: bool`, `padding: real`, `color: color`.

**Declared signals:** `collapseToggled(bool collapsed)`, `closeRequested()`, `floatRequested()`.

**Object names:** `"panelHeader"`, `"panelTitle"`, `"panelCollapse"`, `"panelFloat"`, `"panelClose"`, `"panelBody"`.

**Coverage pointers:** [tst_smoke.qml](../../../tests/qml/tst_smoke.qml).

## SectionHeader

Section title, count and trailing actions with optional disclosure.

**Source:** [src/qml/SectionHeader.qml](../../../src/qml/SectionHeader.qml) · **Base:** `Item` · **Kind:** QML component.

title/count/collapsible/collapsed and default trailing slot; row is 22 px compact. Bind the associated body's visibility to collapsed. It changes local collapsed state but does not create a document outline tree.

**Declared members:** `title: string`, `count: string`, `collapsible: bool`, `collapsed: bool`, `trailing: alias`.

**Object names:** `"sectionTitle"`.

## PropertyRow

Label/editor row for property inspectors.

**Source:** [src/qml/PropertyRow.qml](../../../src/qml/PropertyRow.qml) · **Base:** `Item` · **Kind:** QML component.

labelWidth, label, hint, alignTop and dense align arbitrary editors in the default slot; editorItem exposes the host. Minimum compact row uses size.row. Mixed selections, reset-to-default and data binding validation need an inspector model.

**Declared members:** `editor: alias`, `editorItem: Item` (ro), `label: string`, `labelWidth: real`, `hint: string`, `alignTop: bool`, `dense: bool`, `minRowHeight: real` (ro).

**Object names:** `"propertyLabel"`, `"propertyEditor"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## PropertyGroup

Collapsible inspector group composed of a header and rows.

**Source:** [src/qml/PropertyGroup.qml](../../../src/qml/PropertyGroup.qml) · **Base:** `Item` · **Kind:** QML component.

title/count/collapsible/collapsed, divider and spacing organize a column. Default rows are QML content; no schema-driven property discovery or transaction management is provided.

**Declared members:** `rows: alias`, `title: string`, `count: string`, `collapsible: bool`, `collapsed: bool`, `divider: bool`, `spacing: real`.

**Object names:** `"groupHeader"`, `"groupBody"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## KeyValue

Read-only label/value pair aligned across one dense row.

**Source:** [src/qml/KeyValue.qml](../../../src/qml/KeyValue.qml) · **Base:** `Item` · **Kind:** QML component.

key/value, mono and accent configure presentation. Compact row 22 px; keyLabel and valueLabel are the testable parts. Values must already be formatted for locale and units.

**Declared members:** `key: string`, `value: string`, `mono: bool`, `accent: bool`.

**Object names:** `"keyLabel"`, `"valueLabel"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## ListRow

Template list delegate with optional icon, second line and accessory slots.

**Source:** [src/qml/ListRow.qml](../../../src/qml/ListRow.qml) · **Base:** `T.ItemDelegate` · **Kind:** QML component.

selected/active/small/indent and leading/trailing slots support dense lists; clicked, rightClicked and inherited doubleClicked route actions. Height 22, 28 for two lines, or 20 small. It is a delegate; the host owns virtualization and multi-selection.

**Declared members:** `secondaryText: string`, `iconName: string`, `selected: bool`, `active: bool`, `small: bool`, `indent: real`, `leading: alias`, `trailing: alias`, `leadingItem: Item` (ro), `trailingItem: Item` (ro), `twoLine: bool` (ro).

**Declared signals:** `rightClicked(var point)`.

**Object names:** `"rowText"`, `"rowSecondary"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## TreeRow

ListRow with indentation, expansion affordance and optional rename editor.

**Source:** [src/qml/TreeRow.qml](../../../src/qml/TreeRow.qml) · **Base:** `Tk.ListRow` · **Kind:** QML component.

depth/expandable/expanded/ editable; toggle emits expansionToggled; startEditing/cancelEditing support renamed(text). The owner updates text and hierarchy. It does not contain a tree model, child loading, reordering or tree-wide keyboard navigation.

**Declared members:** `depth: int`, `expandable: bool`, `expanded: bool`, `editable: bool`, `editing: bool` (ro).

**Declared signals:** `expansionToggled(bool expanded)`, `renamed(string text)`.

**Source methods:** `toggle()`, `startEditing()`, `cancelEditing()`. See source for parameters and helper visibility.

**Object names:** `"treeEditor"`, `"treeChevron"`.

**Coverage pointers:** [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## Card

Raised Box surface for a summary, chooser tile or inspector block.

**Source:** [src/qml/Card.qml](../../../src/qml/Card.qml) · **Base:** `Tk.Box` · **Kind:** QML component.

selected accents the border; inherited interactive supplies pointer actions, with theme padding and radius. Consumers need keyboard semantics when using it as a selectable action tile.

**Declared members:** `selected: bool`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## EmptyState

Centered explanation and optional actions for an empty area.

**Source:** [src/qml/EmptyState.qml](../../../src/qml/EmptyState.qml) · **Base:** `Item` · **Kind:** QML component.

title, text, iconName, maxTextWidth and default actions slot produce a useful empty panel. The host decides whether empty, loading, permission failure or filter-no-match is the correct state.

**Declared members:** `text: string`, `title: string`, `iconName: string`, `maxTextWidth: real`, `actions: alias`.

**Object names:** `"emptyTitle"`, `"emptyText"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## Notice

Inline contextual message with status icon and optional actions.

**Source:** [src/qml/Notice.qml](../../../src/qml/Notice.qml) · **Base:** `Tk.Box` · **Kind:** QML component.

variant info/success/warning/danger, title/text, iconName, dismissible and actions slot. dismissed reports the close gesture. Long-lived notifications, announcements and retry tasks need an application-level service.

**Declared members:** `text: string`, `title: string`, `variant: string`, `iconName: string`, `dismissible: bool`, `actions: alias`, `statusColor: color` (ro), `statusIcon: string` (ro).

**Declared signals:** `dismissed()`.

**Object names:** `"noticeTitle"`, `"noticeText"`, `"noticeDismiss"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## Popover

Non-modal anchored popup clamped within the window overlay.

**Source:** [src/qml/Popover.qml](../../../src/qml/Popover.qml) · **Base:** `T.Popup` · **Kind:** QML component.

anchorItem, placement, gap and optional arrow position the popup; open/close/reposition expose control. Escape/outside press closes. Placement is tied to the host overlay, not a cross-screen native popup policy.

**Declared members:** `anchorItem: Item`, `placement: string`, `arrow: bool`, `gap: real`.

**Source methods:** `reposition()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## Dialog

Modal dialog with bounded scrolling body and explicit action footer.

**Source:** [src/qml/Dialog.qml](../../../src/qml/Dialog.qml) · **Base:** `T.Dialog` · **Kind:** QML component.

title/subtitle, dialogWidth, primaryText/secondaryText/tertiaryText, primaryEnabled and destructive configure it. accepted/rejected/tertiary route decisions; header/footer can be replaced. standardButtons is intentionally unsupported by the custom footer.

**Declared members:** `content: alias`, `subtitle: string`, `primaryText: string`, `secondaryText: string`, `destructive: bool`, `dialogWidth: real`, `tertiaryText: string`, `primaryEnabled: bool`, `naturalHeight: real` (ro).

**Declared signals:** `tertiary()`.

**Object names:** `"dialogHeader"`, `"dialogTitle"`, `"dialogBody"`, `"dialogFooter"`, `"dialogTertiary"`, `"dialogSecondary"`, `"dialogPrimary"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## MessageDialog

Dialog specialization for an informational or decision message.

**Source:** [src/qml/MessageDialog.qml](../../../src/qml/MessageDialog.qml) · **Base:** `Tk.Dialog` · **Kind:** QML component.

text and info/question/warning/danger variant set content; iconName can override the icon. Supports three action outcomes, suitable for Save/Cancel/Discard. The app implements saving and handles failure before closing a document.

**Declared members:** `text: string`, `variant: string`, `iconName: string`, `iconColor: color` (ro).

**Object names:** `"messageIcon"`, `"messageText"`.

**Coverage pointers:** [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml).

## PromptDialog

Dialog requesting one line of text.

**Source:** [src/qml/PromptDialog.qml](../../../src/qml/PromptDialog.qml) · **Base:** `Tk.Dialog` · **Kind:** QML component.

label/text/placeholderText and acceptEmpty configure the field; acceptable gates the primary action. Focus selects text on open; Enter accepts and Escape rejects. Name uniqueness or asynchronous validation remains application work.

**Declared members:** `label: string`, `text: alias`, `placeholderText: alias`, `acceptEmpty: bool`, `acceptable: bool` (ro).

**Object names:** `"promptLabel"`, `"promptField"`.

**Coverage pointers:** [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml).

## ToolTip

Themed tooltip popup using caption text.

**Source:** [src/qml/ToolTip.qml](../../../src/qml/ToolTip.qml) · **Base:** `T.ToolTip` · **Kind:** QML component.

Instantiate Tk.ToolTip as a child with text/visible bound to the owner. The inherited delay defaults to the implementation's 600 ms. Do not use the attached form: it can instantiate a Qt Controls style tooltip. The leading source comment recommending attached use is stale.

**Declared members:** No additional local properties; use the inherited API.
