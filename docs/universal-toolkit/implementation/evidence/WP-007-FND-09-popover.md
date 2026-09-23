<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-09 — Popover anchor tracking

**Scope:** localized dynamic-anchor repair for `Tk.Popover`. This evidence
does not claim that the full FND-09 popup policy is complete.

## Finding and change

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

## Remaining FND-09 work

This change does not qualify focus return, dynamic monitor/work-area changes,
nested menu overflow, or platform popup behavior. Those need separate focused
fixtures and, where native window placement is involved, desktop verification.
