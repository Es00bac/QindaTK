// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1280
    height: 900
    color: Tk.Theme.color.canvas
    property var grade: ({ exposureEV: 0, lift: { x: 0.08, y: -0.04 },
                          gamma: { x: -0.03, y: 0.06 }, gain: { x: 0.02, y: 0.01 } })
    property var previewGrade: grade
    property string statusText: qsTr("Linear sRGB grade fixture · ready")
    property var wheelDefaults: ({ lift: { x: 0, y: 0 }, gamma: { x: 0, y: 0 }, gain: { x: 0, y: 0 } })

    Tk.ColorGradePreviewModel { id: renderer }

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
    function replaceWheel(wheel, x, y) {
        const next = Object.assign({}, root.previewGrade)
        next[wheel] = { x: x, y: y }
        root.previewGrade = next
        const result = renderer.render(next)
        root.statusText = result.ok ? qsTr("Live preview · generation %1").arg(result.generation) : result.error
    }

    Component.onCompleted: {
        previewGrade = grade
        renderer.render(grade)
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
            Tk.Heading { level: 1; text: qsTr("CPU color grade preview"); Tk.Flex.grow: 1 }
            Tk.Badge { text: qsTr("Fixture · 8-bit RGBA"); variant: "accent" }
        }
        Tk.Notice {
            variant: "info"
            title: qsTr("Declared processing space")
            text: qsTr("Tagged sRGB input converts to linear sRGB for grade math, then to tagged sRGB output. This demonstrates image color management; it does not claim broadcast, codec, or display calibration fidelity.")
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
                    gradeId: "fixture-grade"
                    wheels: root.wheelsFor(root.previewGrade)
                    selectedWheelId: "lift"
                    editingEnabled: true
                    tooltip: qsTr("Host-defined lift, gamma and gain wheel parameters")
                    statusText: root.statusText
                    onWheelPreviewRequested: function(gesture, gradeId, wheel, x, y) { root.replaceWheel(wheel, x, y) }
                    onWheelEditFinished: function(gesture, gradeId, wheel, x, y) {
                        const next = Object.assign({}, root.grade)
                        next[wheel] = { x: x, y: y }
                        root.grade = next
                        root.previewGrade = next
                        renderer.render(next)
                        root.statusText = qsTr("Grade accepted by the local fixture")
                    }
                    onWheelEditCanceled: function(gesture, gradeId, wheel) {
                        root.previewGrade = root.grade
                        renderer.render(root.grade)
                        root.statusText = qsTr("Gesture canceled · committed values restored")
                    }
                    onResetRequested: function(gradeId, wheel) {
                        const next = Object.assign({}, root.grade)
                        next[wheel] = root.wheelDefaults[wheel]
                        root.grade = next
                        root.previewGrade = next
                        renderer.render(next)
                        root.statusText = qsTr("%1 reset").arg(wheel)
                    }
                }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Label { text: qsTr("Exposure"); Accessible.role: Accessible.StaticText }
                Tk.NumberField {
                    objectName: "gradeExposure"
                    value: root.grade.exposureEV
                    from: -8; to: 8; decimals: 2; stepSize: 0.1; suffix: " EV"
                    tooltip: qsTr("Exposure in stops")
                    onValueModified: function(value) {
                        const next = Object.assign({}, root.grade, { exposureEV: value })
                        root.grade = next
                        root.previewGrade = next
                        renderer.render(next)
                        root.statusText = qsTr("Exposure committed by the local fixture")
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
                Image { objectName: "gradeBeforeImage"; source: renderer.beforeImageUrl; fillMode: Image.PreserveAspectFit; width: 440; height: 190; cache: false }
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { title: qsTr("AFTER · DECLARED OUTPUT"); wide: true }
                Image { objectName: "gradeAfterImage"; source: renderer.afterImageUrl; fillMode: Image.PreserveAspectFit; width: 440; height: 190; cache: false }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.VideoScopes { objectName: "gradeBeforeScopes"; Tk.Flex.flex: 1; snapshot: renderer.beforeScopes; scopeMode: "histogram"; tooltip: qsTr("Before image histogram · encoded sRGB luma") }
            Tk.VideoScopes { objectName: "gradeAfterScopes"; Tk.Flex.flex: 1; snapshot: renderer.afterScopes; scopeMode: "histogram"; tooltip: qsTr("After image histogram · encoded sRGB luma") }
        }
        Tk.Label { text: renderer.errorMessage || root.statusText; color: Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
    }
}
