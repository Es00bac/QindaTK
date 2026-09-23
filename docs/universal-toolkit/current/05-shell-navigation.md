<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Application shell, menus and navigation

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

## AppWindow

Document application window with menu, toolbar, content, find and status bands.

**Source:** [src/qml/AppWindow.qml](../../../src/qml/AppWindow.qml) · **Base:** `T.ApplicationWindow` · **Kind:** QML component.

Default content occupies the remaining area; toolBars is a list, findBar/statusBar are slots, visibility flags collapse the corresponding bands. A palette bridge themes descendants. This is one application window, not a document manager or multi-window session coordinator.

**Declared members:** `content: alias`, `toolBars: alias`, `findBar: alias`, `statusBar: alias`, `contentHostItem: Item` (ro), `toolBarsVisible: bool`, `statusBarVisible: bool`.

**Source methods:** `seatContent()`. See source for parameters and helper visibility.

**Object names:** `"appHeader"`, `"appToolBars"`, `"appFooter"`, `"appFindBar"`, `"appStatusBar"`, `"appContent"`.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp).

## ToolBar

Horizontal command band with optional wrapping.

**Source:** [src/qml/ToolBar.qml](../../../src/qml/ToolBar.qml) · **Base:** `Tk.Box` · **Kind:** QML component.

Default items are laid out in a Flex; compact chooses 28 instead of 36 px, gap/edge/wrap control arrangement, rowItem exposes the row. Wrapping increases height. Customizable command groups, ribbon layouts and overflow menus need additional behavior.

**Declared members:** `items: alias`, `rowItem: Item` (ro), `compact: bool`, `gap: real`, `edge: string`, `wrap: bool`, `baseHeight: real` (ro).

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml), [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml).

## ToolSeparator

Inset vertical rule between toolbar groups.

**Source:** [src/qml/ToolSeparator.qml](../../../src/qml/ToolSeparator.qml) · **Base:** `Item` · **Kind:** QML component.

inset and margin keep the rule aligned to the surrounding bar; its Item wrapper stretches with the row. It carries no interactive or document state.

**Declared members:** `inset: real`, `margin: real`.

**Coverage pointers:** [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## MenuBar

Themed template menu bar.

**Source:** [src/qml/MenuBar.qml](../../../src/qml/MenuBar.qml) · **Base:** `T.MenuBar` · **Kind:** QML component.

Children are Menu objects with titles; templates provide menu activation and mnemonic handling. Compact height 22 px. This QML bar is not evidence of native operating-system menu integration on every platform.

**Declared members:** No additional local properties; use the inherited API.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## Menu

Cascading template menu with bounded height and scrolling.

**Source:** [src/qml/Menu.qml](../../../src/qml/Menu.qml) · **Base:** `T.Menu` · **Kind:** QML component.

title, minWidth and popup/open/close behavior integrate with MenuBar and context menus. MenuItem/MenuSeparator supply rows; long menus scroll within the output. An application command registry and contextual command policy are not supplied.

**Declared members:** `minWidth: real`, `naturalHeight: real` (ro), `verticalRoom: real` (ro).

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## MenuItem

Command row with icon, check/radio marker and shortcut display.

**Source:** [src/qml/MenuItem.qml](../../../src/qml/MenuItem.qml) · **Base:** `T.MenuItem` · **Kind:** QML component.

text/checkable/checked/triggered come from the template; iconName, shortcut, radio and danger extend visuals. shortcut is a display string, not an automatically installed key binding; radio group exclusivity is the owner's responsibility.

**Declared members:** `shortcut: string`, `iconName: string`, `danger: bool`, `radio: bool`.

**Object names:** `"menuItemText"`.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## MenuSeparator

Themed divider between menu groups.

**Source:** [src/qml/MenuSeparator.qml](../../../src/qml/MenuSeparator.qml) · **Base:** `T.MenuSeparator` · **Kind:** QML component.

Based on T.MenuSeparator with theme spacing and border role. The containing Menu owns navigation and geometry.

**Declared members:** No additional local properties; use the inherited API.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## TabStrip

Dense index-based tab selector with optional close actions.

**Source:** [src/qml/TabStrip.qml](../../../src/qml/TabStrip.qml) · **Base:** `Item` · **Kind:** QML component.

model supports strings or text/iconName/closable/tooltip/enabled records; style underline/pill, stretch/small and global closable alter presentation. Tabs are keyboard-focusable with ←/→ and Home/End navigation over enabled entries. Accessible PageTab items report selected state; disabled entries and their close buttons are inert. Direct currentIndex assignments are silent; tabActivated/tabClosed report user actions. tabItem retrieves a delegate. Closing does not manage documents, save prompts or page lifetime.

**Declared members:** `model: var`, `currentIndex: int`, `style: string`, `closable: bool`, `small: bool`, `stretch: bool`, `count: int` (ro).

**Declared signals:** `tabActivated(int index)`, `tabClosed(int index)`.

**Source methods:** `entry()`, `tabItem()`, `activate()`. See source for parameters and helper visibility.

**Object names:** `"tab_" + index`, `"tabClose_" + tab.index`.

**Coverage pointers:** [tst_selection_composites.qml](../../../tests/qml/tst_selection_composites.qml).

## StatusBar

Bottom band containing horizontally arranged fields.

**Source:** [src/qml/StatusBar.qml](../../../src/qml/StatusBar.qml) · **Base:** `Tk.Box` · **Kind:** QML component.

Default fields, gap and Spacer support left/right groups; compact height 22 px. Use StatusField for clickable text. Status calculation, background jobs and notification history remain external.

**Declared members:** `fields: alias`, `gap: real`.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## StatusField

Clickable status entry with optional icon and tooltip.

**Source:** [src/qml/StatusField.qml](../../../src/qml/StatusField.qml) · **Base:** `Item` · **Kind:** QML component.

text, iconName, muted and hovered control display; clicked lets the host reveal details. A single status field does not provide progress task ownership or a notification center.

**Declared members:** `text: string`, `iconName: string`, `muted: bool`, `tooltip: string`, `hovered: bool` (ro).

**Declared signals:** `clicked()`.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## CommandPalette

Searchable popup of grouped commands.

**Source:** [src/qml/CommandPalette.qml](../../../src/qml/CommandPalette.qml) · **Base:** `T.Popup` · **Kind:** QML component.

commands are id/label/shortcut/section/iconName/keywords records; case-insensitive substring filtering builds rows. Arrow keys/Enter or pointer activation emit activated(id). The app opens it and dispatches IDs; it does not implement a global command registry, shortcut resolver or fuzzy search.

**Declared members:** `commands: var`, `placeholderText: string`, `maxVisible: int`, `filterText: alias`, `currentRow: alias` (ro), `resultCount: int` (ro), `rows: var`.

**Declared signals:** `activated(string id)`.

**Source methods:** `rebuild()`, `firstSelectable()`, `move()`, `activateCurrent()`. See source for parameters and helper visibility.

**Object names:** `"paletteInput"`, `"paletteList"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).
