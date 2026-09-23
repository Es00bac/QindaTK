<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-09 — Popover anchor tracking

**Scope:** localized dynamic-anchor repair for `Tk.Popover`. This evidence
does not claim that the full FND-09 popup policy is complete.

## Finding and change

### Ancestor scrolling

The direct-movement fix above still left a popup behind when an ancestor
viewport scrolled: the anchor's own x/y did not change, so the direct anchor
watchers did not run even though its mapped scene position changed. The new
`ControlsStructure::test_popover_tracks_anchor_while_ancestor_scrolls`
regression failed against the integration source at the expected popup-y
assertion. `Popover.qml` now observes contentX/contentY changes on every
scrolling ancestor and repositions while open. This covers nested viewports
without requiring callers to invoke `reposition()` themselves.

`docs/controls.md` now states that open popovers follow scrolling ancestors.
This remains a focused FND-09 repair and does not claim overall WP-007
acceptance.

Verification for this slice:

- A clean local Debug configure and full `cmake --build build/wp007-popup-audit`
  completed using the configured default Ninja parallelism.
- Before the source repair, `ctest --test-dir build/wp007-popup-audit -R
  '^tst_qml$' --output-on-failure` failed specifically in
  `ControlsStructure::test_popover_tracks_anchor_while_ancestor_scrolls`.
- After the repair, the same focused QtQuick suite passed.
- `python tools/scripts/gen_catalog.py` completed. The source inventory now
  includes the internal `refreshScrollAncestors()` helper on `Popover`.

After integration of the ancestor-scroll repair, Qt 6.11.1 and 6.8.3 each
passed the complete 19-target CTest suite and `all_qmllint` at commit
`c8efd7e`.

An open popover was positioned and clamped when opened or resized, but it did
not follow its anchor when the anchor moved or changed size. The new regression
test failed at that assertion against the unmodified `Popover.qml`.

`Popover.qml` now repositions while open when the anchor's x/y/width/height
changes, the Overlay's width/height changes, or the popover's placement, gap or
arrow setting changes. The existing clamp still keeps the resulting popup
inside the Overlay. `docs/controls.md` records the live-update behavior.

## Verification

- Read the configured build policy: `portageq envvar MAKEOPTS` returned
  `-j24 -l24`. Direct Ninja builds used `-j24 -l24`.
- Debug configure and full build completed in `build/wp007-popups`.
- With the fix temporarily removed, `tst_qml` reported
  `ControlsStructure::test_popover_tracks_anchor_and_stays_inside_small_host`
  failed at the moved-anchor position assertion (line 243 in that run).
- With the fix restored, `ctest --test-dir build/wp007-popups -R '^tst_qml$'`
  passed. The added test uses a 320×200 host, moves the anchor while open,
  places it at the lower-right edge and checks the popup remains within the
  Overlay, then moves it again.
- `ninja -C build/wp007-popups -j24 -l24 all_qmllint` exited 0. It reported
  existing unqualified-access warnings elsewhere in the module.
- `python tools/scripts/gen_catalog.py` completed and left the generated
  catalog unchanged; no generated type metadata changed.

After the earlier direct-movement repair, Qt 6.11.1 and 6.8.3 Debug builds and all 16 then-current
CTest targets passed on each version. A separate `qtk-preview` scene opened
the popover, moved its anchor to the host's lower-right edge, and used a
300ms settle time. The dump showed `Rectangle#movingAnchor 275,170 40x24`
and `PopupItem#Popover 208,138 110x60` within the 320×200 overlay. The
software-rendered PNG was opened and showed the whole popup inside the host.
These ignored artifacts are `build/popover-moving-anchor.qml` and
`build/popover-moving-anchor.png` in the integration worktree.

## Remaining FND-09 work

This change does not qualify focus return, dynamic monitor/work-area changes,
nested menu overflow, or platform popup behavior. Those need separate focused
fixtures and, where native window placement is involved, desktop verification.
