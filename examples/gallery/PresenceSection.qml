// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Presence layer"
    note: "Remote cursors, selection and activity on host-mapped anchors"
    Tk.Box {
        implicitWidth: 560
        implicitHeight: 230
        color: Tk.Theme.color.canvas
        borderWidth: Tk.Theme.size.border
        borderColor: Tk.Theme.color.divider
        Item {
            id: surface
            objectName: "presenceDemoSurface"
            anchors.fill: parent
            Tk.Flex {
                x: Tk.Theme.space.lg
                y: Tk.Theme.space.lg
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Heading { text: "Scene notes"; level: 3 }
                Tk.Label { text: "Intro scene: reveal the observatory at sunrise." }
                Tk.Label { text: "Sound cue: wind fades below the narration." }
            }
            Tk.PresenceLayer {
                id: presence
                anchors.fill: parent
                mapper: function(anchor, kind, packet) {
                    if (anchor.objectId === "scene/intro")
                        return kind === "cursor" ? {x: 125, y: 91, height: 20}
                                                 : {x: 125, y: 91, width: 235, height: 20}
                    if (anchor.objectId === "scene/sound")
                        return {x: 105, y: 132, height: 20}
                    return null
                }
                Component.onCompleted: {
                    updatePeer("alice", {label: "Alice", activity: "Selecting intro",
                              cursor: {objectId: "scene/intro"},
                              selection: {objectId: "scene/intro"}})
                    updatePeer("bo", {label: "Bo", activity: "Editing sound",
                              cursor: {objectId: "scene/sound"}})
                }
            }
        }
    }
}
