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

The QtTest `adaptiveNarrowLayout` data rows exercise compact and comfortable
density in both LTR and RTL. It checks two Flex lines with non-overlapping
children, RTL/LTR ordering, two auto-fill Grid columns and their mirrored
positions, badge/message non-overlap in Stack, and the density-resolved gap
and insets. It enables `Density.scaleFonts` while testing to include a text
scale change in the stress case.

## Observations

All four test rows pass. The preview dump and inspected software captures
show these representative geometries (child coordinates are relative to the
container):

| Region | Compact LTR | Compact RTL | Comfortable LTR | Comfortable RTL |
| --- | --- | --- | --- | --- |
| Flex action A/B | `0,0 157x15`; `159,0 136x15` | `179,0 157x15`; `41,0 136x15` | same | same |
| Flex third action | `0,17 151x15` | `185,17 151x15` | same | same |
| Grid card A/B | `0,0 167x84`; `169,0 167x84` | `169,0 167x84`; `0,0 167x84` | same | same |
| Stack message | `4,4 282x76` | `50,4 282x76` | `5,5 278x74` | `53,5 278x74` |
| Stack badge | `288,4 44x16` | `4,4 44x16` | `285,5 46x19` | `5,5 46x19` |

The action row forms two lines. Grid auto-fill chooses two columns at 336px
of available width. Grid column 0 and the Stack badge move to the opposite
physical side in RTL. Stack reserves the badge's implicit width plus a
theme-scaled gap from the message region, so the long message no longer
paints underneath the badge. The four PNGs were opened: all show the expected
column order and non-overlapping Stack message/badge at both densities.

The preview uses QindaTK's default `Density.scaleFonts: false`, so its text
glyph size remains the same at the two density settings while spacing and
Stack insets grow. The automated matrix enables font scaling and verifies
that the layout continues to fit. This is a software-rendered geometry
fixture, not a claim about platform-specific font metrics or physical input
target accessibility.

This package adds no layout algorithms. The existing narrow Flex/Grid/Stack
semantics are exercised as currently implemented; wider stress widths,
monitor-scale changes, and platform-native font variations remain outside
this focused fixture.
