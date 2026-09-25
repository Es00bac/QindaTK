// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-088 qualification consumer scene: imports the installed QindaTK module
// and instantiates one plain control and one optional-engine view. The
// optional view must still instantiate when its engine was omitted.
import QtQuick
import QindaTK as Tk

Item {
    id: root
    objectName: "qualifyRoot"
    width: 360
    height: 200

    readonly property string buttonText: button.text
    readonly property bool sceneRendererAvailable: scene.rendererAvailable
    readonly property string sceneDescription: scene.Accessible.description

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column

        Tk.Button {
            id: button
            objectName: "qualifyButton"
            text: qsTr("Qualify")
            tooltip: text
        }

        Tk.SceneView3D {
            id: scene
            objectName: "qualifyScene"
            Tk.Flex.grow: 1
        }
    }
}
