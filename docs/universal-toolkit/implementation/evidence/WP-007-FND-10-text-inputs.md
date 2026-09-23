<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-10 — Text input editing semantics slice

**Scope:** `TextField`, `SearchField`, and `TextArea` only. This is a focused
FND-10/FIX-F04 repair, not acceptance of WP-007 or a uniform editing contract
for every control.

## Change

`TextField` now applies inherited `readOnly` and `enabled` state to its custom
clear paths: the clear button is hidden, `clearText()` is a no-op, and Escape
does not clear when editing is blocked. Its accessible description reports
read-only and error state together. `TextArea` exposes the same read-only/error
description.

`SearchField` now debounces Qt's user-edit signal instead of every `text`
property change. Host-side text synchronization is silent; Enter still searches
immediately, and an explicit clear still searches for the empty string.

`TextArea` remains a live editor, not a transactional draft field. Typing
updates owner-visible `text` immediately, and Escape does not restore an earlier
value. Its inherited `editingFinished` fires when focus is lost even when the
owner assigned `text` during that focus session, so it denotes session end and
cannot serve as a user-change notification. Qt's inherited `textEdited` is
available since Qt 6.9; it cannot be the portable QindaTK contract while Qt
6.8 remains supported. No parser, reset default, mixed state, or rollback path
is added for this plain text component.

## Evidence

- `portageq envvar MAKEOPTS` returned `-j24 -l24`; the direct Ninja build used
  the equivalent `-j24 -l24` limits.
- `cmake --preset dev` configured successfully.
- `cmake --build --preset dev -- -j24 -l24` completed successfully.
- `ctest --preset dev -R '^tst_qml$'` passed (1/1 test target, including the
  new read-only clear and SearchField regressions).
- Focused `tst_qml` passed on Qt 6.11.1 and Qt 6.8.3 with TextArea cases for
  immediate user edits, owner assignment, focus-loss `editingFinished`,
  read-only/disabled guards, and Escape behavior.
- `python tools/scripts/gen_catalog.py` regenerated `docs/catalog.json`.

## Limits

These QML tests verify control behavior under headless Qt 6.11.1. Native
screen-reader announcements and behavior under a QindaQt desktop theme remain
unverified. These controls still do not define reset/default or invalid-input
semantics; those need separate decisions and implementation.
