// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1280
    height: 900
    color: Tk.Theme.color.canvas
    property string gestureId: ""

    Tk.ColorGradePreviewModel { id: fixture }
    Tk.ColorGradeEditSession {
        id: session
        clipId: "fixture-clip"
        frameId: "frame-0001"
        sourceImage: fixture.sourceImage
    }

    function wheelsFor(parameters) {
        return [
            { id: "lift", label: qsTr("Lift · shadows"), x: parameters.lift.x, y: parameters.lift.y,
              minimumX: -1, maximumX: 1, minimumY: -1, maximumY: 1, stepX: 0.01, stepY: 0.01, enabled: true },
            { id: "gamma", label: qsTr("Gamma · midtones"), x: parameters.gamma.x, y: parameters.gamma.y,
              minimumX: -1, maximumX: 1, minimumY: -1, maximumY: 1, stepX: 0.01, stepY: 0.01, enabled: true },
            { id: "gain", label: qsTr("Gain · highlights"), x: parameters.gain.x, y: parameters.gain.y,
              minimumX: -1, maximumX: 1, minimumY: -1, maximumY: 1, stepX: 0.01, stepY: 0.01, enabled: true }
        ]
    }
    function candidate(wheel, x, y) {
        const next = Object.assign({}, session.parameters)
        next[wheel] = { x: x, y: y }
        return next
    }
    function preview(gesture, wheel, x, y) {
        if (!session.editing) {
            root.gestureId = gesture
            session.beginGesture(gesture)
        }
        const accepted = session.preview(gesture, candidate(wheel, x, y))
        root.statusText = accepted ? qsTr("Preview · one edit gesture") : session.errorMessage
    }
    property string statusText: qsTr("Tagged sRGB frame · ready")

    Component.onCompleted: {
        const id = "fixture-seed"
        const sample = Object.assign({}, session.parameters, { lift: { x: 0.24, y: -0.12 }, gain: { x: 0.08, y: 0.02 } })
        session.beginGesture(id)
        session.preview(id, sample)
        session.finishGesture(id)
        root.statusText = qsTr("Sample grade committed · use Undo or drag a wheel")
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.md
            Tk.Heading { level: 1; text: qsTr("CPU color grade session"); Tk.Flex.grow: 1 }
            Tk.Button { text: qsTr("Undo"); enabled: session.canUndo; tooltip: qsTr("Undo the last grade gesture"); onClicked: session.undo() }
            Tk.Button { text: qsTr("Redo"); enabled: session.canRedo; tooltip: qsTr("Redo the grade gesture"); onClicked: session.redo() }
            Tk.Badge { text: qsTr("Host frame fixture"); variant: "accent" }
        }
        Tk.Notice {
            variant: "info"
            title: qsTr("Declared processing space")
            text: qsTr("Tagged sRGB input converts to linear sRGB for grade math, then to tagged sRGB output. The fixture demonstrates edit history; it does not claim broadcast, codec, or display calibration fidelity.")
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Item {
                implicitWidth: Tk.Theme.size.panelMinWidth * 2
                implicitHeight: Tk.Theme.size.waveformHeight * 6
                Tk.Flex.shrink: 0
                Tk.ColorWheels {
                    id: wheels
                    anchors.fill: parent
                    gradeId: session.clipId
                    wheels: root.wheelsFor(session.parameters)
                    selectedWheelId: "lift"
                    editingEnabled: true
                    tooltip: qsTr("Host-defined lift, gamma and gain parameters")
                    statusText: root.statusText
                    onWheelPreviewRequested: function(gesture, gradeId, wheel, x, y) { root.preview(gesture, wheel, x, y) }
                    onWheelEditFinished: function(gesture, gradeId, wheel, x, y) {
                        if (session.editing) session.preview(gesture, root.candidate(wheel, x, y))
                        if (session.finishGesture(gesture)) root.statusText = qsTr("Grade gesture committed · one undo step")
                    }
                    onWheelEditCanceled: function(gesture) {
                        if (session.editing) session.cancelGesture(gesture)
                        root.statusText = qsTr("Gesture canceled · committed pixels restored")
                    }
                    onResetRequested: function(gradeId, wheel) {
                        const id = "reset-" + wheel
                        session.beginGesture(id)
                        session.preview(id, root.candidate(wheel, 0, 0))
                        session.finishGesture(id)
                        root.statusText = qsTr("%1 reset · one undo step").arg(wheel)
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Label { text: qsTr("Exposure"); Accessible.role: Accessible.StaticText }
                Tk.NumberField {
                    objectName: "gradeExposure"
                    value: session.parameters.exposureEV
                    from: -8; to: 8; decimals: 2; stepSize: 0.1; suffix: " EV"
                    tooltip: qsTr("Exposure in stops")
                    onValueModified: function(value) {
                        const id = "exposure-" + Date.now()
                        session.beginGesture(id)
                        const next = Object.assign({}, session.parameters, { exposureEV: value })
                        session.preview(id, next)
                        session.finishGesture(id)
                        root.statusText = qsTr("Exposure committed · one undo step")
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { title: qsTr("BEFORE · TAGGED SRGB"); wide: true }
                Image { objectName: "gradeBeforeImage"; source: session.beforeImageUrl; fillMode: Image.PreserveAspectFit; width: 440; height: 190; cache: false }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { title: qsTr("AFTER · SESSION OUTPUT"); wide: true }
                Image { objectName: "gradeAfterImage"; source: session.afterImageUrl; fillMode: Image.PreserveAspectFit; width: 440; height: 190; cache: false }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.VideoScopes { objectName: "gradeBeforeScopes"; Tk.Flex.flex: 1; snapshot: session.beforeScopes; scopeMode: "histogram"; tooltip: qsTr("Before frame histogram · encoded sRGB luma") }
            Tk.VideoScopes { objectName: "gradeAfterScopes"; Tk.Flex.flex: 1; snapshot: session.afterScopes; scopeMode: "histogram"; tooltip: qsTr("Fixture reference histogram") }
        }
        Tk.Label { text: session.errorMessage || root.statusText; color: Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
    }
}
