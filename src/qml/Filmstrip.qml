// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The frames of a clip, in order — most usefully its first and its last.
//
// AGENT-NOTE: an editor scanning a timeline is reading the cut, and a cut is
// two frames: what the outgoing clip ends on and what the incoming one starts
// on. A strip of evenly spaced frames looks richer and answers a question
// nobody asked; head and tail is the default here for that reason, with more
// frames available when there is width to spend.
//
// AGENT-CONTRACT: `frames` is a list of {source, caption} in display order.
// The strip never reflows them — it shows as many as fit from the front and
// the final one last, so the tail frame is always visible however narrow the
// clip gets. A strip that dropped the tail would hide half of every cut.
Item {
    id: root

    property var frames: []
    property bool crop: true
    property string placeholderIcon: "film"
    // Below this width only the head frame is drawn: two frames squeezed into
    // forty pixels are two smears, which is worse than one readable frame.
    property real minimumFrameWidth: Tk.Theme.size.thumbnail / 2

    readonly property int count: root.frames === undefined || root.frames === null
                                 ? 0 : root.frames.length

    objectName: "filmstrip"
    implicitWidth: Tk.Theme.size.thumbnail * 2 + Tk.Theme.space.xs
    implicitHeight: Tk.Theme.size.thumbnailHeight

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("%1 frames").arg(root.count)

    // How many frames actually fit, tail always included.
    readonly property int shown: {
        if (root.count <= 1) {
            return root.count
        }
        const fit = Math.floor(width / Math.max(1, root.minimumFrameWidth))
        return Math.max(1, Math.min(root.count, fit))
    }

    Row {
        anchors.fill: parent
        spacing: Tk.Theme.space.xs

        Repeater {
            model: root.shown
            delegate: Tk.Thumbnail {
                id: cell
                required property int index
                // Head frames in order, then the tail — so the last cell is
                // the clip's final frame even when most frames are dropped.
                readonly property var frame: {
                    const list = root.frames
                    if (list === undefined || list === null || list.length === 0) {
                        return ({})
                    }
                    const isLastCell = cell.index === root.shown - 1
                    const which = (isLastCell && root.shown > 1)
                                  ? list.length - 1
                                  : Math.min(cell.index, list.length - 1)
                    return list[which] || ({})
                }
                objectName: "filmstripFrame" + cell.index
                width: (root.width - Tk.Theme.space.xs * (root.shown - 1)) / root.shown
                height: root.height
                source: cell.frame.source !== undefined ? cell.frame.source : ""
                caption: cell.frame.caption !== undefined ? cell.frame.caption : ""
                crop: root.crop
                placeholderIcon: root.placeholderIcon
            }
        }
    }
}
