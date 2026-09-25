<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Theming

`Tk.Theme` is the process-wide theme singleton (C++, `src/core/theme.h`).
Everything visual in the toolkit resolves through it: colour roles, the
type ramp, and the spacing / radius / size / motion / opacity ladders.
`Tk.Density` scales the size-related ladders. Presets are built in, a JSON
file can replace one, and on the QindaQt desktop the `QindaTK.QindaQt`
bridge feeds the desktop's QST-1 tokens in.

## Ownership across windows

The supported theme and density apply to every QindaTK window in the
process. `Tk.Theme.applyPreset()`, JSON/role changes, QST token updates and
`Tk.Density.mode` all update shared singleton objects. Putting a separate
palette object inside one window does not change controls that bind directly
to `Tk.Theme`. See [D-016](decisions.md#d-016--keep-toolkit-theme-ownership-process-wide)
for the scoped-theme decision and the work required before mixed-theme
toolkit windows can be supported. Applications can still render document
content using document-specific colours without changing toolkit chrome.

```qml
import QindaTK as Tk
Rectangle {
    color: Tk.Theme.color.panel
    border.color: Tk.Theme.color.border
    radius: Tk.Theme.radius.md
    Tk.Label { text: "Layers"; font.pixelSize: Tk.Theme.font.caption }
}
```

## Colour roles

Twelve **base roles** are authored (by a preset, a JSON file, `applyRoles`,
or the QST bridge). Every other role is **derived** from them with the
ratios below (`theme_presets.cpp`, `derive()`), which were recovered from
Sloom Studio's stylesheet (`color-mix` rules) and, for the chip and island
roles, from sampling its rendered pixels. Controls only ever read derived
roles, so a theme stays coherent when its accent changes. The two high
contrast presets replace translucent control borders, focus and selected
surfaces with opaque derivatives; their exact overrides are in `derive()`.

`mix(a, b, t)` weights `b` by `t` (CSS `color-mix` order); `alpha(c, a)`
sets the alpha channel; `dark` is the preset's flag, or `luminance(bg) <
0.4` when unspecified.

| Role | Kind | Derivation (dark / light) | Used for |
| --- | --- | --- | --- |
| `bg` | base | | window background |
| `surface` | base | | sidebars, toolbars, status bars |
| `panel` | base | | panels, cards' parent |
| `border` | base | | 1px borders |
| `text` | base | | primary text |
| `textMuted` | base | | captions, overlines, inactive tabs |
| `accent` | base | | brand colour, active states |
| `accentContrast` | base | | text on an accent fill |
| `danger`, `warning`, `success`, `info` | base | | status colours (sloom-dark: `#fb7185`, `#ecc52f`, `#6ee7b7`, accent) |
| `panelAlt` | derived | `mix(panel, black, .12)` / `.04` | cards, sunken list backgrounds |
| `canvas` | derived | `mix(bg, black, .30)` / `.06` | document area behind pages/artboards |
| `borderStrong` | derived | `mix(border, text, .25)` | floating panel borders |
| `divider` | derived | `alpha(border, .60)` | rules, header underlines |
| `textDisabled` | derived | `alpha(textMuted, .55)` | |
| `accentSubtle` | derived | `mix(panel, accent, .12)` | checked backgrounds, notices |
| `accentText` | derived | `accent` / `mix(accent, black, .15)` | accent-coloured text (darker on light themes for contrast) |
| `hover` | derived | `alpha(accent, .10)` | hover overlay |
| `pressed` | derived | `alpha(accent, .18)` | pressed overlay |
| `selection` | derived | `alpha(accent, .28)` | selected rows, text selection |
| `focus` | derived | `alpha(accent, .65)` | focus ring |
| `dangerContrast` | derived | `onColor(danger)` | text on a danger fill |
| `dangerSubtle` | derived | `alpha(danger, .15)` | danger notices |
| `controlBg` | derived | `alpha(panel, .72)` | button/field rest fill (Sloom `theme-control`) |
| `controlBorder` | derived | `alpha(accent, .18)` | |
| `controlHoverBg` | derived | `mix(panel, accent, .12)` | |
| `controlHoverBorder` | derived | `alpha(accent, .42)` | |
| `controlActiveBg` | derived | `mix(panel, accent, .18)` | checked / open |
| `controlActiveBorder` | derived | `alpha(accent, .54)` | |
| `chipBg` | derived | `mix(panel, accent, .18)` | Sloom pill at rest (pixel-measured) |
| `chipBorder` | derived | `mix(border, accent, .25)` | |
| `chipHoverBg` | derived | `mix(panel, accent, .26)` | |
| `chipHoverBorder` | derived | `mix(border, accent, .38)` | |
| `inputBg` | derived | `mix(panel, bg, .66)` | text fields (`theme-input`) |
| `inputBorder` | derived | `alpha(border, .78)` | |
| `inputFocusBorder` | derived | `alpha(accent, .62)` | |
| `headerBg` | derived | `mix(surface, panel, .78)` | panel headers (`theme-header`) |
| `headerText` | derived | `textMuted` | |
| `popoverBg` | derived | `mix(surface, black, .04)` / `panel` | menus, popovers, dialogs |
| `popoverBorder` | derived | `mix(border, accent, .15)` | |
| `tooltipBg` | derived | `mix(panel, black, .10)` / `text` | light themes invert the tooltip |
| `tooltipText` | derived | `text` / `bg` | |
| `islandBg` | derived | `alpha(bg, .72)` | floating control islands |
| `islandBorder` | derived | `alpha(border, .55)` | |
| `scrollTrack` | derived | `mix(surface, black, .18)` / `.04` | |
| `scrollThumb` | derived | `alpha(accent, .38)` | |
| `seam` | derived | `alpha(border, .40)` | splitter / dock dividers at rest |
| `seamHover` | derived | `alpha(accent, .80)` | |
| `dropZone` | derived | `alpha(accent, .22)` | dock drop targets |
| `shadow` | derived | `alpha(black, .55)` / `.25` | |
| `overlay` | derived | `alpha(black, .65)` | modal scrim |

Every role is a `Q_PROPERTY` of `ThemeColors` and a key in
`Theme.toMap().colors`. `ThemeColors.roleNames()` lists them; `tst_theme`
checks that the role table and the properties agree.

## Type ramp (`Tk.Theme.font`)

| Key | px | Use |
| --- | --- | --- |
| `micro` | 9 | badges, shortcut hints |
| `caption` | 10 | captions, overlines, status bar, panel titles |
| `small` | 11 | menus, mono readouts |
| `body` | 12 | default control and label text |
| `medium` | 13 | emphasised body |
| `large` | 14 | third-level headings |
| `title` | 16 | second-level headings |
| `display` | 20 | first-level headings |
| `trackingWide` | caption × 0.14 | overline letter spacing |
| `trackingWider` | caption × 0.18 | wide overline letter spacing |
| `family`, `monoFamily` | | first installed of Inter, Fira Sans, Noto Sans, DejaVu Sans / IBM Plex Mono, JetBrains Mono, Fira Code, DejaVu Sans Mono; else the system fonts |

Sizes are pixels (`font.pixelSize`). They scale with density only when
`Tk.Density.scaleFonts` is true.

## Ladders

Each ladder is a `ThemeMetrics` (a `QQmlPropertyMap`): keys are dynamic
QML properties, so `Tk.Theme.space.md` binds and updates. `space` and
`size` are **scalable** (multiplied by `Density.scale` and rounded);
`radius`, `motion` and `opacity` are not.

| Ladder | Keys (base value) |
| --- | --- |
| `space` | `unit` 4, `xs` 2, `sm` 4, `md` 8, `lg` 12, `xl` 16, `xxl` 24, `xxxl` 32 |
| `radius` | `none` 0, `xs` 2, `sm` 4, `md` 6, `lg` 10, `xl` 14, `full` 999 |
| `size` | `controlSm` 20, `control` 24, `controlLg` 28, `sliderTarget` 16, `sliderTargetSm` 14, `toggleTargetSm` 16, `seamTarget` 16, `chip` 32, `action` 36, `row` 22, `rowLg` 28, `header` 24, `tab` 22, `toolbar` 36, `statusBar` 22, `menuItem` 22, `menuMinWidth` 160, `menuItemMinWidth` 120, `icon` 14, `iconSm` 12, `iconLg` 16, `iconXl` 20, `seam` 4, `scrollbar` 8, `labelWidth` 96, `fieldWidth` 120, `panelMinWidth` 200, `panelMinHeight` 120, `border` 1, `focusRing` 2, `grip` 10, `handle` 14, `dropEdge` 28, `tableColumn` 80, `dialogWidth` 420, `rulerLength` 200, `rulerMarker` 9, `sparkline` 56, `sparklineHeight` 14, `meter` 6, `graphMinHeight` 40, `thumbnail` 72, `thumbnailHeight` 40, `waveformHeight` 28, `timeRuler` 22 |
| `motion` | `instant` 0, `fast` 80, `base` 140, `slow` 220, `tooltipDelay` 600, `tooltipTimeout` 6000, `toastTimeout` 6000, `gestureIdle` 500 (ms); reduced motion zeroes delay and animation durations but retains tooltip/toast reading time and `gestureIdle`, the pause that ends a wheel gesture on Knob and Fader |
| `opacity` | `hidden` 0, `full` 1, `disabled` 0.45, `muted` 0.7, `island` 0.72, `ghost` 0.55, `scrim` 0.65, `wash` 0.06, `tint` 0.18, `tintStrong` 0.32 |

`wash`, `tint` and `tintStrong` are mix weights for colours that are user
data rather than theme roles, such as a calendar's own colour:
`Tk.Theme.mix(Tk.Theme.color.panel, calendarColour, Tk.Theme.opacity.tint)`
is an event fill that keeps `text` readable on every preset, `tintStrong`
its hovered/selected fill, and `alpha(accent, wash)` a whole-column wash
(`CalendarGridView`, D-301).

The `size` values are measurements of Sloom Studio at 100% (see
`installBaseMetrics` in `theme_presets.cpp`). `Tk.Theme.size.get("row")`
reads a key by name.

When text zoom grows, text-bearing vertical sizes keep the larger of their
density-scaled base and a font-based minimum. This applies to control,
controlSm, controlLg, chip, row, rowLg, tab, toolbar, menuItem, header,
statusBar and timeRuler. For example, at compact density and 2× text zoom,
`row` resolves to 34px and `control` to 36px while a 30px segment label fits
inside its 34px row. This does not enlarge widths or turn fixed application
layouts into responsive layouts; inspect narrow and large-text scenes.

In Touch density, interactive vertical roles `controlSm`, `control`,
`controlLg`, `row`, `rowLg`, `tab`, `menuItem`, `header`, `timeRuler`,
`sliderTarget`, `sliderTargetSm`, `toggleTargetSm` and `seamTarget` have a 48 logical-pixel minimum after
density and text zoom resolve (D-022).
This gives a bordered joined control a 44px interior button. For example,
`control` resolves to 48px in Touch at normal text size, then
returns to 24px in Compact. The floor covers controls using those roles.
`sliderTarget` (16px base) and `sliderTargetSm` (14px base) give `Slider` and
`RangeSlider` density-scaled targets while retaining their Compact visual
rows. In Touch each role resolves to at least 48px; the track and handles
remain centered at their authored density size. `toggleTargetSm` (16px base)
keeps small toggle rows at their desktop geometry while providing a 48px
Touch surface. Actionable StatusField uses `size.control` for its Touch target,
and StatusBar places its top rule outside that content height. DockDivider uses
`seamTarget` (16px base) for its handler margin, leaving the seam width
unchanged. DockFloating uses the same role for expanded edge and corner resize
targets while preserving frame geometry; constrained small frames may have
smaller targets to keep the eight sides distinct.

`ColorSwatches` uses `controlSm` as the minimum width and height for each
square cell. In Touch this provides a 48×48px input area while the swatch
artwork remains centered at its authored `swatchSize`.

`Ruler` keeps marker artwork at `size.rulerMarker`; only its hit radius grows
to half of `size.controlSm` (24px) in Touch. When marker regions overlap, the
nearest marker receives the gesture, with exact ties resolved by model order.

## Telemetry ramps (`Tk.Theme.ramp`)

A ramp is a value-to-colour scale, for readings that should be understood by
magnitude rather than by series: a core at 95%, a disk at 98% full, a sensor
near its limit. `Tk.Graph`, `Tk.Meter` and `Tk.Sparkline` take one as a
`ramp` property, and `Tk.TableColumn.ramp` colours a cell's number the same
way, so the bar and the figure beside it always agree.

| Ramp | Shape | Reads as |
| --- | --- | --- |
| `load` | `success` 0 → `warning` .55 → `danger` 1 | utilisation: CPU, a core, a queue |
| `thermal` | `info` 0 → `success` .45 → `warning` .75 → `danger` 1 | a temperature against its limit |
| `memory` | `accent` 0 → `warning` .6 → `danger` 1 | occupancy: RAM, swap, a filesystem |
| `network` | dimmed `accent` 0 → `accent` .55 → brightened `accent` 1 | throughput |
| `io` | `accent` 0 → `warning` .6 → `danger` 1 | disk transfer and queue depth |
| `neutral` | `textMuted` 0 → `text` 1 | magnitude with no judgement attached |

```qml
Tk.Label {
    text: temperature + "\u00b0C"
    color: Tk.Theme.ramp.thermal.forValue(temperature, 30, 95)
}
```

`at(t)` takes a fraction of the scale; `forValue(v, from, to)` rescales a
reading onto it. `stops` and `colors` expose the scale itself, for a gradient
brush. Ramps are derived from the colour roles and rebuilt on every theme
change, so they follow a preset, a JSON theme and the QindaQt bridge alike.

Two shapes are deliberate rather than tasteful. The stop *positions* keep a
reading calm through the first half of its range and turn only near
saturation, so an idle machine is visually quiet — moving one changes what a
reader believes about their machine. And `network` is built by mixing
`accent` against `bg` and `text` instead of naming two roles, because a
preset may resolve two roles to the same colour (`sloom-dark` resolves
`accent` and `info` that way) and a ramp whose ends collapse renders every
reading identically; `tst_telemetry` asserts this for every preset.

## Density (`Tk.Density`)

| Mode | `scale` | Effect |
| --- | --- | --- |
| `Tk.Density.Compact` (default) | 1.0 | the measured Sloom metrics |
| `Tk.Density.Comfortable` | 1.2 | `space` and `size` ×1.2, rounded |
| `Tk.Density.Touch` | 1.5 | pen/finger use; themed interactive rows resolve to at least 48 logical px |

`mode` (enum) and `modeName` (`"compact"`, `"comfortable"`, `"touch"`;
`"dense"` is accepted as compact) are both writable. `scaleFonts` (default
false) scales the type ramp with density. `textScale` (default `1.0`) is an
independent user text zoom for `Tk.Theme.font`; it is clamped to `0.5`–`3.0`
and ignores non-finite values. The effective font factor is `textScale`, or
`textScale × Density.scale` when `scaleFonts` is enabled. Assigning `1.0`
resets user text zoom without changing density or `scaleFonts`. For example,
`Tk.Density.textScale = 1.25` enlarges text by 25% independently of density.
Spacing stays the same while text-bearing vertical sizes gain the minimums
described above. Some fixed application widths still crowd or elide content;
WP-007's full large-text workflow qualification remains open. JSON and QST
font sizes remain the unscaled base ramp, so changing or resetting text zoom
never compounds previously published font sizes. `px(v)` and `scaled(v)`
scale a value by hand. Radii, motion and
opacity never scale; the 1px border does not scale either because
`size.border` is scalable but rounds back to 1 at every mode.

## Presets

| Id | Name | Base |
| --- | --- | --- |
| `sloom-dark` (default) | Sloom Dark | Sloom Studio's shipped palette (`bg #0b0c10`, `surface #11141d`, `panel #1a1b23`, `border #263244`, `text #f3f7fb`, `textMuted #92a3b8`, `accent #22d3ee`, `accentContrast #061018`, `danger #fb7185`, `warning #ecc52f`, `success #6ee7b7`, `info #22d3ee`) |
| `sloom-light` | Sloom Light | paper tones from the original's splash screen, cyan-600 accent |
| `graphite` | Graphite | neutral dark with a blue accent, for non-Sloom applications |
| `high-contrast-dark` | High Contrast Dark | black surfaces, white borders/text and yellow focus/accent |
| `high-contrast-light` | High Contrast Light | white surfaces, black borders/text and dark-blue focus/accent |

The high contrast presets keep disabled controls opaque so labels remain
legible. The focused Qt test checks representative text pairs at 4.5:1 and
key control boundaries at 3:1 using resolved theme colours. This does not
certify every widget or custom theme against an accessibility standard;
WP-007 still requires large-text, hit-target and visual workflow review.
The ratio targets follow [WCAG 2.2 text and non-text contrast criteria](https://www.w3.org/TR/wcag/).

`Tk.Theme.preset = "sloom-light"` or `Tk.Theme.applyPreset("sloom-light")`
(returns false for an unknown id). `Tk.Theme.presets()` lists ids.
`Tk.Theme.name` is the human name; `Tk.Theme.dark` the flag.

## Authoring a theme

- `Tk.Theme.applyRoles({accent: "#ff8800", dark: true})` — sets any of the
  twelve base roles (missing ones keep their value), re-derives everything,
  then applies any further key as an override of a derived role. `preset`
  becomes `"custom"`.
- `Tk.Theme.setColor("scrollThumb", c)` — override one role after
  derivation; kept until the next preset/roles/file/QST apply.
- `Tk.Theme.setMetric("size", "row", 24)` — set a ladder base value
  (`"space"`, `"radius"`, `"size"`, `"motion"`, `"opacity"`).
- `Tk.Theme.setFontFamilies(family, monoFamily)` — empty strings keep the
  current family.
- `Tk.Theme.reducedMotion = true` — every motion key reads 0 until set back.
- `Tk.Theme.loadFile(path)` / `loadJson(text)` — a JSON theme (below);
  `preset` becomes `"file"`.
- `Tk.Theme.toMap()` — the resolved theme (`qtk-preview --dump-theme`).
- `Tk.Theme.generation` — increments on every change; `Tk.Theme.changed`
  is the signal.

### JSON theme file

```json
{
  "name": "Ink",
  "dark": true,
  "colors": {
    "bg": "#0e0f13", "surface": "#14161c", "panel": "#1b1e26", "border": "#2a3040",
    "text": "#f0f3f8", "textMuted": "#8f9bb0", "accent": "#7dd3fc", "accentContrast": "#06131c",
    "danger": "#fb7185", "warning": "#fbbf24", "success": "#86efac", "info": "#7dd3fc",
    "scrollThumb": "#7dd3fc66"
  },
  "font": { "family": "Inter", "monoFamily": "JetBrains Mono", "caption": 10, "body": 12 },
  "space": { "md": 8 },
  "radius": { "md": 5 },
  "size": { "row": 22, "control": 24 },
  "motion": { "fast": 60 },
  "opacity": { "disabled": 0.4 }
}
```

`colors` is required. Base keys are derived from; any other colour key is
an override. `font` keys are the ramp names plus the two families. Ladder
objects set base values by key. Colours are anything `QColor` parses
(`#rrggbb`, `#aarrggbb`, names).

## Colour math

| Function | Result |
| --- | --- |
| `Tk.Theme.mix(a, b, t)` | linear sRGB mix, `b` weighted by `t` in 0…1 |
| `Tk.Theme.alpha(c, a)` | `c` with alpha `a` |
| `Tk.Theme.lighten(c, amount)`, `darken(c, amount)` | mix with white / black |
| `Tk.Theme.onColor(bg)` | `#0b0c10` or `#f3f7fb`, whichever contrasts with `bg` (WCAG relative luminance, threshold 0.35) |
| `Tk.Theme.luminance(c)` | relative luminance 0…1 |

Prefer a derived role over calling these in a control; add a role when a
mix is needed in more than one place.

## The QindaQt desktop bridge

On the QindaQt desktop, `QindaQt.Tokens` publishes QST-1 semantic tokens.
The QML-only module `QindaTK.QindaQt` maps them into the theme:

```qml
import QtQuick
import QindaTK as Tk
import QindaTK.QindaQt

Window {
    QindaQtTheme { id: desktopTheme }     // active: true by default
    // desktopTheme.applied is true once tokens have been adopted
}
```

`QindaQtTheme.sync()` runs on completion, on every `Tokens.tokensChanged`,
and when `active` flips to true. It calls `Tk.Theme.applyQst()` with the
token maps and sets `Tk.Theme.reducedMotion` from
`Tokens.accessibility.reducedMotion`. When `Tokens.ready` is false nothing
is applied and the toolkit keeps its current preset, so a surface never
paints invented colours over an unpublished theme.

Mapping (`ThemePresets::applyQst`): `bg.base/raised/highest` →
`bg/surface/panel`; `fg.default/muted/disabled` → `text/textMuted/
textDisabled`; `accent.default/fg/subtle` → `accent/accentContrast/
accentSubtle`; `state.hover/pressed`, `focus.ring`, `outline.divider/strong`
→ `border/borderStrong`; `status.*.fg` → `success/warning/info`;
`danger.default/fg`; `type.fontFamily/monoFontFamily` and the point-size
ramp (`caption`, `body`, `subtitle`, `title`, `display`, converted to
pixels at 96 dpi; `small`/`medium`/`large`/`micro` interpolated);
`radius.s/m/l` → `sm/md/lg`; `motion.short/base/long` → `fast/base/slow`.
Alternative key spellings (`foreground`, `defaultColor`, `small/medium/
large`, `shortDuration`) are accepted. `preset` becomes `"qindaqt"`,
`name` the source theme id.

The same map can be built by hand and passed to `Tk.Theme.applyQst({...})`
from any QML or C++ code; it returns false when `bg`, `fg` or `accent` is
missing.

Verifying the bridge without a running desktop session: `qtk-preview`
built with the desktop libraries present (`QINDATK_PREVIEW_QINDAQT`, on by
default; the configure log says `qtk-preview: QindaQt desktop themes
enabled (--qst)`) can publish a real desktop theme through the
`QindaQt.Tokens` facade and instantiate the bridge itself:

```
qtk-preview --list-qst                                  # qinda-bliss, qinda-dark, qinda-dusk, ...
qtk-preview file.qml --qst qinda-dusk --grab out.png    # the file under the desktop theme
qtk-preview file.qml --qst /path/to/theme.json --dump
```

The first stdout line reports what was adopted, e.g.
`qst: published qinda-dusk, QindaTK theme is now "qinda-dusk" (qindaqt)`.
Installed theme ids come from `/usr/share/qindaqt/themes`.

## Adding a colour role

1. `theme.h`: add `Q_PROPERTY(QColor <role> READ <role> NOTIFY changed)`
   and `QTK_ROLE(QColor, <role>)` to `ThemeColors`.
2. `theme.cpp`: add `{"<role>", &ThemeColors::m_<role>}` to `roleTable()`.
3. `theme_presets.cpp`: derive it in `derive()` from base roles with a
   documented ratio (or map it in `applyQst` when QST carries it).
4. Add the row to the table above, run `ctest --preset dev` (`tst_theme`
   fails if the table and the properties disagree), regenerate
   `docs/catalog.json`.
