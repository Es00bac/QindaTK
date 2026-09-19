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
