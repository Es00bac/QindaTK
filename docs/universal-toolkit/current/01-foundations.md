<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Theme, density, icons and desktop bridge

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

## Theme

Process-wide visual configuration shared by QML engines and C++ callers.

**Source:** [src/core/theme.h](../../../src/core/theme.h) · **Base:** `QObject` · **Kind:** C++ singleton.

Use color, font, space, radius, size, motion, opacity and ramp groups. Presets are sloom-dark, sloom-light and graphite; applyRoles, setColor, setMetric, loadFile/loadJson and applyQst change the theme. reducedMotion zeros animation durations. This styles interface chrome; it does not color-manage document pixels or supply per-window theme isolation.

**Declared members:** `color: QindaTK::ThemeColors *` (ro), `font: QindaTK::ThemeFont *` (ro), `space: QindaTK::ThemeMetrics *` (ro), `radius: QindaTK::ThemeMetrics *` (ro), `size: QindaTK::ThemeMetrics *` (ro), `motion: QindaTK::ThemeMetrics *` (ro), `opacity: QindaTK::ThemeMetrics *` (ro), `ramp: QindaTK::ThemeRamps *` (ro), `preset: QString`, `name: QString` (ro), `dark: bool` (ro), `reducedMotion: bool`, `generation: qulonglong` (ro).

**Declared signals:** `changed()`.

**Source methods:** `presets()`, `applyPreset()`, `applyRoles()`, `setColor()`, `setMetric()`, `setFontFamilies()`, `applyQst()`, `loadFile()`, `loadJson()`, `toMap()`, `mix()`, `alpha()`, `lighten()`, `darken()`, `onColor()`, `luminance()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_telemetry.cpp](../../../tests/cpp/tst_telemetry.cpp), [tst_theme.cpp](../../../tests/cpp/tst_theme.cpp), [tst_controls_input.qml](../../../tests/qml/tst_controls_input.qml).

## Density

Process-wide sizing policy for compact, comfortable and touch interfaces.

**Source:** [src/core/density.h](../../../src/core/density.h) · **Base:** `QObject` · **Kind:** C++ singleton.

mode/modeName choose scales 1.0, 1.2 and 1.5. space and size ladders scale and round; scaleFonts optionally composes density with typography. textScale independently changes the Tk.Theme.font ramp (0.5–3.0, default 1.0) without changing layout metrics. px rounds; scaled preserves fractional values. A larger density alone does not supply touch gestures or accessible hit targets for every custom part.

**Declared members:** `mode: Mode`, `modeName: QString`, `scale: qreal` (ro), `scaleFonts: bool`, `textScale: qreal`.

**Declared signals:** `changed()`.

**Source methods:** `px()`, `scaled()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_theme.cpp](../../../tests/cpp/tst_theme.cpp).

## ThemeColors

Anonymous object exposing the semantic color roles through Theme.color.

**Source:** [src/core/theme.h](../../../src/core/theme.h) · **Base:** `QObject` · **Kind:** Anonymous C++ helper.

Base colors feed derived control, input, panel, selection, focus, overlay and status roles. See the complete role and mix table in the existing theming reference; this object is obtained from Theme, not constructed as Tk.ThemeColors.

**Declared members:** `bg: QColor` (ro), `surface: QColor` (ro), `panel: QColor` (ro), `panelAlt: QColor` (ro), `canvas: QColor` (ro), `border: QColor` (ro), `borderStrong: QColor` (ro), `divider: QColor` (ro), `text: QColor` (ro), `textMuted: QColor` (ro), `textDisabled: QColor` (ro), `accent: QColor` (ro), `accentContrast: QColor` (ro), `accentSubtle: QColor` (ro), `accentText: QColor` (ro), `hover: QColor` (ro), `pressed: QColor` (ro), `selection: QColor` (ro), `focus: QColor` (ro), `danger: QColor` (ro), `dangerContrast: QColor` (ro), `dangerSubtle: QColor` (ro), `warning: QColor` (ro), `success: QColor` (ro), `info: QColor` (ro), `controlBg: QColor` (ro), `controlBorder: QColor` (ro), `controlHoverBg: QColor` (ro), `controlHoverBorder: QColor` (ro), `controlActiveBg: QColor` (ro), `controlActiveBorder: QColor` (ro), `chipBg: QColor` (ro), `chipBorder: QColor` (ro), `chipHoverBg: QColor` (ro), `chipHoverBorder: QColor` (ro), `inputBg: QColor` (ro), `inputBorder: QColor` (ro), `inputFocusBorder: QColor` (ro), `headerBg: QColor` (ro), `headerText: QColor` (ro), `popoverBg: QColor` (ro), `popoverBorder: QColor` (ro), `tooltipBg: QColor` (ro), `tooltipText: QColor` (ro), `islandBg: QColor` (ro), `islandBorder: QColor` (ro), `scrollTrack: QColor` (ro), `scrollThumb: QColor` (ro), `seam: QColor` (ro), `seamHover: QColor` (ro), `dropZone: QColor` (ro), `shadow: QColor` (ro), `overlay: QColor` (ro).

**Declared signals:** `changed()`.

**Coverage pointers:** [tst_theme.cpp](../../../tests/cpp/tst_theme.cpp).

## ThemeFont

Anonymous typography object obtained through Theme.font.

**Source:** [src/core/theme.h](../../../src/core/theme.h) · **Base:** `QObject` · **Kind:** Anonymous C++ helper.

family and monoFamily plus micro/caption/small/body/medium/large/title/display and tracking roles describe interface text. This is not a document font manager or an OpenType typography editor.

**Declared members:** `family: QString` (ro), `monoFamily: QString` (ro), `micro: qreal` (ro), `caption: qreal` (ro), `small: qreal` (ro), `body: qreal` (ro), `medium: qreal` (ro), `large: qreal` (ro), `title: qreal` (ro), `display: qreal` (ro), `trackingWide: qreal` (ro), `trackingWider: qreal` (ro).

**Declared signals:** `changed()`.

## ThemeMetrics

Anonymous dynamic property map behind each metric ladder.

**Source:** [src/core/theme.h](../../../src/core/theme.h) · **Base:** `QQmlPropertyMap` · **Kind:** Anonymous C++ helper.

Theme.space, radius, size, motion and opacity publish named values. Use Theme.setMetric to change base values. Source defaults and runtime theme overrides are different; consumers should bind tokens rather than copy snapshot pixels.

**Declared members:** No additional local properties; use the inherited API.

## ThemeRamp

Anonymous value-to-color scale used by telemetry.

**Source:** [src/core/theme_ramp.h](../../../src/core/theme_ramp.h) · **Base:** `QObject` · **Kind:** Anonymous C++ helper.

at(t) interpolates a normalized position; forValue(value, from, to) maps a numeric range. stops/colors/count expose the scale. Access a ramp through Theme.ramp; C++ can set stops. It is not a document gradient or an ICC/OCIO transform.

**Declared members:** `stops: QVariantList` (ro), `colors: QList<QColor>` (ro), `count: int` (ro).

**Declared signals:** `changed()`.

**Source methods:** `at()`, `forValue()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_telemetry.cpp](../../../tests/cpp/tst_telemetry.cpp).

## ThemeRamps

Anonymous collection of six theme-derived telemetry ramps.

**Source:** [src/core/theme_ramp.h](../../../src/core/theme_ramp.h) · **Base:** `QObject` · **Kind:** Anonymous C++ helper.

load, thermal, memory, network, io and neutral are rebuilt with the theme; byName resolves a scale. Adding a categorical chart palette or divergent scientific scale would be new work.

**Declared members:** `load: QindaTK::ThemeRamp *` (ro), `thermal: QindaTK::ThemeRamp *` (ro), `memory: QindaTK::ThemeRamp *` (ro), `network: QindaTK::ThemeRamp *` (ro), `io: QindaTK::ThemeRamp *` (ro), `neutral: QindaTK::ThemeRamp *` (ro).

**Source methods:** `byName()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_telemetry.cpp](../../../tests/cpp/tst_telemetry.cpp).

## Icons

Process-wide registry for built-in and application-supplied stroked icons.

**Source:** [src/core/icon.h](../../../src/core/icon.h) · **Base:** `QObject` · **Kind:** C++ singleton.

has, names and resolve inspect the registry; registerIcon(name, paths) adds SVG path strings in the icon coordinate box. Aliases preserve older icon names. It is an icon registry, not an SVG document or asset database.

**Declared members:** No additional local properties; use the inherited API.

**Declared signals:** `registryChanged()`.

**Source methods:** `has()`, `names()`, `resolve()`, `registerIcon()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_smoke.qml](../../../tests/qml/tst_smoke.qml).

## Icon

Painted vector icon using a registry name or an SVG source.

**Source:** [src/core/icon.h](../../../src/core/icon.h) · **Base:** `QQuickPaintedItem` · **Kind:** C++ type.

name/source choose content; size sets implicit extent; color, strokeWidth and tint style it; available reports resolution. Supply accessible meaning through the owning control. General editable vector documents need a separate model and renderer.

**Declared members:** `name: QString`, `source: QUrl`, `size: qreal`, `color: QColor`, `strokeWidth: qreal`, `tint: bool`, `available: bool` (ro).

**Declared signals:** `nameChanged()`, `sourceChanged()`, `sizeChanged()`, `colorChanged()`, `strokeWidthChanged()`, `tintChanged()`, `availableChanged()`.

**Coverage pointers:** [tst_icon.cpp](../../../tests/cpp/tst_icon.cpp), [tst_smoke.qml](../../../tests/qml/tst_smoke.qml).

## QindaQtTheme

Optional bridge from QindaQt desktop tokens into the shared Theme.

**Source:** [src/bridge/QindaQtTheme.qml](../../../src/bridge/QindaQtTheme.qml) · **Base:** `QtObject` · **Kind:** QML component.

Lives in QindaTK.QindaQt, requires QindaQt.Tokens at runtime. active enables synchronization; applied reports adoption; sync runs when tokens become ready/change. This existing bridge is the documented exception to the main module import. Its underscore members are implementation details.

**Declared members:** `active: bool`, `applied: bool` (ro).

**Source methods:** `sync()`. See source for parameters and helper visibility.
