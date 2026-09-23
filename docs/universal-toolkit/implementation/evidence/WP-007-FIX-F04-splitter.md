<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Splitter pane resize

## Contract

Each `Tk.Splitter` seam is a focusable `Accessible.Splitter`, named by the
control's optional `tooltip` or by the number of the pane immediately before
the seam. Horizontal pane layouts use Left/Right; vertical layouts use Up/Down.
Accessible increase/decrease move in the same signed direction by
`Theme.space.lg`. A disabled splitter rejects both input paths. The seam
updates `QtQuick.Templates.SplitView.preferredWidth` or `preferredHeight` on
its preceding pane while respecting both adjacent panes' minimum and maximum
sizes. Pointer and touch dragging remain owned by the template. The focus
outline surrounds the painted seam without expanding the pane gap.

## Verification

`tests/qml/tst_splitter_targets.qml` checks both orientations, focus/name/role,
arrow and accessible operations, lower and upper bounds, disabled rejection,
and a second seam in a three-pane layout. Its existing mouse and synthetic
touch cases continue to check expanded targets and template state restore.
The Qt 6.11.1 and 6.8.3 QML suites passed after the implementation; final
integrated full-suite and lint results will be recorded in WP-007 progress.

A focused Compact preview dump reports `Splitter#reviewSplitter 0,0 360x180`,
`Item#firstPane 0,0 160x180`, `Item#secondPane 164,0 196x180`,
`Rectangle#splitterHandle 160,0 4x180`, `Rectangle#splitterFocusRing -2,-2
8x184`, and `MouseArea#splitterHitTarget -6,0 16x180`. The PNG grab was
opened: the focus colour is visible around the 4px seam; the pane gap and
contents keep their original geometry.

## Limits

Native screen-reader action delivery and physical keyboard qualification
remain WP-092 work. This slice establishes the QML operation path and tests
both supported Qt versions.
