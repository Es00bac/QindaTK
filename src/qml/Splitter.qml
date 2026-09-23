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
        objectName: "splitterHandle"
        implicitWidth: splitter.orientation === Qt.Horizontal ? splitter.handleSize : splitter.width
        implicitHeight: splitter.orientation === Qt.Horizontal ? splitter.height : splitter.handleSize
        color: T.SplitHandle.pressed || T.SplitHandle.hovered ? Tk.Theme.color.seamHover
                                                              : Tk.Theme.color.seam
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        // AGENT-CONTRACT: SplitView filters pointer events delivered to this
        // child; its handle lookup uses the matching containment mask. The
        // child gives Qt's delivery agent actual bounds outside the seam.
        containmentMask: targetArea
        MouseArea {
            id: targetArea
            objectName: "splitterHitTarget"
            x: splitter.orientation === Qt.Horizontal ? (seam.width - width) / 2 : 0
            y: splitter.orientation === Qt.Horizontal ? 0 : (seam.height - height) / 2
            width: splitter.orientation === Qt.Horizontal ? Math.max(seam.width, Tk.Theme.size.seamTarget) : seam.width
            height: splitter.orientation === Qt.Horizontal ? seam.height : Math.max(seam.height, Tk.Theme.size.seamTarget)
            acceptedButtons: Qt.LeftButton
        }
    }
}
