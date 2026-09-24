// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1080
    height: 650
    property var renderState: ({ before: false, after: false })

    function drawFrames() {
        const before = sourceRenderer.render("0", 640, 360)
        const after = effectRenderer.render("0", 640, 360)
        root.renderState = { before: before.ok, after: after.ok }
    }

    Tk.MotionComposition {
        id: sourceComposition
        layers: [{layerId: "sample-circle", sourceId: "circle", zIndex: 0, visible: true,
                 transform: ({x: 40, y: 35, scaleX: 5, scaleY: 5, opacity: 1})}]
    }
    Tk.MotionComposition {
        id: effectComposition
        layers: [{layerId: "sample-circle", sourceId: "circle", zIndex: 0, visible: true,
                 transform: ({x: 40, y: 35, scaleX: 5, scaleY: 5, opacity: 1}),
                 effects: [
                     {effectId: "grayscale", enabled: true, parameters: ({})},
                     {effectId: "brightness", enabled: true, parameters: ({amount: 0.10})}
                 ]}]
    }
    Tk.MotionFrameRenderer {
        id: sourceRenderer
        composition: sourceComposition
        Component.onCompleted: loadSource("circle", Qt.resolvedUrl("../motion-authoring/orbit.svg"))
    }
    Tk.MotionFrameRenderer {
        id: effectRenderer
        composition: effectComposition
        Component.onCompleted: {
            loadSource("circle", Qt.resolvedUrl("../motion-authoring/orbit.svg"))
            Qt.callLater(root.drawFrames)
        }
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 1; text: qsTr("Ordered motion effects") }
                Tk.Caption { text: qsTr("Host supplied source · deterministic encoded sRGB operations"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Badge { text: qsTr("CPU preview"); variant: "accent" }
        }
        Tk.Notice {
            variant: "info"
            title: qsTr("Effect order is saved layer order")
            text: qsTr("Grayscale runs first, then brightness adds 10% encoded sRGB. Unsupported enabled effects fail rendering; disabled records are skipped.")
        }
        Tk.Grid {
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.md
            Tk.Flex.grow: 1
            Tk.Panel {
                title: qsTr("Source pixels")
                iconName: "image"
                padding: Tk.Theme.space.md
                Tk.CompositionView { anchors.fill: parent; renderer: sourceRenderer; tooltip: qsTr("Tagged sRGB source composite before effects") }
            }
            Tk.Panel {
                title: qsTr("Effect output")
                iconName: "sparkles"
                padding: Tk.Theme.space.md
                Tk.CompositionView { anchors.fill: parent; renderer: effectRenderer; tooltip: qsTr("Same layer after ordered grayscale and brightness effects") }
            }
        }
        Tk.Caption {
            text: root.renderState.before && root.renderState.after
                ? qsTr("Both frames rendered · effects applied before layer transforms and composition")
                : sourceRenderer.lastError || effectRenderer.lastError || qsTr("Loading local fixture pixels…")
            color: root.renderState.before && root.renderState.after ? Tk.Theme.color.accent : Tk.Theme.color.warning
        }
    }
}
