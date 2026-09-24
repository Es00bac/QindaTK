// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned snapshots with an explicit preview/acknowledgment stub. This
// demonstrates request contracts; it does not edit or render a sequence.
Item {
    id: root
    width: 1360
    height: 1120
    property string activeAngle: "cam-a"
    property string trimOperationId: ""
    property string trimPreviewState: "idle"
    property string hostStatus: qsTr("Sequence host ready · 24 fps · 705600000 ticks per second")
    property var angleRows: [
        {angleId: "cam-a", label: "A · Wide", resourceId: "offline-cam-a", frameId: "104", frameTimeTicks: "244944000000000000", syncOffsetTicks: "0", available: true},
        {angleId: "cam-b", label: "B · Close", resourceId: "offline-cam-b", frameId: "104", frameTimeTicks: "244944000000000000", syncOffsetTicks: "-7056000", available: true},
        {angleId: "cam-c", label: "C · Gimbal", resourceId: "offline-cam-c", frameId: "104", frameTimeTicks: "244944000000000000", syncOffsetTicks: "14112000", available: false},
        {angleId: "cam-d", label: "D · Overhead", resourceId: "offline-cam-d", frameId: "104", frameTimeTicks: "244944000000000000", syncOffsetTicks: "0", available: true}
    ]
    property var transitionData: ({transitionId: "transition-021", leftClipId: "clip-017", rightClipId: "clip-018",
        type: "dissolve", durationTicks: "23543520", alignment: "center", headHandleTicks: "23543520",
        tailHandleTicks: "11771760", parameters: [{id: "softness", label: "Softness", valueText: "0.35", editable: true}], missingMedia: false})

    function acknowledgeTransition(parameter, value) {
        const next = Object.assign({}, transitionData)
        if (parameter.indexOf("parameter:") === 0) {
            const id = parameter.slice("parameter:".length)
            next.parameters = transitionData.parameters.map(function(entry) {
                return entry.id === id ? Object.assign({}, entry, {valueText: value}) : entry
            })
        } else next[parameter] = value
        transitionData = next
        hostStatus = qsTr("Host accepted transition %1 = %2 at revision 23").arg(parameter).arg(value)
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Sequence edit surfaces") }
                Tk.Caption { text: root.hostStatus; color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Badge { text: qsTr("HOST SNAPSHOT · REV 22"); variant: "accent"; tooltip: qsTr("The demo host owns all source and transition snapshots") }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1.55fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.md
            Tk.Panel {
                title: qsTr("Trim point")
                iconName: "scissors"
                padding: Tk.Theme.space.sm
                Tk.TrimView {
                    id: trim
                    width: parent.width
                    edit: ({editId: "edit-017-018", leftClipId: "clip-017", rightClipId: "clip-018",
                            leftResourceId: "offline-left", rightResourceId: "offline-right",
                            leftHeadTicks: "205632000000000000", leftTailTicks: "244921456480000000",
                            rightHeadTicks: "244944000000000000", rightTailTicks: "276856560000000000",
                            legalHead: true, legalTail: true, missingLeft: false, missingRight: false})
                    viewId: "sequence-trim"
                    rateNumerator: "24000"
                    rateDenominator: "1001"
                    mode: "roll"
                    deltaTicks: "23543520"
                    previewOperationId: root.trimOperationId
                    previewStatus: root.trimPreviewState
                    statusText: qsTr("Last host preview: roll by 23543520 ticks · source audio linked")
                    errorText: root.trimPreviewState === "rejected" ? qsTr("Host rejected preview: incoming media is offline") : ""
                    onPreviewRequested: function(requestId, editId, mode, deltaTicks) {
                        root.trimOperationId = "trim-op-42"
                        root.trimPreviewState = "ready"
                        root.hostStatus = qsTr("Preview acknowledged · %1 · %2 ticks · request %3").arg(mode).arg(deltaTicks).arg(requestId)
                    }
                    onCommitRequested: function(requestId, operationId) {
                        root.hostStatus = qsTr("Host committed %1 as revision 23 · request %2").arg(operationId).arg(requestId)
                        root.trimOperationId = ""
                        root.trimPreviewState = "idle"
                    }
                    onCancelRequested: function(requestId, operationId) {
                        root.hostStatus = qsTr("Host canceled %1 · request %2").arg(operationId).arg(requestId)
                        root.trimOperationId = ""
                        root.trimPreviewState = "idle"
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Camera angles")
                iconName: "video"
                padding: Tk.Theme.space.sm
                Tk.MulticamView {
                    id: multicam
                    width: parent.width
                    angles: root.angleRows
                    playheadTicks: "244944000000000000"
                    activeAngleId: root.activeAngle
                    viewId: "program-monitor"
                    rateNumerator: "24000"
                    rateDenominator: "1001"
                    statusText: qsTr("Sync offsets remain exact; the host maps offsets against its TimeMap.")
                    onAngleSwitchRequested: function(requestId, angleId, atTicks) {
                        multicam.pendingRequestId = requestId
                        root.activeAngle = angleId
                        multicam.acknowledgeSwitch(requestId, true, angleId, qsTr("Cut accepted by fixture host"))
                        multicam.pendingRequestId = ""
                        root.hostStatus = qsTr("Host cut to %1 at %2 ticks · %3").arg(angleId).arg(atTicks).arg(requestId)
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Transition")
                iconName: "blend"
                padding: Tk.Theme.space.sm
                Tk.TransitionEditor {
                    id: transitionEditor
                    width: parent.width
                    transition: root.transitionData
                    type: String(root.transitionData.type)
                    durationTicks: String(root.transitionData.durationTicks)
                    alignment: String(root.transitionData.alignment)
                    previewState: "ready"
                    statusText: qsTr("Host preview frame: frame 105 · evaluator unavailable in this fixture")
                    onParameterEditRequested: function(requestId, transitionId, parameter, value) {
                        root.acknowledgeTransition(parameter, value)
                        root.hostStatus = qsTr("Host accepted %1 on %2 · request %3").arg(parameter).arg(transitionId).arg(requestId)
                    }
                    onPreviewRequested: function(requestId, transitionId) {
                        root.hostStatus = qsTr("Preview request %1 for %2 · render backend unavailable").arg(requestId).arg(transitionId)
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Host contract")
                iconName: "braces"
                padding: Tk.Theme.space.md
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Overline { text: qsTr("EXACT TIME + STABLE IDS") }
                    Tk.Caption { text: qsTr("Clip, edit, angle, and transition IDs stay host-owned. All canonical times cross this UI as decimal strings."); wrapMode: Text.WordWrap }
                    Tk.Notice {
                        title: qsTr("Backend boundary")
                        text: qsTr("This fixture acknowledges UI requests only. Legal source edits, multicam sync, audio linking, preview rendering, evaluation, undo and persistence belong to the sequence host.")
                        variant: "info"
                    }
                }
            }
        }
    }
}
