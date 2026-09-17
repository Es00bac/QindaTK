<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Adopting QindaTK in QindaStudio

QindaStudio (`~/work_SPaC3/QindaStudio`) already carries a first-generation
version of most of what QindaTK provides: `StudioPanel.qml`, `DockHost.qml`
+ `DockPanel.qml` + `DockDivider.qml` over `PanelLayoutController`,
`StudioPill.qml`, `StudioActionPill.qml`, `StudioControlIsland.qml`,
`StudioSectionHeader.qml`, `StudioIcon.qml` (an image provider), and
`StudioTheme.qml` (accent-mix ratios). QindaTK generalises those, keeps
their measured metrics, and adds what they lacked: real flexbox/grid
layout, lanes and tab groups with drag-to-dock, per-side borders, overflow
scrolling with overlay bars, a full dense control set, and headless
verification. This page maps one onto the other so the port is mechanical.

## Build integration

```cmake
find_package(QindaTK REQUIRED)            # after the Qt6 find_package
target_link_libraries(qindastudio PRIVATE QindaTK::qindatk)   # only if C++ uses DockModel/Theme
```

QML needs nothing beyond the installed module (`/usr/lib64/qt6/qml/QindaTK`).
Keep `ensureTokenFacade` + `applyAppearance` exactly as they are; add one
object to `Main.qml` / `WorkspaceWindow.qml` so QindaTK follows the studio's
QST publication (theme switch and density both republish):

```qml
import QindaTK as Tk
import QindaTK.QindaQt
ApplicationShell {
    QindaQtTheme { }          // Tk.Theme now mirrors Tokens; re-derives on tokensChanged
    ...
}
```

`AppearanceController.density` ("compact" | "comfortable") should also set
`Tk.Density.modeName` so QindaTK's own ladders (row heights, control
heights) move with the deriver's text scale: `Tk.Density.modeName = density`.

## Type map

| QindaStudio | QindaTK | Notes |
| --- | --- | --- |
| `StudioPanel { title; floating; collapsed }` | `Tk.Panel { title; floatable; floating; collapsible; collapsed; closable }` | Same 24px header, grip, uppercase title. Float/dock is a *request* (`floatRequested`); the dock host decides. Body has `padding` (default `space.sm`). |
| `DockHost { layout: panelLayout; panelContent: fn }` | `Tk.DockHost { model: Tk.DockModel; workspace; canvas: Item {}; DockPanel {...} }` | Panels are declared as children, contents are reparented (state survives moves); no `panelContent` component chooser. See [docking.md](docking.md) for the method-by-method PanelLayoutController → DockModel table. |
| `DockPanel` (chrome) | `Tk.DockFrame` (internal) | Not used directly. |
| `DockDivider { vertical; onDragged }` | `Tk.DockDivider` (internal) / `Tk.Splitter` | Splitter is the in-panel resizable pane; dock seams are owned by DockHost. |
| `panelLayout.registerPanel(ws, id, {title, mode, dockZone, dockedExtent, minWidth, ...})` | `model.registerPanel(ws, id, {...})` or a `DockPanel` child | Keys `dockZone`/`dockedExtent`/`column` are accepted as aliases of `zone`/`extent`/`lane`. |
| `panelLayout.dock/floatPanel/hide/collapse/toggle/moveFloating/resizeFloating/resizeDocked/bringToFront/groupWith/ungroup/activateInGroup/reset/registerPreset/applyPreset/presets/saveLayout/applyLayout/deleteLayout/persist/restore` | identical names on `Tk.DockModel` | Additions: `dockBeside`, `lanes`, `setLaneExtent`, `setShare`, `moveInGroup`, `show`, `serialize`/`deserialize`, `storageKey`. |
| `StudioPill { label; iconName; showChevron; active }` | `Tk.Chip { text; iconName; chevron; active }` | 32px, radius 6, `chipBg/chipBorder` (= StudioTheme.pillFill/pillBorder ratios). |
| `StudioActionPill { label; iconName; emphasis }` | `Tk.Chip { rounded: true; emphasis }` | 36px fully round; `"plain"`, `"tinted"`, `"solid"` kept. |
| `StudioControlIsland` | `Tk.Island` | 72% canvas fill, 55% border, 6/8px padding. |
| `StudioSectionHeader { title }` | `Tk.Overline { title }` or `Tk.SectionHeader { title; count; trailing }` | 10px semibold uppercase, 0.18em → `Tk.Overline { wide: true }`. |
| `StudioIcon { name; size; color; strokeWidth }` | `Tk.Icon { name; size; color; strokeWidth }` | Same Lucide names (deprecated names alias); no image provider, no URL encoding; register app icons with `Tk.Icons.registerIcon`. |
| `StudioTheme.pillFill / pillBorder / pillFillHover / pillBorderHover` | `Tk.Theme.color.chipBg / chipBorder / chipHoverBg / chipHoverBorder` | Same ratios (18/25/26/38 %). |
| `StudioTheme.islandFill / islandBorder` | `Tk.Theme.color.islandBg / islandBorder` | |
| `StudioTheme.needsSetup / runsLocally` | `Tk.Theme.color.warning / success` | Literal amber/emerald when QST has no usable status colours; QST status colours when the bridge is active. |
| `Tokens.bg.base / raised / highest` | `Tk.Theme.color.bg / surface / panel` | |
| `Tokens.fg.default / muted / disabled` | `Tk.Theme.color.text / textMuted / textDisabled` | |
| `Tokens.accent.default / fg / subtle` | `Tk.Theme.color.accent / accentContrast / accentSubtle` | |
| `Tokens.outline.divider / strong` | `Tk.Theme.color.border / borderStrong` (+ `divider` at 60 %) | |
| `Tokens.space["1".."6"]` | `Tk.Theme.space.xs/sm/md/lg/xl/xxl` | Same 2/4/8/12/16/24 values at compact density. |
| `Tokens.radius.s/m/l` | `Tk.Theme.radius.sm/md/lg` | |
| `Tokens.type.caption/body/title` (points) | `Tk.Theme.font.caption/body/title` (pixels) | Bridge converts pt → px. |
| `Tokens.motion.short/base/long` | `Tk.Theme.motion.fast/base/slow` | |
| `Qinda.Button` (40px) | `Tk.Button` (24px) | QindaQt.Controls stays right for settings pages; QindaTK for dense chrome. |
| `Qinda.FormRow { label; editor }` | `Tk.PropertyRow { label; ... }` | 22px inspector rows instead of 220px-label form rows. |
| `T.ScrollView` + `ColumnLayout` | `Tk.Scroll { Tk.Flex { direction: Column } }` | Removes the `height: implicitHeight` / `anchors.fill` binding-loop class of bugs noted in `InspectorPanel.qml`. |
| `RowLayout` / `ColumnLayout` / `GridLayout` | `Tk.Flex` / `Tk.Grid` | `Layout.fillWidth: true` → `Tk.Flex.grow: 1`; `Layout.preferredWidth` → `Tk.Flex.basis`; `Layout.minimumWidth` → `Tk.Flex.minWidth`; `GridLayout.columns: 2` → `Tk.Grid { columns: "auto 1fr" }`. |

## Suggested order

1. Add the bridge object and `Tk.Density.modeName`; nothing else changes.
   Verify with `qtk-preview qml/ImageLayersPanel.qml --qst sloom-... ` style
   probes (a workspace QML that only needs `Tk.*` renders headless).
2. Replace `StudioIcon` → `Tk.Icon` (drop the image provider), `StudioPill`
   → `Tk.Chip`, `StudioControlIsland` → `Tk.Island`, `StudioSectionHeader`
   → `Tk.Overline`. These are leaf replacements.
3. Replace `StudioPanel` and the per-workspace `DockHost`/`DockPanel` with
   `Tk.DockHost` + `Tk.DockPanel` children; keep `PanelLayoutController`'s
   registrations by feeding them to `Tk.DockModel.registerPanel` (or move
   `workspace_panels.cpp` to construct a `QindaTK::DockModel` — the maps
   are compatible). Persisted layouts migrate by reading the old
   `panels/layouts` QSettings key and calling `deserialize` after adapting
   the key names (`dockZone`→`zone`, `dockedExtent`→`extent`).
4. Convert inspectors panel by panel: `ColumnLayout` → `Tk.Flex`, rows →
   `Tk.PropertyRow`, `Qinda.TextField`/`ComboBox` in dense contexts →
   `Tk.TextField`/`Tk.ComboBox`, section headers → `Tk.SectionHeader`.
5. Add `ui_contract.json` objectNames to the new frames
   (`dockFrame_<panelId>` etc.) so `--check-ui-contract` keeps passing.

## What stays in QindaStudio

- `QindaQt.AppShell` (`ApplicationShell`, coordinator, global menu export)
  — QindaTK has no opinion about windows or menus export.
- `QindaQt.Controls` for settings/preferences pages (40px targets).
- Command dispatch, workspace launcher, project model.
