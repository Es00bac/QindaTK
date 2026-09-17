<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Layout reference

QindaTK has five layout primitives. Three are C++ containers that position
their children (`Flex`, `Grid`, `Stack`); two are QML boxes that give a
single child a padded, bordered, or scrolling frame (`Box`, `Scroll`).
Together they cover what CSS flexbox, grid, absolute positioning, the box
model and `overflow` cover. Import them with `import QindaTK as Tk`.

All containers share these rules:

- They read children's **implicit** sizes and attached properties; they set
  children's position and size. A child's own `width`/`height` binding is
  ignored by the container (and overwritten) — give sizes through
  `implicitWidth`/`implicitHeight` or the attached constraints.
- They set their **own implicit size** from the content plus padding. Never
  bind `implicitWidth`/`implicitHeight` on a `Flex`, `Grid` or `Stack`; the
  layout overwrites it. Wrap in an `Item` if a component must report a
  fixed size.
- Invisible children take no slot. `Repeater` objects are skipped (their
  delegates are laid out). `Tk.<Container>.ignore: true` excludes a child.
- Layout runs on the next polish after any change; `relayout()` runs it
  now. Layouts converge after a child's implicit size reacts to its new
  size (wrapping text, nested containers).
- Padding: `padding` plus `paddingLeft/Top/Right/Bottom`; a side set to
  `-1` (the default) inherits `padding`. Gaps: `gap` plus `rowGap` and
  `columnGap`; `-1` inherits `gap`.

The attached constraints exist on every container under its own name
(`Tk.Flex.minWidth`, `Tk.Grid.minWidth`, `Tk.Stack.minWidth`):

| Attached | Default | Meaning |
| --- | --- | --- |
| `minWidth`, `minHeight` | 0 | Lower bound of the size the container gives the child. **0 by default**, i.e. CSS `min-width: 0`: a child may shrink below its content. |
| `maxWidth`, `maxHeight` | unbounded | Upper bound. |
| `order` | 0 | Stable sort key; lower first. |
| `ignore` | false | Take no slot; keep own geometry. |
| `alignSelf` | `Auto` | Overrides the container's cross alignment (`Flex.align`) or `Grid.alignItems`. |

## Flex

`Tk.Flex` is a CSS flexbox container (Flexible Box Layout Level 1).

| Property | Values | Default | CSS |
| --- | --- | --- | --- |
| `direction` | `Tk.Flex.Row`, `RowReverse`, `Column`, `ColumnReverse` | `Row` | `flex-direction` |
| `wrap` | `Tk.Flex.NoWrap`, `Wrap`, `WrapReverse` | `NoWrap` | `flex-wrap` |
| `justify` | `Start`, `End`, `Center`, `SpaceBetween`, `SpaceAround`, `SpaceEvenly` | `Start` | `justify-content` |
| `align` | `Stretch`, `Start`, `End`, `Center` (`Baseline` acts as `Start`) | `Stretch` | `align-items` |
| `alignContent` | `Stretch`, `Start`, `End`, `Center`, `SpaceBetween`, `SpaceAround`, `SpaceEvenly` | `Stretch` | `align-content` (wrapped lines only) |
| `gap`, `rowGap`, `columnGap` | px | 0, -1, -1 | `gap`, `row-gap`, `column-gap` |
| `padding`, `padding{Left,Top,Right,Bottom}` | px | 0, -1… | `padding` |
| `contentWidth`, `contentHeight` | read-only | | laid-out extent incl. padding (for a Scroll) |
| `lineCount` | read-only | | number of flex lines |

Attached on children (`Tk.Flex.*`):

| Attached | Default | CSS |
| --- | --- | --- |
| `grow` | 0 | `flex-grow` |
| `shrink` | 1 | `flex-shrink` |
| `basis` | -1 (auto = implicit main size) | `flex-basis` (px) |
| `flex` | — | `flex: n` shorthand: sets `grow: n`, `shrink: 1`, `basis: 0`. Reading it returns `grow`. |
| `alignSelf` | `Auto` | `align-self` |
| `order`, `minWidth`… | see above | |

### Algorithm

1. **Hypothetical main size** of each child: `basis` if ≥ 0, else the
   implicit main size (`implicitWidth` for rows), clamped to `min`/`max`.
2. **Lines.** With `NoWrap`, or when the container's main size is not yet
   definite (0), everything is one line. With `Wrap`, children are packed
   greedily: a child starts a new line when hypothetical size + gap would
   exceed the inner main size.
3. **Free space** per line = inner main − Σ hypothetical − gaps. Positive
   free space is distributed by `grow`; negative by `shrink × hypothetical`
   (so bigger items give up more). Children with a zero factor, or already
   at the relevant bound, are frozen at their clamped hypothetical size.
   Then the CSS freeze loop runs: compute targets, clamp to `min`/`max`,
   sum the violations; if the sum is positive freeze the min-violators, if
   negative the max-violators, if zero freeze everything; repeat with the
   remaining free space.
4. **Indefinite main size** (the container has no width yet in a row):
   every child gets its hypothetical size and `justify` is not applied.
5. **Cross size.** A single line fills the container's inner cross size
   when that is definite; otherwise (and for every wrapped line) the line
   is as tall as its tallest child. With several lines and a definite cross
   size, `alignContent: Stretch` gives each line an equal share of the
   leftover; the other modes place the lines like `justify` places items.
6. **Per child**: `alignSelf` (or `align`) `Stretch` sets the cross size to
   the line's cross size clamped by the child's `min`/`max`; `Start`,
   `Center`, `End` keep the implicit cross size and position it.
7. **Justify** applies to leftover main space; negative leftover is treated
   as zero (items never move left of the padding to centre an overflow).
8. **Reverse directions** mirror every main position.
9. **Implicit size**: main = longest line's hypothetical content (Σ
   hypothetical + gaps) + padding; cross = Σ natural line heights + row
   gaps + padding. This is the max-content size; a wrapped container's
   implicit cross size therefore depends on its current width, like QML
   `Flow`.

`grow` versus `flex`: `Tk.Flex.grow: 1` keeps `basis` auto, so leftover is
added *on top of* each child's content size (CSS `flex: auto`). `Tk.Flex.flex:
1` sets `basis: 0`, so all flexed children share the whole space equally
regardless of content (CSS `flex: 1`). Two labels of different length end
up different widths with the first and identical widths with the second.

Because the default `minWidth` is 0, a row can shrink a child below its
text. Set `Tk.Flex.shrink: 0` on a child that must keep its size, or give
it a `minWidth`. Text children elide because `Tk.Label` sets
`elide: Text.ElideRight` and the container sets their width.

```qml
import QtQuick
import QindaTK as Tk

// A toolbar row: fixed chips, a flexible search field, right-aligned actions.
Tk.Flex {
    direction: Tk.Flex.Row
    align: Tk.Flex.Center
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm
    Tk.IconButton { iconName: "undo-2"; tooltip: "Undo" }
    Tk.IconButton { iconName: "redo-2"; tooltip: "Redo" }
    Tk.Divider { vertical: true; inset: 4 }
    Rectangle {                       // stands in for a field
        implicitHeight: Tk.Theme.size.control
        color: Tk.Theme.color.inputBg
        Tk.Flex.grow: 1               // takes the leftover
        Tk.Flex.minWidth: 80
    }
    Tk.Spacer {}                      // pushes what follows to the right
    Tk.Label { text: "185%"; muted: true; Tk.Flex.shrink: 0 }
}
```

```qml
// A wrapping tag cloud with equal line heights, top aligned.
Tk.Flex {
    width: 260
    wrap: Tk.Flex.Wrap
    gap: Tk.Theme.space.xs
    alignContent: Tk.Flex.Start
    Repeater {
        model: ["comic", "book", "print", "epub", "idml", "pdf/x-4"]
        Tk.Label { text: modelData; padding: 2 }
    }
}
```

## Grid

`Tk.Grid` is a CSS grid container (Grid Layout Level 1 with the
simplifications listed under *Deviations*).

| Property | Values | Default | CSS |
| --- | --- | --- | --- |
| `columns`, `rows` | track list string | `""` | `grid-template-columns/rows` |
| `autoColumns`, `autoRows` | track list string for implicit tracks | `"auto"` | `grid-auto-columns/rows` |
| `autoFlow` | `Tk.Grid.Row`, `Column`, `RowDense`, `ColumnDense` | `Row` | `grid-auto-flow` |
| `areas` | list of strings, one per row, `.` for an empty cell | `[]` | `grid-template-areas` |
| `justifyItems`, `alignItems` | `Stretch`, `Start`, `End`, `Center` | `Stretch` | `justify-items`, `align-items` |
| `gap`, `rowGap`, `columnGap`, `padding…` | px | | |
| `columnCount`, `rowCount` | read-only | | tracks after placement (explicit + implicit) |
| `contentWidth`, `contentHeight` | read-only | | laid-out extent incl. padding |
| `error` | read-only string | `""` | last track-list parse error (also printed as a warning) |

Attached on children (`Tk.Grid.*`): `row`, `column` (0-based, `-1` =
auto), `rowSpan`, `columnSpan` (≥ 1), `area` (a name from `areas`,
overrides the four above), `justifySelf`, `alignSelf` (`Auto` = the
container's), plus `minWidth`…, `order`, `ignore`.

### Track grammar

```
tracks   := track+                      separated by whitespace
track    := length | minmax(length, length) | repeat(count, tracks)
length   := <number> | <number>px | <number>% | <number>fr | auto
          | min-content | max-content     (both treated as auto)
```

| Spec | Meaning |
| --- | --- |
| `200`, `200px` | fixed 200px |
| `25%` | 25% of the container's inner size; behaves as `auto` while the container size is indefinite |
| `auto` | content-sized: as wide as the widest single-span item; stretches to fill leftover when the list has no `fr` track |
| `1fr` | **`minmax(0, 1fr)`**: a share of the leftover, may be narrower than its content |
| `minmax(120, 1fr)` | a share of the leftover, never below 120 |
| `minmax(auto, 1fr)` | a share of the leftover, never below content (CSS's own `1fr`) |
| `repeat(3, minmax(80, 1fr))` | three such tracks |

A parse error keeps the tracks parsed so far, sets `error`, and logs
`QindaTK.Grid: …`.

### Placement

1. Items with an `area` take that area's rectangle. Items with both `row`
   and `column` are placed there (spans may extend the grid).
2. Items locked on one axis (`row` set in row flow, `column` set in column
   flow) take the first free slot along the other axis.
3. Everything else is auto-placed by a cursor in `autoFlow` order. In row
   flow the column count is fixed (explicit tracks, the widest span or the
   largest explicit column index, at least 1) and rows grow; in column flow
   the reverse. `RowDense`/`ColumnDense` restart the cursor at the origin
   for each item, back-filling holes; the sparse modes never move the
   cursor backwards.
4. Tracks beyond the explicit list are implicit tracks and take
   `autoColumns`/`autoRows` (repeated cyclically).

### Sizing (two phases)

1. **Columns** are sized from the children's `implicitWidth` (clamped by
   `minWidth`/`maxWidth`), then every child's width is set: the cell width
   for `Stretch`, the implicit width otherwise.
2. **Rows** are then sized from the children's `implicitHeight`, which
   already reflects the new widths (wrapped `Text`, nested containers on
   their next polish), and positions are applied with `justifyItems`/
   `alignItems` and the per-item overrides.

Track sizing per axis: fixed and percent tracks start at their value;
`auto` tracks take the largest single-span contribution; items spanning
several non-`fr` tracks distribute their extra need equally over the
spanned content-sized tracks; when the axis is definite, `fr` tracks share
what is left after the other tracks and gaps (an `fr` track whose minimum
exceeds its share is fixed at the minimum and the rest re-shared); with no
`fr` track and leftover space, the content-sized tracks stretch equally.
When the axis is indefinite, `fr` tracks are as large as their content
and percent tracks act as `auto`; the container's implicit size is the
sum of these natural sizes plus gaps and padding.

### Deviations from CSS

- `Nfr` means `minmax(0, Nfr)` (decision D-004). Use `minmax(auto, 1fr)` for
  the CSS default.
- Items spanning an `fr` track do not enlarge it.
- Percent tracks resolve against the container's inner size only when it
  is definite.
- Row/column indices are 0-based (`Tk.Grid.column: 1` is the second track).

```qml
// Inspector rows: label column sized to content, editors take the rest.
Tk.Grid {
    columns: "auto 1fr"
    columnGap: Tk.Theme.space.sm
    rowGap: Tk.Theme.space.xs
    alignItems: Tk.Grid.Center
    Tk.Caption { text: "Width" }   Tk.Mono { text: "170" }
    Tk.Caption { text: "Height" }  Tk.Mono { text: "260" }
    Tk.Caption { text: "Bleed" }   Tk.Mono { text: "3.17" }
}
```

```qml
// A workspace frame with named areas and a fixed side column.
Tk.Grid {
    anchors.fill: parent
    columns: "220 1fr"
    rows: "auto 1fr 22"
    areas: ["tools tools", "side main", "status status"]
    Rectangle { Tk.Grid.area: "tools"; implicitHeight: 36; color: Tk.Theme.color.surface }
    Rectangle { Tk.Grid.area: "side"; color: Tk.Theme.color.panel }
    Rectangle { Tk.Grid.area: "main"; color: Tk.Theme.color.canvas }
    Rectangle { Tk.Grid.area: "status"; color: Tk.Theme.color.surface }
}
```

```qml
// Thumbnails: as many 96px columns as fit, packed densely.
Tk.Grid {
    width: 400
    columns: "repeat(4, minmax(0, 1fr))"
    autoRows: "72"
    autoFlow: Tk.Grid.RowDense
    gap: Tk.Theme.space.xs
    Repeater { model: 11; Rectangle { color: Tk.Theme.color.panelAlt } }
}
```

## Stack

`Tk.Stack` is `position: absolute` inside a `position: relative` parent.
Children stack in declaration order (later on top). Each child either fills
the padding box or places itself with insets.

| Attached (`Tk.Stack.*`) | Default | Meaning |
| --- | --- | --- |
| `top`, `left`, `right`, `bottom` | unset (NaN) | inset from the padding box edge; `inset: n` sets all four |
| `fill` | unset | `true` stretches on axes without insets; `false` keeps the implicit size. Unset means: fill when no inset is set, otherwise not. |
| `centerX`, `centerY` | false | centre on an axis that has no inset and no fill |
| `minWidth`… | | clamp the resulting size |

Per axis: two opposite insets stretch the child between them; one inset
positions it at its implicit size; no inset: fill (per the rule above),
else centre if asked, else position 0. The container's implicit size is the
largest child implicit size plus its insets, plus padding.

```qml
Tk.Stack {
    anchors.fill: parent
    Rectangle { color: Tk.Theme.color.canvas }                    // fills
    Tk.Island {                                                    // top-centre overlay
        Tk.Stack.top: Tk.Theme.space.sm; Tk.Stack.centerX: true
        Tk.Caption { text: "185%" }
    }
    Tk.Caption { text: "3 of 5"; Tk.Stack.right: 8; Tk.Stack.bottom: 6 }   // corner badge
    Rectangle {                                                    // bottom bar
        implicitHeight: 22; color: Tk.Theme.color.surface
        Tk.Stack.left: 0; Tk.Stack.right: 0; Tk.Stack.bottom: 0
    }
}
```

## Box

`Tk.Box` is the CSS box model for one element: background, border, radius,
padding, and a padding box (`contentItem`) that holds the children.

| Property | Default | Notes |
| --- | --- | --- |
| `color` | transparent | background |
| `borderColor`, `borderWidth` | `Theme.color.border`, 0 | uniform border, honours `radius` |
| `borderLeft/Top/Right/Bottom` | -1 | any side ≥ 0 switches to per-side edge rectangles (no radius on them); -1 inherits `borderWidth` |
| `radius` | 0 | |
| `padding`, `padding{Left,Top,Right,Bottom}` | 0, -1 | inside the border |
| `fill` | true | stretch a single unanchored child to the padding box |
| `clipContent` | false | clip to the padding box |
| `interactive`, `hovered`, `pressed` | false | enables hover/tap handlers; `clicked(point)`, `rightClicked(point)`, `doubleClicked(point)` |
| `contentItem` | read-only | the padding box; `insetLeft/Top/Right/Bottom` give border + padding per side |

Implicit size = content implicit size + padding + border. With one child,
the content size is that child's implicit size; with several it is their
`childrenRect`.

**Seating** (the single-child rule): after creation, and again whenever the
children change (deferred with `Qt.callLater` so a new child's own
properties are set first), a Box with exactly one child and `fill: true`
checks that child through `Tk.LayoutInfo`:

- child has anchors of its own → left alone;
- no explicit width and no explicit height → `anchors.fill` the padding box;
- explicit width only → anchored top/bottom (keeps its width);
- explicit height only → anchored left/right.

With several children nothing is seated: position them with anchors inside
`contentItem` (they are already parented there). Set `fill: false` to
switch seating off entirely.

```qml
// A card: sunken background, 1px border, padded content that wraps to width.
Tk.Box {
    width: 240
    color: Tk.Theme.color.panelAlt
    borderWidth: 1
    radius: Tk.Theme.radius.md
    padding: Tk.Theme.space.md
    Tk.Label {
        text: "Threaded text with OpenType features, hyphenation and drop caps."
        wrapMode: Text.Wrap
    }
}
```

```qml
// A panel body with only a right rule (CSS `border-r`).
Tk.Box { borderRight: 1; borderColor: Tk.Theme.color.divider; padding: 4; Tk.Label { text: "Layers" } }
```

## Scroll

`Tk.Scroll` is `overflow: auto`. It holds one child, sized to its implicit
size on a scrolling axis and to the viewport on a hidden axis.

| Property | Default | Notes |
| --- | --- | --- |
| `overflowX` | `Tk.Scroll.Hidden` | `Auto`, `Hidden`, `Always` |
| `overflowY` | `Tk.Scroll.Auto` | |
| `padding` | 0 | around the viewport |
| `interactive` | true | drag/flick |
| `contentX`, `contentY` | | scroll position |
| `contentWidth`, `contentHeight`, `viewportWidth`, `viewportHeight`, `scrollableX`, `scrollableY` | read-only | |
| `contentItem`, `flickable` | read-only | the content host and the Flickable |

Functions: `ensureVisible(item)`, `scrollToTop()`, `scrollToBottom()`.

The default (`overflowX: Hidden`) is the panel case: a column of rows wraps
to the width and scrolls vertically. A timeline or table sets `overflowX:
Tk.Scroll.Auto`. Scrollbars are overlays (`Tk.ScrollBar`) and take no
layout space. The single child is seated like in `Box` unless it has
anchors.

```qml
Tk.Scroll {
    anchors.fill: parent
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Repeater { model: 200; Tk.Label { text: "Row " + index } }
    }
}
```

## CSS property → QindaTK

| CSS | QindaTK |
| --- | --- |
| `display: flex` | `Tk.Flex {}` |
| `flex-direction: column` | `direction: Tk.Flex.Column` |
| `flex-wrap: wrap` | `wrap: Tk.Flex.Wrap` |
| `justify-content: space-between` | `justify: Tk.Flex.SpaceBetween` |
| `align-items: center` | `align: Tk.Flex.Center` |
| `gap: 8px` | `gap: 8` (or `Tk.Theme.space.md`) |
| `flex: 1` | `Tk.Flex.flex: 1` |
| `flex-grow: 1` | `Tk.Flex.grow: 1` |
| `flex-shrink: 0` | `Tk.Flex.shrink: 0` |
| `flex-basis: 200px` | `Tk.Flex.basis: 200` |
| `min-width: 0` | default |
| `min-width: 120px` | `Tk.Flex.minWidth: 120` |
| `order: 2` | `Tk.Flex.order: 2` |
| `align-self: flex-end` | `Tk.Flex.alignSelf: Tk.Flex.End` |
| `display: grid; grid-template-columns: auto 1fr` | `Tk.Grid { columns: "auto 1fr" }` |
| `grid-template-areas` | `areas: ["a b", "c d"]` + `Tk.Grid.area: "a"` |
| `grid-column: span 2` | `Tk.Grid.columnSpan: 2` |
| `grid-auto-flow: dense` | `autoFlow: Tk.Grid.RowDense` |
| `position: absolute; inset: 0` | child of `Tk.Stack` (default fill) or `anchors.fill: parent` |
| `top: 8px; right: 8px` | `Tk.Stack.top: 8; Tk.Stack.right: 8` |
| `padding: 4px 8px` | `padding: 4; paddingLeft: 8; paddingRight: 8` |
| `border: 1px solid` | `Tk.Box { borderWidth: 1 }` |
| `border-right: 1px solid` | `Tk.Box { borderRight: 1 }` |
| `border-radius: 6px` | `radius: Tk.Theme.radius.md` |
| `overflow-y: auto` | `Tk.Scroll {}` |
| `overflow: auto` | `Tk.Scroll { overflowX: Tk.Scroll.Auto }` |
| `overflow: hidden` | `Tk.Box { clipContent: true }` |
| `z-index` | declaration order in `Tk.Stack`, or `z` |

## Gotchas

- **Implicit size ownership.** `Tk.Flex { implicitHeight: 22 }` does not
  hold; the layout resets it. Use `Item { implicitHeight: 22; Tk.Flex {
  anchors.fill: parent } }`.
- **Children's `width`/`height` are ignored.** `Rectangle { width: 100 }` in
  a `Flex` gets the width the layout computes from `implicitWidth` (which is
  0 for a bare Rectangle). Set `implicitWidth: 100`.
- **Repeater in a container** works: the Repeater takes no slot, its
  delegates do. Do not give the Repeater itself attached properties.
- **Anchors versus layout.** A child that anchors inside a `Flex`/`Grid`
  fights the layout. Anchor only inside `Box.contentItem`, `Stack`, or an
  `Item` you own.
- **Binding loops.** `width: parent.width` on a container child is
  redundant and can loop; let the container set it. A `Box` whose height
  binds to its own content's height while the content anchors to the Box
  loops too — bind the content's size to the Box, not the reverse.
- **Text in a row shrinks.** A `Label` in a row with `min-width: 0`
  semantics elides when space runs out; set `Tk.Flex.shrink: 0` for text
  that must stay whole (a readout, a shortcut hint).
- **`1fr` shrinks below content.** Intentional; use `minmax(auto, 1fr)`
  where content must win.
- **A `Grid` with no `columns`** in row flow is a single column (like a
  block); give it tracks or `autoFlow: Tk.Grid.Column` for a row.
- **`Scroll` needs a size.** Like any viewport it must be given width and
  height by its parent (anchors, a `Flex` slot with `Tk.Flex.grow: 1`, a
  `Grid` cell); its implicit size is only its content's.


Every container also accepts `paddingLeft`, `paddingTop`, `paddingRight` and `paddingBottom` (−1, the default, inherits `padding`).
