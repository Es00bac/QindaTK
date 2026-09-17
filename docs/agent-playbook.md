<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Agent playbook

How a coding agent builds a screen with QindaTK and proves it works
without looking at a monitor. Read [../AGENTS.md](../AGENTS.md) first; this
page is the working procedure.

## The loop

1. **Read the contract.** [controls.md](controls.md) for the control you
   need (its properties, sizes, objectNames); [layout.md](layout.md) for
   the container rules; [theming.md](theming.md) for the role that carries
   the colour you are about to write.
2. **Write complete QML.** `import QindaTK as Tk`; toolkit types qualified;
   no colour/size literals; sizes through `implicitWidth`/`implicitHeight`
   or attached constraints, never `width`/`height` inside a container.
3. **Configure and build** (once per new file; the QML list is globbed):
   ```
   cmake --preset dev && cmake --build --preset dev
   ```
   For a scratch screen outside the module you do not need to rebuild at
   all — `qtk-preview` loads any `.qml` file that imports the built module.
4. **Instantiate.** `qtk-preview screen.qml --check` — exit 0 or the QML
   error on stderr (file:line:column). Fix before anything else.
5. **Measure.** `qtk-preview screen.qml --dump --size 1280x800` and read
   the geometry (below). Compare against the numbers the contract gives:
   header 24, row 22, control 24, chip 32.
6. **Look.** `qtk-preview screen.qml --grab shot.png --size 1280x800`, then
   open `shot.png` with the Read tool. Check alignment, elision, spacing,
   overlap, that nothing is 0×0 or off-canvas.
7. **Vary.** Repeat 5–6 with `--density comfortable` and `--theme
   sloom-light`; a control must survive both.
8. **Assert.** Put the geometry that matters into a test:
   `tests/qml/tst_*.qml` (QtQuickTest, `createTemporaryObject`, `compare`)
   or `tests/cpp/tst_layout.cpp` (inline QML scene, `QTRY_COMPARE`). Run
   `ctest --preset dev`.
9. **Document.** `docs/controls.md` row, `docs/catalog.json`
   (`python3 tools/scripts/gen_catalog.py`), a decision entry if the
   choice was cross-cutting.
10. **Report** with the dump lines that prove the claim and the PNG path.

## Reading a `--dump`

```
Panel#inspector  1036,4 240x792
  Box#panelHeader  0,0 240x24
    Overline#panelTitle  18,5 152x13  "INSPECTOR"
  Box#panelBody  0,24 240x768
```

`Type#objectName  x,y WxH  "text"`, indented by item depth, positions
relative to the parent, `hidden` appended when not visible. A `Flex`
named in your file shows up with its objectName; its children follow.
Things to check:

- **Every row has its contract height** (`22`, `24`, `32`) and rows do not
  overlap: each child's `y` ≥ previous `y` + height (+ gap).
- **Nothing is `0x0`** unless hidden or a Spacer; a `0x0` control means it
  is missing an `implicitWidth`/`implicitHeight` or its parent never got a
  size.
- **Stretch happened**: in a column `Flex`, children's widths equal the
  container's inner width; in a `Grid`, cells sum to the width minus gaps.
- **Text fits**: a `Label` narrower than its `implicitWidth` (in
  `--dump-json`) is eliding — intended for tables, a bug for a button
  caption.
- **Overlay geometry**: an `Island` should be its content size, not the
  canvas size (a filled Island means `Box` seated it — anchor it or set
  `fill: false`).

`--dump-json` adds `implicitWidth`/`implicitHeight` per item for scripts.

## Checklist for a dense layout

- Heights: is every control on the size ladder? Mixed 22/24/28 rows in one
  column look wrong; a `Flex` row with `align: Center` hides the mismatch.
- Alignment: labels and editors in one `Grid` with `alignItems: Center`;
  captions muted; numbers `Mono` and right-aligned in `KeyValue` rows.
- Elision: which text may elide (tables, tabs) and which must not
  (shortcuts, readouts → `Tk.Flex.shrink: 0`)?
- Minimums: side panels have `minWidth` in the dock definition; fields in
  a row have `Tk.Flex.minWidth` ≥ 60.
- Scrolling: the panel body scrolls (`Tk.Scroll`), the header does not; the
  scroll is a `Flex` child with `grow: 1; basis: 0; minHeight: 0`.
- Gaps: `xs` (2) inside a control group, `sm` (4) between rows, `md` (8)
  between blocks, `lg`/`xl` around a page.
- Tooltips on every icon-only control; `Accessible.role` on custom
  interactive items.
- Theme variants: no literal colours, nothing unreadable on `sloom-light`.

## Failure signatures

| You see | Cause | Fix |
| --- | --- | --- |
| `Type X unavailable` / `X is not a type` | missing `import QindaTK as Tk` or unqualified name | qualify with `Tk.`; check `qmldir` lists the file (re-run cmake) |
| `Binding loop detected for property "implicitHeight"` on a Flex/Grid | you bound `implicitHeight`/`implicitWidth` on a container | remove it; wrap in an `Item` for a fixed size (layout.md, Gotchas) |
| A child of a Flex/Grid ignores its `width:` | containers set child sizes from `implicitWidth` | set `implicitWidth`, or `Tk.Flex.basis` |
| Column children are all 0 wide | the `Flex` itself has no width (e.g. inside a plain `Item`) | give the container a size: anchors, a `Flex` slot with `grow`, or `Grid` |
| Island / overlay fills its Box | `Box` seated the single child because it had no anchors when created | give it anchors or `Tk.Stack` insets, or set `fill: false` on the Box |
| `Scroll` shows nothing / content 0 tall | the Scroll has no size from its parent | `Tk.Flex.grow: 1; Tk.Flex.basis: 0; Tk.Flex.minHeight: 0` in a column, or `anchors.fill` |
| Repeater delegates missing from the layout | delegates are fine; you gave the *Repeater* attached props | put `Tk.Flex.*` on the delegate |
| `QindaTK.Grid: bad track "…"` | track grammar | see layout.md → Track grammar |
| Wrapped text never grows its row | row sized before wrap | it converges on the next polish; if it does not, the Text lacks `wrapMode` or the column has no width |
| Text looks blurry at DPR 1 | `renderType` | use `Tk.Label` (native rendering) |
| Icon missing | name not in the set | `qtk-preview --list-icons`; aliases resolve old Lucide names; register custom paths with `Tk.Icons.registerIcon` |
| Colours wrong in light theme | literal colour or wrong role | replace with a role; `--theme sloom-light --grab` |
| `polish loop` warning | two containers set each other's implicit size through bindings | remove the manual binding; let implicit sizes flow up only |

## Adding things

**A theme role** — theming.md → "Adding a colour role" (header property,
role table, derivation, test).

**A control** — AGENTS.md → "How to add a control". Start from the nearest
existing file (`IconButton.qml` for buttons, `Panel.qml` for chrome,
`Scroll.qml` for containers). Every control: `Tk` prefix, template base,
`tooltip`, `Accessible.role`, objectNames for parts, no literals, a row in
controls.md, a test, the gallery.

**An icon** — built-in: add the Lucide name to `CURATED` in
`tools/scripts/gen_icons.py` and re-run it against a Lucide package
(`node_modules/lucide-react/dist/esm/icons`). Application icons at run
time: `Tk.Icons.registerIcon("brand", ["M3 3h18v18H3z"])` (24×24 viewBox,
stroked). A one-off SVG file: `Tk.Icon { source: "qrc:/x.svg"; tint: true }`.

**A dock panel** — docking.md.

## Verifying variants

```
P=build/dev/tools/preview/qtk-preview
$P s.qml --dump                       > /tmp/compact.txt
$P s.qml --dump --density comfortable > /tmp/comfortable.txt
diff /tmp/compact.txt /tmp/comfortable.txt      # sizes must move together
$P s.qml --grab light.png --theme sloom-light
$P --dump-theme --density comfortable | grep '"control"'
$P --list-qst && $P s.qml --qst qinda-dusk --grab dusk.png   # real desktop theme, when available
```

## Reporting

State the claim, then the evidence:

```
Panel body scrolls and header stays 24px:
  Box#panelHeader  0,0 240x24
  Box#panelBody    0,24 240x768
    Scroll         0,0 232x760
PNG: docs/screenshots/inspector.png (compact), inspector-comfortable.png
Tests: ctest --preset dev → 8/8 passed
```

Never report a visual result you have not grabbed and looked at.
