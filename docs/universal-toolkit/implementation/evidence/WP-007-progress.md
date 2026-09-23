<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 — Interaction and accessibility progress

**Status: active.** The [source inventory](WP-007-inventory.md) identified
pointer-only SectionHeader disclosure and TimeRuler scrubbing. Those two
repairs are integrated; WP-007 still owns the remaining F04 audit and
FND-07 through FND-10.

## Integrated repairs

- `SectionHeader` and `PropertyGroup` share a guarded disclosure operation
  for tap, Space, Return, Enter and `Accessible.pressAction`. Collapsible
  enabled headers can receive Tab focus and show a theme-derived focus ring.
  Accessible name and description report title and current state. Disabled
  and noncollapsible headers do not toggle; programmatic group state remains
  synchronized. Stable `sectionTitle`, `groupHeader` and `groupBody`
  objectNames remain.
- `TimeRuler` exposes its visible range and value as read-only QML properties,
  an accessible slider role and description, keyboard Left/Right/Home/End,
  and accessible increase/decrease actions. All user scrubs use one bounded
  operation. External playhead updates do not emit `scrubbed`; disabled
  rulers ignore scrubs.
- Two high-contrast presets use opaque derived borders, focus and control
  surfaces. Resolved representative text pairs meet 4.5:1 and control
  boundaries 3:1 in `tst_theme`; switching back restores standard disabled
  opacity. These pairs are a focused palette check, not a whole-toolkit
  accessibility certification.

## Verification

On both Qt 6.11.1 and isolated Qt 6.8.3, integrated Debug builds, all
15 CTest targets and `all_qmllint` exited 0. The new QML tests cover
pointer/keyboard/assistive-action signal paths, disabled behavior and group
state synchronization. These signal tests do not establish native screen-reader
delivery or a platform value interface.

The focused SectionHeader preview was dumped and grabbed under compact and
comfortable density, and both PNGs were opened for pixel inspection. The
compact dump showed `SectionHeader#focusedSectionHeader 0,0 440x22` with a
visible focus-ring `Item 0,0 440x22` and four 2px themed edge rectangles;
the plain header remained `0,26 440x22` with its ring hidden. Comfortable
density showed the focused header `0,0 440x26` and the plain header
`0,31 440x26`. The focus ring was visible around the focused row in both
captures, with the title, count and disclosure icon readable. The ignored
captures are `build/section-header-focus-{compact,comfortable}.png` in the
integration worktree.

The gallery under `high-contrast-dark` and `high-contrast-light` was dumped
and grabbed at 1280×800, then both images were opened. The dump showed
`Box#galleryHeader 0,0 1280x40` and the two new preset choices as
`AbstractButton#segment_3 224,0 123x22` and `segment_4 347,0 120x22`.
The visible gallery text, selected choice, focus/accent colours and widget
borders were legible in both captures. The ignored PNGs are
`build/gallery-high-contrast-{dark,light}.png` in the integration worktree.

## Still open

Audit the remaining custom composites for F04, including native assistive
bridge semantics where applicable. FND-07 still requires broad high-contrast,
text-scale and hit-target workflow coverage;
[D-016](../../../decisions.md#d-016--keep-toolkit-theme-ownership-process-wide)
records the conditional FND-08 decision to retain process-wide theming;
FND-09
requires popup bounds and focus-return behavior; FND-10 requires uniform
editing state and notification contracts. No WP-007 acceptance is claimed.
