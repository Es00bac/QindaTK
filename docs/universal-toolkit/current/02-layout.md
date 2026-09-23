<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Layout, scrolling and geometry

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

## Flex

C++ flexbox layout for dense rows, columns and wrapping tool strips.

**Source:** [src/core/flex.h](../../../src/core/flex.h) · **Base:** `QQuickItem` · **Kind:** C++ type.

direction, layoutDirection, wrap, justify, align, alignContent, gaps and padding control layout; contentWidth/Height and lineCount report results. Children use Tk.Flex attached sizing. Flex owns its implicit size. Baseline aligns mixed text/control items in horizontal rows using Qt item baselines, with a bottom-edge fallback for plain Items; columns currently treat Baseline as Start. Explicit Qt.RightToLeft layoutDirection mirrors horizontal placement within physical padding; LayoutMirroring alone does not affect custom C++ positions.

**Declared members:** `direction: Direction`, `wrap: WrapMode`, `justify: Alignment`, `align: Alignment`, `alignContent: Alignment`, `gap: qreal`, `rowGap: qreal`, `columnGap: qreal`, `padding: qreal`, `paddingLeft: qreal`, `paddingTop: qreal`, `paddingRight: qreal`, `paddingBottom: qreal`, `contentWidth: qreal` (ro), `contentHeight: qreal` (ro), `lineCount: int` (ro).

**Declared signals:** `layoutPropertyChanged()`, `contentSizeChanged()`.

**Source methods:** `relayout()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_layout.cpp](../../../tests/cpp/tst_layout.cpp), [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml), [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml).

## FlexAttached

Anonymous per-child sizing and ordering contract for Flex.

**Source:** [src/core/flex.h](../../../src/core/flex.h) · **Base:** `LayoutAttachedBase` · **Kind:** Anonymous C++ helper.

Use Tk.Flex.grow/shrink/basis or flex. flex sets grow=n, shrink=1, basis=0; grow alone retains content basis. Inherited min/max width/height, order, ignore and alignSelf come from LayoutAttachedBase. Do not instantiate this helper directly.

**Declared members:** `grow: qreal`, `shrink: qreal`, `basis: qreal`, `flex: qreal`.

## Grid

C++ grid layout with track strings, named areas and automatic placement.

**Source:** [src/core/grid.h](../../../src/core/grid.h) · **Base:** `QQuickItem` · **Kind:** C++ type.

columns/rows accept fixed, percent, auto, fr, minmax and numeric repeat tracks; autoFlow supports dense placement. Child indices are zero based. 1fr means minmax(0,1fr); request minmax(auto,1fr) for a content floor. error exposes parsing failures. This is a layout grid, not an editable data grid.

**Declared members:** `columns: QString`, `rows: QString`, `autoColumns: QString`, `autoRows: QString`, `autoFlow: Flow`, `areas: QStringList`, `justifyItems: Alignment`, `alignItems: Alignment`, `gap: qreal`, `rowGap: qreal`, `columnGap: qreal`, `padding: qreal`, `paddingLeft: qreal`, `paddingTop: qreal`, `paddingRight: qreal`, `paddingBottom: qreal`, `columnCount: int` (ro), `rowCount: int` (ro), `contentWidth: qreal` (ro), `contentHeight: qreal` (ro), `error: QString` (ro).

**Declared signals:** `layoutPropertyChanged()`, `contentSizeChanged()`, `errorChanged()`.

**Source methods:** `relayout()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_layout.cpp](../../../tests/cpp/tst_layout.cpp), [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## GridAttached

Anonymous per-child placement contract for Grid.

**Source:** [src/core/grid.h](../../../src/core/grid.h) · **Base:** `LayoutAttachedBase` · **Kind:** Anonymous C++ helper.

Tk.Grid.row/column, rowSpan/columnSpan and area place an item; justifySelf and inherited alignSelf control alignment. Shared min/max sizes, order and ignore also apply. Direct helper construction is not the consumer API.

**Declared members:** `row: int`, `column: int`, `rowSpan: int`, `columnSpan: int`, `area: QString`, `justifySelf: int`.

## Stack

C++ container for overlapping surfaces and absolute placement.

**Source:** [src/core/stack.h](../../../src/core/stack.h) · **Base:** `QQuickItem` · **Kind:** C++ type.

Padding establishes the content box; children normally fill it and paint in declaration/z order. Tk.Stack attached insets, centers and fill override placement. `layoutDirection` defaults to `Qt.LeftToRight`; `Qt.RightToLeft` mirrors computed child x positions inside the content box while padding values remain physical. Bind to `Qt.application.layoutDirection` to follow the application locale. Stack owns its implicit size; it does not manage navigable pages or navigation history.

**Declared members:** `padding: qreal`, `paddingLeft: qreal`, `paddingTop: qreal`, `paddingRight: qreal`, `paddingBottom: qreal`, `layoutDirection: Qt::LayoutDirection`.

**Declared signals:** `layoutPropertyChanged()`.

**Source methods:** `relayout()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_layout.cpp](../../../tests/cpp/tst_layout.cpp).

## StackAttached

Anonymous per-child inset and alignment contract for Stack.

**Source:** [src/core/stack.h](../../../src/core/stack.h) · **Base:** `LayoutAttachedBase` · **Kind:** Anonymous C++ helper.

Tk.Stack.top/left/right/bottom/inset, centerX/centerY and fill govern geometry. Unset fill follows whether insets exist; fill=false uses natural size. Shared bounds/order/ignore/alignSelf are inherited from LayoutAttachedBase.

**Declared members:** `top: qreal`, `left: qreal`, `right: qreal`, `bottom: qreal`, `inset: qreal`, `fill: bool`, `centerX: bool`, `centerY: bool`.

## Box

CSS-like background, padding and per-side border wrapper.

**Source:** [src/qml/Box.qml](../../../src/qml/Box.qml) · **Base:** `Item` · **Kind:** QML component.

Default content is hosted in contentItem; fill seats a single unanchored child while respecting explicit geometry. Uniform borders honor radius; individual edge rectangles do not have rounded corners. interactive enables clicked/rightClicked/doubleClicked; it is not a template button with a complete keyboard action contract.

**Declared members:** `content: alias`, `contentItem: Item` (ro), `color: color`, `borderColor: color`, `borderWidth: real`, `borderLeft: real`, `borderTop: real`, `borderRight: real`, `borderBottom: real`, `radius: real`, `padding: real`, `paddingLeft: real`, `paddingTop: real`, `paddingRight: real`, `paddingBottom: real`, `fill: bool`, `clipContent: bool`, `interactive: bool`, `hovered: bool`, `pressed: alias` (ro), `perSide: bool` (ro), `edgeLeft: real` (ro), `edgeTop: real` (ro), `edgeRight: real` (ro), `edgeBottom: real` (ro), `insetLeft: real` (ro), `insetTop: real` (ro), `insetRight: real` (ro), `insetBottom: real` (ro).

**Declared signals:** `clicked(var eventPoint)`, `rightClicked(var eventPoint)`, `doubleClicked(var eventPoint)`.

**Source methods:** `maxImplicit()`, `seat()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_app_window.cpp](../../../tests/cpp/tst_app_window.cpp), [tst_layout.cpp](../../../tests/cpp/tst_layout.cpp), [tst_smoke.qml](../../../tests/qml/tst_smoke.qml).

## Scroll

Clipped scrolling wrapper with independent overflow policies.

**Source:** [src/qml/Scroll.qml](../../../src/qml/Scroll.qml) · **Base:** `Item` · **Kind:** QML component.

overflowX defaults Hidden and overflowY Auto; Always forces the corresponding overlay scrollbar. contentX/Y and the exposed flickable provide scroll control. ensureVisible, scrollToTop and scrollToBottom are helpers. It does not virtualize arbitrary children; choose a model view for large collections.

**Declared members:** `content: alias`, `contentItem: Item` (ro), `flickable: alias` (ro), `overflowX: int`, `overflowY: int`, `padding: real`, `contentX: alias`, `contentY: alias`, `contentWidth: alias` (ro), `contentHeight: alias` (ro), `viewportWidth: real` (ro), `viewportHeight: real` (ro), `scrollableX: bool` (ro), `scrollableY: bool` (ro), `interactive: bool`.

**Source methods:** `ensureVisible()`, `scrollToTop()`, `scrollToBottom()`, `maxImplicit()`, `seat()`. See source for parameters and helper visibility.

## ScrollBar

Themed Qt template scrollbar used by Scroll and other flickables.

**Source:** [src/qml/ScrollBar.qml](../../../src/qml/ScrollBar.qml) · **Base:** `T.ScrollBar` · **Kind:** QML component.

The template owns position, size, orientation and interaction. Thin overlay visuals use theme roles. Attach through T.ScrollBar with Tk.ScrollBar visuals; scrollbar appearance does not determine the host's content or selection model.

**Declared members:** No additional local properties; use the inherited API.

## Spacer

Flexible blank space within a Flex row or column.

**Source:** [src/qml/Spacer.qml](../../../src/qml/Spacer.qml) · **Base:** `Item` · **Kind:** QML component.

With size < 0 (the default), it grows and shrinks from zero natural size; a nonnegative size makes a fixed spacer with no growth/shrink. Use to push status fields or toolbar groups apart. It contains no application state or interaction.

**Declared members:** `size: real`.

## Splitter

Themed Qt template SplitView for user-resizable adjacent panes.

**Source:** [src/qml/Splitter.qml](../../../src/qml/Splitter.qml) · **Base:** `T.SplitView` · **Kind:** QML component.

handleSize uses size.seam; orientation and saveState/restoreState are inherited. Children use T.SplitView preferred/minimum/fill attached properties. It is distinct from DockModel and does not make a pane dockable.

**Declared members:** `handleSize: real`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## LayoutInfo

C++ singleton exposing item geometry intent that QML cannot inspect directly.

**Source:** [src/core/layout_info.h](../../../src/core/layout_info.h) · **Base:** `QObject` · **Kind:** C++ singleton.

hasAnchors, hasExplicitWidth and hasExplicitHeight support Box/Scroll/Viewport seating; describe assists diagnostics. Uses Qt Quick private API. It is an implementation aid, not an app layout persistence service.

**Declared members:** No additional local properties; use the inherited API.

**Source methods:** `hasAnchors()`, `hasExplicitWidth()`, `hasExplicitHeight()`, `describe()`. See source for parameters and helper visibility.

## Divider

Passive horizontal or vertical rule between interface groups.

**Source:** [src/qml/Divider.qml](../../../src/qml/Divider.qml) · **Base:** `Rectangle` · **Kind:** QML component.

vertical chooses orientation, thickness and inset control geometry. Defaults follow theme border roles; Flex attached sizing lets the rule stretch across the other axis. It has no resize gesture; use Splitter or DockDivider for interactive seams.

**Declared members:** `vertical: bool`, `thickness: real`, `inset: real`.
