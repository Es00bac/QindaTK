// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Resizable panes with a 4px seam (Theme.size.seam) that lights in the
// accent when hovered or dragged. Children size themselves with the
// template's attached properties (`SplitView.preferredWidth`,
// `SplitView.minimumWidth`, `SplitView.fillWidth`; import
// `QtQuick.Templates as T` or `QtQuick.Controls as QQC` for the attached
// type). `saveState()`/`restoreState()` come from the template.
T.SplitView {
    id: splitter

    property real handleSize: Tk.Theme.size.seam

    handle: Rectangle {
        id: seam
        implicitWidth: splitter.orientation === Qt.Horizontal ? splitter.handleSize : splitter.width
        implicitHeight: splitter.orientation === Qt.Horizontal ? splitter.height : splitter.handleSize
        color: T.SplitHandle.pressed || T.SplitHandle.hovered ? Tk.Theme.color.seamHover
                                                              : Tk.Theme.color.seam
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        // A wider invisible hit area than the painted seam, so a 4px
        // divider is still easy to grab.
        containmentMask: Item {
            x: splitter.orientation === Qt.Horizontal ? -Tk.Theme.space.xs : 0
            y: splitter.orientation === Qt.Horizontal ? 0 : -Tk.Theme.space.xs
            width: seam.width + (splitter.orientation === Qt.Horizontal ? Tk.Theme.space.xs * 2 : 0)
            height: seam.height + (splitter.orientation === Qt.Horizontal ? 0 : Tk.Theme.space.xs * 2)
        }
    }
}
