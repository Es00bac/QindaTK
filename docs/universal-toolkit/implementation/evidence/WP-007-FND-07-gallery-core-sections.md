<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 FND-07 — core gallery sections at large text

## Scope

Audited the gallery `ButtonsSection`, `TextSection`, `ThemeSection`,
`StructureSection`, and `OfficeSection` at 480×320 in Touch density with
`Tk.Density.textScale = 3`. Each section was isolated with the gallery filter
and rendered under `high-contrast-dark` and `high-contrast-light`.

## Findings

No clipped or unreachable controls were found in these sections. In every
capture, the selected section fits the gallery content width (456px); its
inner header/body area is 430px wide and wraps the section note and row labels.
The section itself is taller than the 160px gallery viewport, but the gallery
Scroll remains vertically scrollable and reaches the end of the selected
section. This leaves the controls below the fold available by scrolling.

The stable narrow-window geometry is `Box#galleryHeader 0,0 480x160`,
`Scroll#galleryHeaderScroll 0,0 480x159`, and `Scroll#galleryScroll 0,160
480x160`. The opened captures show separate header and section viewports with
vertical scrollbars. Selected section heights from the dark-theme dumps are:

| Section | Height |
| --- | ---: |
| Buttons and chips | 1958px |
| Text | 1279px |
| Theme | 2075px |
| Structure | 2789px |
| Document app parts | 1470px |

The expanding section headers range from 125px (Text and Theme) to 273px
(Office), with the longer notes wrapping above their first rows. No source
layout fix was needed for this scope.

## Regression coverage

`TouchTargets::test_narrow_large_text_gallery_sections_remain_reachable_in_high_contrast`
filters each of the five sections under both high-contrast presets at Touch 3×,
checks section width against the gallery, verifies vertical scrolling is
available, and moves the scroll position to the content end.

## Render evidence

All ten PNG captures and their `qtk-preview --dump` text files were generated
from the isolated review copy in
`build/wp007-gallery-review/`. The capture names are
`<section>-high-contrast-<dark|light>.png` and `.dump`, where section is
`Buttons`, `Text`, `Theme`, `Structure`, or `Document-app-parts`. All ten PNGs
were opened and visually inspected.

The review copy sets `Tk.Density.textScale = 3` on gallery startup because
`qtk-preview` has no text-scale CLI option. The checked-in gallery source was
not modified by the review harness.

## Verification limits

`ctest --test-dir build/dev -R '^tst_qml$' --output-on-failure` passes on
Qt 6.11.1. `cmake --build --preset dev --target all_qmllint` exits successfully
with existing module warnings. Only Qt 6.11.1 is installed in this environment;
Qt 6.8 verification was not available.

After integration, the gallery-section and full QML fixtures also passed in
the isolated Qt 6.8.3 build. The integrated source passed all 20 CTest targets
and `all_qmllint` on both Qt 6.11.1 and 6.8.3. The worker environment limit
above remains the historical scope of that isolated review.
