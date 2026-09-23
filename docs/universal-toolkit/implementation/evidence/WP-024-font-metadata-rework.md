<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-024 font metadata and specimen follow-up

**State: isolated implementation evidence; Sol owns integrated acceptance.**
The worktree branched from `4b980ec` on `work/universal-implementation`.
The base branch contains the QML pickers but not the uncommitted FontCatalog
registration and implementation in the integration worktree. This follow-up
includes those foundation files and the font-specific repair.

## Source behavior

`FontCatalog.faceMetadata` reads the selected installed face through
`QRawFont`, validates the OpenType `fvar` axis records and `GSUB`/`GPOS`
feature lists, and returns exact axis ranges/defaults and distinct supported
feature tags. A static face reports a known empty axis set. A malformed table
does not advertise metadata. Missing fonts retain their requested family and
expose the system fallback. Glyph coverage and per-script shaping are not
claimed. `FontFeaturesEditor` can consume this service directly; editing
still requires host property bindings. Without a binding, axes show their
font-table default/range and feature checks show unknown state. `FontPicker`
renders a theme-sized specimen in the chosen face or explicit fallback.

## Focused checks

- Environment: Qt 6.11.1, GCC 15.3, Gentoo, Debug/Ninja, offscreen/software
  scene graph; actual Portage `MAKEOPTS` was `-j24 -l24`. No local override.
- `tst_font_catalog`: 5 passed, 0 failed. The installed-face test found an
  actual layout-feature face and an actual variable face, validated tags and
  axis ordering; no fixture was skipped on this machine.
- Focused `TypographyCalendar` cases for installed metadata/specimen, missing
  fallback, and unsupported metadata: 5 passed, 0 failed including
  init/cleanup. `qindatk_qmllint` exited zero. The base branch's other QML
  cases depend on separate uncommitted integration services and were not used
  as evidence for this follow-up.
- `qtk-preview` rendered and the PNGs were opened and inspected at compact
  and comfortable density. The inspected files are
  `build/wp024/font-compact.png` and `build/wp024/font-comfortable.png` in the
  isolated worktree. The specimen is visible, the selected Newsreader face
  exposes `wght` and `opsz`, and unbound settings do not pretend to show a
  document value.

Representative dump lines at 760×650:

```text
compact:     FontPicker#previewFontPicker  12,54 736x102
compact:       Box#fontSpecimen  0,78 736x24
compact:     FontFeaturesEditor#previewFontFeatures  12,164 736x292
compact:       PropertyRow#fontAxis_wght  0,0 736x24
comfortable: FontPicker#previewFontPicker  14,60 732x119
comfortable:   Box#fontSpecimen  0,93 732x26
comfortable: FontFeaturesEditor#previewFontFeatures  14,189 732x346
comfortable:   PropertyRow#fontAxis_opsz  0,31 732x29
```

Sol must re-run the integrated WP-024 fixture, supported-Qt checks and
installed-consumer check after cherry-picking. The public model reports
font-level feature presence; applications needing script/language/glyph
eligibility still need their shaping engine's metadata and fidelity checks.
