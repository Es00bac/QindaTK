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

## Still open

Audit the remaining custom composites for F04, including native assistive
bridge semantics where applicable. FND-07 requires high-contrast/text-scale
and hit-target behavior; FND-08 requires a scoped-theme decision; FND-09
requires popup bounds and focus-return behavior; FND-10 requires uniform
editing state and notification contracts. No WP-007 acceptance is claimed.
