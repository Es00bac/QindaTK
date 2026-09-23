<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-09 — Monitor and work-area qualification gap

`Tk.Menu` currently caps its natural height using `parent.Screen.height`.
That is the full height of the screen containing its parent item, including
space reserved by a taskbar or system menu. Qt's QML `Screen` attached type
offers `desktopAvailableHeight`, but that describes the available height of
the *virtual desktop*, not the work area of the particular screen holding a
menu. [QML Screen](https://doc.qt.io/qt-6/qml-qtquick-screen.html) and
[QScreen](https://doc.qt.io/qt-6/qscreen.html) document the distinction.

The native per-screen source of truth is `QScreen::availableGeometry()`. Qt
notes that on X11 it may equal the full geometry for multi-monitor setups
because the window manager protocol cannot always report a true per-screen
work area. This is a platform limit, not a geometry value QindaTK can infer
from `Screen.height`. The popup implementation also differs between
`Popup.Item` and `Popup.Window` (Qt 6.8 and newer); see
[Qt Popup positioning](https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html).

**Required follow-up before FND-09 acceptance:** expose per-screen available
geometry from a small C++/QML bridge using the screen at the popup's anchor,
keep the cap live across monitor and scale changes, and test a long menu on
two screens with different scale/work-area settings. The current headless
small-window tests qualify Overlay bounds, nested menu scrolling and focus;
they do not qualify native monitor placement. No multi-monitor behavior is
claimed by this note.
