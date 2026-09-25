// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The four scopes of examples/grading-scopes-live, all painting one
// VideoScopesModel snapshot. The model is bound to the MediaPlayer, so each
// snapshot measures the presented frame after the program's finishing (grade,
// then burned-in captions) and carries its presentedFrameKey as frameId.
Tk.Panel {
    id: panel
    objectName: "gradingScopesPanel"
    title: qsTr("Scopes · presented frame")
    iconName: "activity"

    required property Tk.VideoScopesModel scopes
    readonly property var snapshot: scopes.snapshot
    readonly property var ranges: ["source", "full", "limited"]
    readonly property var matrices: ["source", "bt709", "bt601"]

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            objectName: "gradingScopeConventions"
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Range"); color: Tk.Theme.color.textMuted }
            Tk.Segmented {
                objectName: "gradingScopeRange"
                small: true
                model: [{text: qsTr("Source"), value: "source"}, {text: qsTr("Full"), value: "full"},
                        {text: qsTr("Limited"), value: "limited"}]
                currentIndex: Math.max(0, panel.ranges.indexOf(panel.scopes.signalRange))
                tooltip: qsTr("8-bit code range the scopes measure in")
                onActivated: function(index) { panel.scopes.signalRange = panel.ranges[index] }
            }
            Tk.Caption { text: qsTr("Matrix"); color: Tk.Theme.color.textMuted }
            Tk.Segmented {
                objectName: "gradingScopeMatrix"
                small: true
                model: [{text: qsTr("Source"), value: "source"}, {text: "709", value: "bt709"},
                        {text: "601", value: "bt601"}]
                currentIndex: Math.max(0, panel.matrices.indexOf(panel.scopes.matrix))
                tooltip: qsTr("Y′CbCr coefficients for the waveform and vectorscope")
                onActivated: function(index) { panel.scopes.matrix = panel.matrices[index] }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Mono {
                objectName: "gradingScopeLevels"
                text: panel.snapshot.valid
                      ? qsTr("Y′ %1–%2").arg(panel.snapshot.levels.yMin).arg(panel.snapshot.levels.yMax)
                      : qsTr("no frame")
                color: Tk.Theme.color.textMuted
            }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) minmax(0, 1fr)"
            rows: "minmax(0, 1fr) minmax(0, 1fr)"
            gap: Tk.Theme.space.sm
            Tk.VideoScopes {
                objectName: "gradingWaveform"
                snapshot: panel.snapshot
                scopeMode: "waveform"
                tooltip: qsTr("Waveform: Y′ codes across the frame width")
            }
            Tk.VideoScopes {
                objectName: "gradingVectorscope"
                snapshot: panel.snapshot
                scopeMode: "vectorscope"
                tooltip: qsTr("Vectorscope: Cb/Cr with 75% and 100% targets; the grade pulls the bars off them")
            }
            Tk.VideoScopes {
                objectName: "gradingParade"
                snapshot: panel.snapshot
                scopeMode: "parade"
                tooltip: qsTr("RGB parade: R′, G′ and B′ codes side by side")
            }
            Tk.VideoScopes {
                objectName: "gradingHistogram"
                snapshot: panel.snapshot
                scopeMode: "histogram"
                tooltip: qsTr("Histogram of Y′ codes")
            }
        }
    }
}
