<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# September 19 overflow repair

Source diagnosis, before compilation: Menu's ListView compared content with
the enclosing window height instead of its own viewport. Qt can shrink an item
popup near a window edge, leaving inaccessible rows while `interactive` stays
false. Separate native popup windows also reapply `implicitHeight` when a menu
layout changes (Qt 6.11.1 `QQuickPopupWindow::implicitHeightChanged`). Capping
only an explicit height does not survive that path.

Menu now bounds its implicit height to the output and derives interactivity
from the actual viewport. Dialog had the same unbounded-content problem for
long forms, with a non-scrolling Box body. It now bounds its implicit height
to its host and uses Scroll for the body. Header/footer, default content alias,
action signals and existing objectNames remain intact; focused controls are
revealed without continuously overriding the user's scroll position.

This is a shared presentation repair for Office, Viewer and other consumers.
All code is written before compilation and final focused verification, as
requested; this diagnosis alone is not a claim of runtime acceptance.

## Final delivery

The reviewed source `d59b080c5b47fbfe35d3984f13be40bf81a0144a` built and is
installed on qinda and qinda-top as `dev-libs/qindatk-0.1.0-r2`. Both installed
ebuild pins match, and each package integrity check reports 114/114 good files.
The existing installed-QML `ControlsStructure::test_menu_and_bar` and
`ControlsStructure::test_dialog_accepts` checks passed with
`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`, and
`QT_FATAL_WARNINGS=1` (4/4 including setup/cleanup). No broad new test suite or
pre-implementation test run was added.
