<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# CSS / Tailwind → QindaTK cheat sheet

The idioms Sloom Studio's components use, and the QindaTK equivalent.
`Tk` is `import QindaTK as Tk`. Numbers follow Tailwind's 4px unit:
`-1` = 4px, `-2` = 8px, `-0.5` = 2px, `-1.5` = 6px.

## Layout

| CSS / Tailwind | QindaTK |
| --- | --- |
| `flex` | `Tk.Flex { }` (row by default) |
| `flex flex-col` | `Tk.Flex { direction: Tk.Flex.Column }` |
| `flex-row-reverse` | `direction: Tk.Flex.RowReverse` |
| `flex-wrap` | `wrap: Tk.Flex.Wrap` |
| `flex-1` | child: `Tk.Flex.flex: 1` |
| `flex-auto` / `grow` | child: `Tk.Flex.grow: 1` |
| `flex-none` / `shrink-0` | child: `Tk.Flex.shrink: 0` (and `grow: 0`) |
| `basis-48` | child: `Tk.Flex.basis: 192` |
| `min-w-0` / `min-h-0` | default (min 0); nothing to write |
| `min-w-[120px]` | child: `Tk.Flex.minWidth: 120` |
| `max-w-xl` | child: `Tk.Flex.maxWidth: 576` |
| `order-2` | child: `Tk.Flex.order: 2` |
| `items-center` | `align: Tk.Flex.Center` |
| `items-start` / `items-stretch` | `align: Tk.Flex.Start` / default |
| `self-end` | child: `Tk.Flex.alignSelf: Tk.Flex.End` |
| `justify-between` | `justify: Tk.Flex.SpaceBetween` |
| `justify-center` / `justify-end` | `justify: Tk.Flex.Center` / `End` |
| `content-start` | `alignContent: Tk.Flex.Start` |
| `gap-2` | `gap: Tk.Theme.space.md` (8) |
| `gap-x-2 gap-y-1` | `columnGap: 8; rowGap: 4` |
| `ml-auto` (push right) | `Tk.Spacer {}` before the item |
| `grid grid-cols-[auto_minmax(0,1fr)_auto]` | `Tk.Grid { columns: "auto 1fr auto" }` |
| `grid-cols-3` | `columns: "repeat(3, 1fr)"` |
| `grid-cols-[200px_1fr]` | `columns: "200 1fr"` |
| `grid-rows-[auto_minmax(0,1fr)_auto]` | `rows: "auto 1fr auto"` |
| `col-span-2` | child: `Tk.Grid.columnSpan: 2` |
| `col-start-2 row-start-1` | child: `Tk.Grid.column: 1; Tk.Grid.row: 0` (0-based) |
| `grid-flow-col` | `autoFlow: Tk.Grid.Column` |
| `grid-flow-row-dense` | `autoFlow: Tk.Grid.RowDense` |
| `auto-rows-[72px]` | `autoRows: "72"` |
| `place-items-center` | `justifyItems: Tk.Grid.Center; alignItems: Tk.Grid.Center` |
| `justify-self-end` | child: `Tk.Grid.justifySelf: Tk.Grid.End` |
| `grid-template-areas` | `areas: ["hd hd", "sb main"]`; child `Tk.Grid.area: "main"` |
| `relative` + `absolute inset-0` | `Tk.Stack { child }` (fills by default) or `anchors.fill: parent` |
| `absolute inset-3` | `Tk.Stack { child { Tk.Stack.inset: 12 } }` |
| `absolute top-2 right-2` | child: `Tk.Stack.top: 8; Tk.Stack.right: 8` |
| `absolute left-0 right-0 bottom-0` | child: `Tk.Stack.left: 0; Tk.Stack.right: 0; Tk.Stack.bottom: 0` |
| `fixed inset-0 z-[120]` (modal) | `T.Popup { modal: true }` (Dialog/CommandPalette) — not a Stack |
| `z-10` | declaration order in `Tk.Stack`, or `z: 10` |
| `w-full` | default inside a column `Flex` (stretch) or `Grid` cell |
| `w-64` / `h-8` | `implicitWidth: 256` / `implicitHeight: 32` (never `width` inside a container) |
| `w-[220px] shrink-0` | `Tk.Flex.basis: 220; Tk.Flex.shrink: 0` |
| `h-full` / `flex-1 min-h-0` (fill remaining) | child: `Tk.Flex.grow: 1; Tk.Flex.basis: 0` |
| `aspect-video` | `implicitHeight: width * 9 / 16` on an explicitly sized item, or a `Grid` row `"auto"` with the item's implicit size |
| `overflow-y-auto` | `Tk.Scroll { }` |
| `overflow-auto` | `Tk.Scroll { overflowX: Tk.Scroll.Auto }` |
| `overflow-x-auto overflow-y-hidden` | `Tk.Scroll { overflowX: Tk.Scroll.Auto; overflowY: Tk.Scroll.Hidden }` |
| `overflow-hidden` | `Tk.Box { clipContent: true }` or `clip: true` |
| `overscroll-contain` | `Tk.Scroll` (Flickable `StopAtBounds`) |
| `sticky top-0` (header above a scrolling list) | put the header in the column `Flex` *outside* the `Tk.Scroll`, the list inside |
| `truncate` | `Tk.Label` (elides by default when narrowed) |
| `whitespace-nowrap` | default for `Tk.Label` (`wrapMode` unset) |
| `break-words` | `wrapMode: Text.Wrap` |
| `hidden` / `sr-only` | `visible: false` (takes no slot) |
| `pointer-events-none` | `enabled: false` on handlers, or `Tk.Flex.ignore: true` for a pure overlay |

## Box model

| CSS / Tailwind | QindaTK |
| --- | --- |
| `p-3` | `padding: Tk.Theme.space.lg` (12) |
| `px-2 py-1` | `paddingLeft: 8; paddingRight: 8; paddingTop: 4; paddingBottom: 4` |
| `px-1.5 py-0.5` | `paddingLeft: 6; paddingRight: 6; paddingTop: 2; paddingBottom: 2` |
| `m-2` (outer margin) | the parent's `gap`/`padding`, or `Tk.Flex.basis` + a `Spacer`; children have no margin |
| `border border-gray-700` | `Tk.Box { borderWidth: 1 }` (colour defaults to `Theme.color.border`) |
| `border-white/5` | `borderColor: Tk.Theme.color.divider` |
| `border-r border-white/5` | `Tk.Box { borderRight: 1; borderColor: Tk.Theme.color.divider }` |
| `border-b border-cyan-400/40` | `Tk.Box { borderBottom: 1; borderColor: Tk.Theme.color.controlHoverBorder }` |
| `border-2 border-cyan-300` (active) | `borderWidth: 2; borderColor: Tk.Theme.color.accent` |
| `divide-y` | `Tk.Divider {}` between children |
| `rounded-sm` / `rounded` / `rounded-md` / `rounded-lg` / `rounded-xl` / `rounded-full` | `radius: Tk.Theme.radius.xs` (2) / `sm` (4) / `md` (6) / `lg` (10) / `xl` (14) / `full` |
| `shadow-2xl` | `Tk.Theme.color.shadow` — QindaTK draws no blur; use a 1px `borderStrong` border, or `MultiEffect` if you must |
| `ring-2 ring-cyan-300/50` (focus) | a 2px `Tk.Theme.color.focus` border rectangle 1px outside, `visible: control.visualFocus` |

## Colour

| CSS / Tailwind | QindaTK |
| --- | --- |
| `bg-[#0b0c10]` / `var(--sl-bg)` | `Tk.Theme.color.bg` |
| `var(--sl-surface)` | `Tk.Theme.color.surface` |
| `var(--sl-panel)` | `Tk.Theme.color.panel` |
| `bg-black/10` (sunken area) | `Tk.Theme.color.panelAlt` or `Tk.Theme.color.canvas` |
| `bg-black/65` (scrim) | `Tk.Theme.color.overlay` |
| `text-gray-100` / `var(--sl-text)` | `Tk.Theme.color.text` |
| `text-gray-400` / `var(--sl-muted)` | `Tk.Theme.color.textMuted` |
| `text-cyan-300` / `var(--sl-accent)` | `Tk.Theme.color.accentText` |
| `bg-cyan-400/15` | `Tk.Theme.color.accentSubtle` |
| `hover:bg-cyan-400/10` | `color: hovered ? Tk.Theme.color.hover : "transparent"` |
| `bg-cyan-300/20` (pressed) | `Tk.Theme.color.pressed` |
| `.theme-control` | `Tk.Theme.color.controlBg` + `controlBorder` |
| `.theme-control:hover` | `controlHoverBg` + `controlHoverBorder` |
| `.theme-button-accent` | `controlActiveBg` + `controlActiveBorder` |
| `.theme-icon-button` (the pill) | `Tk.Chip` (roles `chipBg`/`chipBorder`) |
| `.theme-input` / `.theme-input:focus` | `inputBg`/`inputBorder` / `inputFocusBorder` |
| `.theme-header` | `headerBg` + `headerText` |
| `.theme-popover` | `popoverBg` + `popoverBorder` |
| `.theme-danger` | `Tk.Theme.color.danger` |
| `color-mix(in srgb, var(--sl-accent) 18%, var(--sl-panel))` | `Tk.Theme.mix(Tk.Theme.color.panel, Tk.Theme.color.accent, 0.18)` — but prefer the derived role |
| `opacity-50` / disabled | `opacity: Tk.Theme.opacity.disabled` |

## Typography

| CSS / Tailwind | QindaTK |
| --- | --- |
| `text-xs` / `text-[11px]` | `Tk.Label { font.pixelSize: Tk.Theme.font.small }` |
| `text-[10px]` | `Tk.Caption` |
| `text-[9px]` | `font.pixelSize: Tk.Theme.font.micro` |
| `text-sm` | `font.pixelSize: Tk.Theme.font.large` (14) |
| `text-[10px] uppercase tracking-[0.14em] font-semibold text-gray-400` | `Tk.Overline { title: "Document" }` |
| `tracking-[0.18em]` | `Tk.Overline { wide: true }` |
| `font-mono tabular-nums` | `Tk.Mono` |
| `font-semibold` | `font.weight: Font.DemiBold` |
| `leading-none` | `Tk.Label` (native rendering, no extra line height) |
| `text-right` | `horizontalAlignment: Text.AlignRight` (needs a set width, e.g. a Grid cell) |
| `select-none` | default (`Tk.Label` is not selectable unless `selectable: true`) |

## Chrome patterns

| Sloom pattern | QindaTK |
| --- | --- |
| Dockable panel with grip + uppercase title | `Tk.Panel { title: ... }` (or `DockPanel` inside a `DockHost`) |
| Section heading inside a panel | `Tk.SectionHeader { title: ...; count: ... }` |
| Floating glass control island over the canvas | `Tk.Island { ... }` anchored/stacked over the canvas |
| Icon button (24px) | `Tk.IconButton { iconName: "x"; tooltip: "Close" }` |
| Category pill / chip (32px, radius 6) | `Tk.Chip { text: ...; iconName: ... }` |
| Thin custom scrollbar | `Tk.Scroll` (built in) or `T.ScrollBar.vertical: Tk.ScrollBar {}` |
| Tooltip on hover (`title=`) | `Tk.ToolTip.text: "..."; Tk.ToolTip.visible: hovered` |
| Lucide icon `<Layers size={14} />` | `Tk.Icon { name: "layers"; size: Tk.Theme.size.icon }` |
