// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Video edit surfaces"
    note: "TrimView · MulticamView · TransitionEditor · exact ticks"

    property string programAngle: "cam-wide"
    property string previewOperation: ""
    property string previewState: "idle"
    property string message: qsTr("Requests are acknowledged by this small demo host.")
    property var transitionData: ({transitionId: "cut-12", leftClipId: "shot-11", rightClipId: "shot-12",
        type: "dissolve", durationTicks: "23543520", alignment: "center", headHandleTicks: "30000000",
        tailHandleTicks: "25000000", parameters: [{id: "softness", label: "Softness", valueText: "0.35", editable: true}]})
    readonly property var cameras: [
        {angleId: "cam-wide", label: "A · Wide", resourceId: "offline-wide", frameId: "24",
         frameTimeTicks: "564677280", syncOffsetTicks: "0", available: true},
        {angleId: "cam-close", label: "B · Close", resourceId: "offline-close", frameId: "24",
         frameTimeTicks: "564677280", syncOffsetTicks: "-7056000", available: true},
        {angleId: "cam-missing", label: "C · Missing", resourceId: "offline-c", frameId: "24",
         frameTimeTicks: "564677280", syncOffsetTicks: "14112000", available: false},
        {angleId: "cam-overhead", label: "D · Overhead", resourceId: "offline-overhead", frameId: "24",
         frameTimeTicks: "564677280", syncOffsetTicks: "0", available: true}
    ]

    GalleryRow {
        label: "Trim point"
        Item {
            implicitWidth: 620
            implicitHeight: trim.implicitHeight
            Tk.TrimView {
                id: trim
                anchors.fill: parent
                edit: ({editId: "trim-demo", leftClipId: "shot-11", rightClipId: "shot-12",
                    leftResourceId: "offline-left", rightResourceId: "offline-right",
                    leftHeadTicks: "0", leftTailTicks: "541380000", rightHeadTicks: "564677280",
                    rightTailTicks: "846720000", legalHead: true, legalTail: true,
                    missingLeft: false, missingRight: false})
                viewId: "gallery-trim"
                deltaTicks: "-23543520"
                previewOperationId: section.previewOperation
                previewStatus: section.previewState
                statusText: section.message
                onPreviewRequested: function(id, editId, mode, ticks) {
                    section.previewOperation = "gallery-preview-1"
                    section.previewState = "ready"
                    section.message = qsTr("Preview accepted · %1 %2 ticks · %3").arg(mode).arg(ticks).arg(id)
                }
                onCommitRequested: function(id, operationId) {
                    section.message = qsTr("Host committed %1 · %2").arg(operationId).arg(id)
                    section.previewOperation = ""
                    section.previewState = "idle"
                }
                onCancelRequested: function(id, operationId) {
                    section.message = qsTr("Host canceled %1 · %2").arg(operationId).arg(id)
                    section.previewOperation = ""
                    section.previewState = "idle"
                }
            }
        }
    }
    GalleryRow {
        label: "Multicam"
        Item {
            implicitWidth: 440
            implicitHeight: multicam.implicitHeight
            Tk.MulticamView {
                id: multicam
                anchors.fill: parent
                angles: section.cameras
                playheadTicks: "564677280"
                activeAngleId: section.programAngle
                viewId: "gallery-program"
                onAngleSwitchRequested: function(id, angleId, ticks) {
                    section.programAngle = angleId
                    section.message = qsTr("Cut to %1 at %2 ticks · %3").arg(angleId).arg(ticks).arg(id)
                }
            }
        }
    }
    GalleryRow {
        label: "Transition"
        Item {
            implicitWidth: 420
            implicitHeight: transition.implicitHeight
            Tk.TransitionEditor {
                id: transition
                anchors.fill: parent
                transition: section.transitionData
                type: String(section.transitionData.type)
                durationTicks: String(section.transitionData.durationTicks)
                alignment: String(section.transitionData.alignment)
                onParameterEditRequested: function(id, transitionId, key, value) {
                    const updated = Object.assign({}, section.transitionData)
                    if (key.indexOf("parameter:") === 0) {
                        const parameterId = key.slice("parameter:".length)
                        updated.parameters = section.transitionData.parameters.map(function(item) {
                            return item.id === parameterId ? Object.assign({}, item, {valueText: value}) : item
                        })
                    } else updated[key] = value
                    section.transitionData = updated
                    section.message = qsTr("Host accepted %1 on %2 · %3").arg(key).arg(transitionId).arg(id)
                }
            }
        }
    }
    Tk.Caption { text: section.message; color: Tk.Theme.color.textMuted; wrapMode: Text.WordWrap }
}
