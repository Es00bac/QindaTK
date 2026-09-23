<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-09 — Native menu work-area contract

`Tk.Menu` now caps its natural height with `Tk.ScreenWorkArea.availableGeometry`
minus its two margins, replacing the full-monitor `parent.Screen.height` cap.
The C++ observer resolves the popup’s parent-local `(x, y)` to a global anchor
point and uses the owning window’s `QScreen::virtualSiblingAt` to select the
screen. A desktop gap falls back to the window screen. It reads that screen’s
`availableGeometry()`, never the union of the virtual desktop.

The bridge observes item/ancestor position and parenting, window position and
screen, screen geometry/work-area/DPI changes, and GUI-thread frame updates
(for arbitrary ancestor transforms). Menu refreshes synchronously before
opening. An anchor without a window returns an empty rectangle. Native popup
placement/flipping remains owned by Qt; the bridge constrains natural height
before Popup.Window is created or resized. Geometry stays in Qt logical
coordinates without multiplying by DPR.

## Evidence

`tst_screen_work_area` adds synthetic QPA screens with different full and
available rectangles, moves the popup anchor between screens while keeping
its owning window on the original screen, changes the selected screen’s
reserved area, and verifies live updates. Other cases cover missing/destroyed
anchors, detachment/reparenting, off-desktop fallback, and the actual Menu
implicit-height binding. QPA private APIs exist only in this test; production
uses public Qt APIs supported on Qt 6.8 and 6.11.

A 50-item Popup.Item preview in a 400×1000 host on the offscreen platform
reported `PopupItem#Menu 20,20 160x796` and `ListView 2,2 156x792`.
The 800-logical-pixel screen work area minus two 2px margins caps the menu,
while the list scrolls. The generated `build/menu/workarea.png` was opened
and inspected. This preview checks bounded visuals only; Popup.Item placement
uses its host overlay, not native monitor placement.

Validation: Qt 6.11.1 and Qt 6.8.3 builds completed; both full CTest suites
passed 20/20. The updated synthetic DPR/ancestor-motion test also passed on
Qt 6.11.1 after its final edit. `all_qmllint` exited 0 on both versions
(existing unrelated warnings remain). Direct builds inherited the job/load
values read from system `MAKEOPTS` without overriding it.

## Platform qualification still required (WP-092)

A physical two-screen run with different scale factors and reserved areas is
still required for native popup placement, compositor relocation, screen
removal, nested menus, and runtime scale changes. Headless synthetic geometry
checks establish the source contract, not native compositor behavior.

Qt documents that `QScreen::availableGeometry()` may equal full geometry on
multi-monitor X11 because its window-manager protocol cannot always report
per-screen reserved areas. QindaTK preserves that platform result instead of
inventing unavailable bounds. See [QScreen](https://doc.qt.io/qt-6/qscreen.html),
[QML Screen](https://doc.qt.io/qt-6/qml-qtquick-screen.html), and
[Qt Popup positioning](https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html).
