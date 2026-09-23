<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-006 FND-03: narrow adaptive layout fixture

`examples/adaptive-layout/Main.qml` records narrow-window geometry for a
wrapped action row, responsive Grid cards, and a Stack message with an
overlaid task badge. The fixture is 360×300 and uses long text to force
wrapping. `interfaceDirection` follows the application direction by default
and can be set to `Qt.RightToLeft` for a preview.

## Reproduction

Configured `build/wp006-adaptive` against the system Qt 6.11.1 build and
read the live `MAKEOPTS` (`-j24 -l24`). The focused test matrix is:

```sh
ctest --test-dir build/wp006-adaptive -R '^tst_layout$' --output-on-failure
build/wp006-adaptive/tools/preview/qtk-preview examples/adaptive-layout/Main.qml \
  --dump --grab build/wp006-adaptive/adaptive-ltr-compact.png \
  --density compact --size 360x300
```

The QtTest `adaptiveNarrowLayout` matrix exercises compact and comfortable
density, LTR and RTL, and 1× and 2× text. It checks two Flex lines with
non-overlapping children, RTL/LTR ordering, two auto-fill Grid columns and
their mirrored positions, badge/message non-overlap in Stack, the
density-resolved gaps/insets, and spacing between the three sections. At
2× it also checks that the Scroll content exceeds its viewport.

## Observations

The four 1× rows pass. The preview dump and inspected software captures
show these representative geometries (child coordinates are relative to their
immediate container):

| Region | Compact LTR | Compact RTL | Comfortable LTR | Comfortable RTL |
| --- | --- | --- | --- | --- |
| Flex action A/B | `0,0 157x15`; `159,0 136x15` | `195,0 157x15`; `57,0 136x15` | `0,0 157x15`; `159,0 136x15` | `193,0 157x15`; `55,0 136x15` |
| Flex third action | `0,17 151x15` | `201,17 151x15` | `0,17 151x15` | `199,17 151x15` |
| Grid card A/B | `0,0 175x30`; `177,0 175x30` | `177,0 175x30`; `0,0 175x30` | `0,0 174x30`; `176,0 174x30` | `176,0 174x30`; `0,0 174x30` |
| Stack message | `4,4 298x30` | `50,4 298x30` | `5,5 292x30` | `53,5 292x30` |
| Stack badge | `304,4 44x16` | `4,4 44x16` | `299,5 46x19` | `5,5 46x19` |

The action row forms two lines. Grid auto-fill chooses two columns at 352px
compact or 350px comfortable available width. Grid column 0 and the Stack badge move to the opposite
physical side in RTL. Stack reserves the badge's implicit width plus a
theme-scaled gap from the message region, so the long message no longer
paints underneath the badge. The four PNGs were opened: all show the expected
column order and non-overlapping Stack message/badge at both densities.

The default preview uses `Density.scaleFonts: false`, so its text glyph size
remains the same at the two density settings while spacing and Stack insets
grow. The automated matrix enables font scaling. In the first version,
fixed-height sections passed the within-section geometry checks but the 2×
software screenshot showed that the sections painted over each other. The
fixture now flows through a column inside `Tk.Scroll`. The Grid is hosted in
an `Item` whose implicit height follows its cards; this avoids a Grid/Flex
implicit-size polish loop observed when the Grid was nested directly.

The eight revised rows pass. The 2× preview used a wrapper that sets
`Density.scaleFonts: true` and `Density.textScale: 2`; all four software
captures were opened. Representative `--dump` lines:

| 2× case | Flex y/height | Grid host y/height | Stack y/height | Scroll content/viewport |
| --- | --- | --- | --- | --- |
| Compact LTR/RTL | `4/182` | `198/150` | `360/98` | `462/300` |
| Comfortable LTR/RTL | `5/218` | `237/180` | `431/154` | `590/300` |

The 12px compact and 14px comfortable section gaps keep the card region
below the wrapped actions and the Stack below the cards. The lower content
is reached by scrolling. These are software-rendered geometry and pixel
checks, not a claim about platform-specific font metrics or physical input
target accessibility.

With the revised fixture, full CTest passes 19/19 on system Qt 6.11.1 and
the isolated Qt 6.8.3 SDK. System `all_qmllint` exits successfully with its
existing unqualified-access warnings. The focused eight-row layout check
finishes without Grid polish-loop warnings on both Qt versions.

The same four 2× cases were captured and opened using `qtk-preview --gpu`
with `QSG_RHI_BACKEND=opengl` on Qt 6.11.1. `QSG_INFO=1` identified Qt's
OpenGL RHI and the AMD Radeon RX 5700 XT renderer, so this exercised the
hardware scene graph rather than the software adaptation. Compact and
comfortable `layout-diagnostics` GPU captures were also opened; baseline
placement, mirrored Flex/Grid/Stack items, square Box edges and clipped
nested Scroll content match the expected geometry. A compact LTR 2× capture
was also opened from Qt 6.8.3 on the same OpenGL renderer. Vulkan via the
headless offscreen platform could not create a platform Vulkan instance,
so these GPU claims are limited to OpenGL RHI on this machine.

This package adds no layout algorithms. The existing narrow Flex/Grid/Stack
semantics are exercised as currently implemented; wider stress widths,
monitor-scale changes, and platform-native font variations remain outside
this focused fixture.
