<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-07 — Touch targets and enlarged gallery chrome

**Scope:** a touch-density metric floor and one reachable large-text
gallery workflow. This is partial FND-07 evidence.

The authored Touch multiplier resolved `size.control` to 36px and
`size.row`/`size.menuItem` to 33px. D-022 now raises interactive vertical
roles to 48 logical pixels after density/text-scale resolution, leaving
44px for the inner button of a 2px bordered joined control. The authored
Compact and Comfortable sizes return when density changes back. Short
`Tk.Segmented` choices also have a `size.controlSm` width floor, so their
touch targets remain at least 48×44 in the inspected case.

The gallery header flows its controls onto additional rows. At maximum
supported `textScale: 3` in Touch density, the preset switch needs 1281px
inside a 1280px window. `Scroll#presetScroll` now shows a 1256×63 viewport
with horizontal overflow; the segmented control remains 1281×48, and the
scrollbar has its own space below the text. A QML test scrolls to the end
and verifies the last preset is fully reachable. `SearchField#sectionFilter`
remains 220×48. The dump reports `Box#galleryHeader 0,0 1280x162` and
`Scroll#galleryScroll 0,162 1280x638`, so the content begins below the
expanded header.

Focused `tst_theme` and `tst_qml` targets pass on Qt 6.11.1. The Touch
QML test checks Button, IconButton, ListRow and short Segmented geometry;
it checks actual inner-segment height/width as well as role values. Normal
Touch and 3× Touch software captures were opened, as was a 3× Touch
OpenGL RHI capture. The software and GPU images show the header controls
and scrollbar inside the window. Ignored captures are
`build/gallery-touch-48-segments.png`,
`build/gallery-touch-3x-scroll-padded.png`, and
`build/gallery-touch-3x-gpu.png` in the integration worktree.

This metric floor covers controls that bind to the named size roles.
Sliders, splitters, menu geometry, custom handlers, narrower windows and
physical touch devices still need target and workflow checks before
FND-07 or WP-007 acceptance.
