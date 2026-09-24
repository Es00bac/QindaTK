// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: two modes (docs/controls.md "MulticamView",
// docs/sequence-edits.md "Multicam"). Host mode: a synchronized angle snapshot
// and explicit switch requests; playback, source offsets and cuts belong to
// the host. Multicam mode (`multicam` set): a Tk.MulticamModel supplies the
// angles, playhead and program angle, and a click is a frame-exact
// SwitchSource transaction on its timeline, acknowledged at once.
Item {
    id: root
    property var angles: [] // {angleId,label,resourceId,syncOffsetTicks,frameId,available,active}
    property Tk.MulticamModel multicam: null
    property var assetProvider: null
    property var document: null
    property string viewId: ""
    property string playheadTicks: "0"
    property string rateNumerator: "24"
    property string rateDenominator: "1"
    property string activeAngleId: ""
    property string pendingRequestId: ""
    property string statusText: ""
    property string errorText: ""
    property string tooltip: qsTr("Synchronized multicamera angles")
    property string _serial: "0"

    signal angleSwitchRequested(string requestId, string angleId, string atTicks)
    signal seekAngleRequested(string angleId, string frameId, string atTicks)
    signal switchAcknowledged(string requestId, bool accepted, string activeAngleId, string message)

    readonly property bool multicamMode: root.multicam !== null
    readonly property var effectiveAngles: root.multicamMode ? root.multicam.angles : root.angles
    readonly property string effectivePlayheadTicks: root.multicamMode ? root.multicam.playheadTicks : root.playheadTicks
    readonly property string effectiveActiveAngleId: root.multicamMode ? root.multicam.activeAngleId : root.activeAngleId
    readonly property string effectiveRateNumerator: root.multicamMode ? root.multicam.frameRateNumerator : root.rateNumerator
    readonly property string effectiveRateDenominator: root.multicamMode ? root.multicam.frameRateDenominator : root.rateDenominator
    readonly property string effectiveError: root.multicamMode && root.multicam.lastError.length ? root.multicam.lastError : root.errorText
    readonly property int angleCount: effectiveAngles ? effectiveAngles.length : 0
    readonly property bool snapshotValid: root.validateAngles()

    objectName: "multicamView"
    implicitWidth: Tk.Theme.size.thumbnail * 5
    implicitHeight: Tk.Theme.size.thumbnailHeight * 3
        + Tk.Theme.size.controlLg * 2 + Tk.Theme.space.lg * 3
    Accessible.role: Accessible.Pane
    Accessible.name: root.tooltip
    Accessible.description: root.effectiveError.length ? root.effectiveError
        : !snapshotValid ? qsTr("Angle snapshot has invalid IDs, sync offsets, or playhead time.")
        : qsTr("%1 angles. Active angle %2 at %3 ticks.").arg(angleCount).arg(effectiveActiveAngleId).arg(effectivePlayheadTicks)

    function isTicks(value) { return typeof value === "string" && /^-?[0-9]+$/.test(value) }
    function validateAngles() {
        const list = root.effectiveAngles
        if (!list || list.length === 0 || !isTicks(String(root.effectivePlayheadTicks))) return false
        const ids = Object.create(null)
        for (let i = 0; i < list.length; ++i) {
            const item = list[i], id = String(item.angleId || "")
            if (!id.length || ids[id] || !isTicks(String(item.syncOffsetTicks))) return false
            ids[id] = true
        }
        // A gap in the program has no active angle; switching needs a clip there.
        return root.multicamMode || ids[root.effectiveActiveAngleId] === true
    }
    function angleById(id) {
        for (let i = 0; i < angleCount; ++i)
            if (String(effectiveAngles[i].angleId) === String(id)) return effectiveAngles[i]
        return null
    }
    function requestId() {
        root._serial = String(Number(root._serial) + 1)
        return (viewId.length ? viewId : "multicam") + ":switch:" + root._serial
    }
    function switchTo(id) {
        const angle = angleById(id)
        if (!enabled || !snapshotValid || !angle || angle.available === false
            || String(id) === effectiveActiveAngleId || pendingRequestId.length > 0) return false
        const request = requestId()
        if (root.multicamMode) {
            const result = root.multicam.switchAngle(String(angle.angleId), String(root.effectivePlayheadTicks))
            switchAcknowledged(request, result.ok === true, root.multicam.activeAngleId,
                               String(result.ok === true ? "" : result.error || ""))
            return result.ok === true
        }
        angleSwitchRequested(request, String(angle.angleId), String(playheadTicks))
        return true
    }
    function requestSeek(id) {
        const angle = angleById(id)
        const at = String(angle ? angle.frameTimeTicks || effectivePlayheadTicks : "")
        if (!enabled || !snapshotValid || !angle || angle.available === false || !isTicks(at)) return false
        seekAngleRequested(String(angle.angleId), String(angle.frameId || ""), at)
        return true
    }
    function acknowledgeSwitch(requestId, accepted, confirmedAngleId, message) {
        if (!root.enabled || String(requestId).length === 0 || String(requestId) !== root.pendingRequestId) return false
        switchAcknowledged(String(requestId), Boolean(accepted), String(confirmedAngleId || ""), String(message || ""))
        return true
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Overline { text: qsTr("MULTICAM") }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("PROGRAM · %1").arg(root.effectiveActiveAngleId); color: Tk.Theme.color.accent }
        }
        Tk.Caption {
            objectName: "multicamSyncReadout"
            text: root.multicamMode
                  ? qsTr("Playhead %1 ticks · a switch cuts the program at this frame").arg(root.effectivePlayheadTicks)
                  : qsTr("Playhead %1 ticks · switch requests are cut at the acknowledged time").arg(root.playheadTicks)
            color: Tk.Theme.color.textMuted
        }
        Tk.Grid {
            id: angleGrid
            objectName: "multicamAngleGrid"
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "1fr 1fr"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.sm
            Repeater {
                model: root.effectiveAngles || []
                delegate: T.AbstractButton {
                    id: card
                    required property var modelData
                    readonly property string angleId: String(modelData.angleId)
                    readonly property bool isProgram: angleId === root.effectiveActiveAngleId
                    objectName: "multicamAngle_" + angleId
                    enabled: root.snapshotValid && modelData.available !== false && !isProgram
                    implicitHeight: Tk.Theme.size.thumbnailHeight + Tk.Theme.size.controlLg * 2
                    implicitWidth: Tk.Theme.size.thumbnail * 2
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Switch program to angle %1").arg(String(modelData.label || angleId))
                    Accessible.description: (isProgram ? qsTr("Current program angle. ") : "")
                        + (modelData.available === false ? qsTr("Media missing.") : qsTr("Sync offset %1 ticks.").arg(String(modelData.syncOffsetTicks)))
                    Accessible.focusable: enabled
                    focusPolicy: enabled ? Qt.TabFocus : Qt.NoFocus
                    hoverEnabled: true
                    onClicked: root.switchTo(angleId)
                    background: Rectangle {
                        color: card.isProgram ? Tk.Theme.color.selection : Tk.Theme.color.panelAlt
                        border.width: card.activeFocus || card.isProgram ? Tk.Theme.size.border : Tk.Theme.size.border
                        border.color: card.activeFocus ? Tk.Theme.color.focus : card.isProgram ? Tk.Theme.color.accent : Tk.Theme.color.controlBorder
                        radius: Tk.Theme.radius.sm
                    }
                    contentItem: Tk.Flex {
                        padding: Tk.Theme.space.xs
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.AssetThumbnail {
                            objectName: "multicamFrame_" + card.angleId
                            provider: root.assetProvider
                            resourceId: String(modelData.resourceId || "")
                            requestKind: "frame"
                            requestOptions: ({frameId: String(modelData.frameId || ""),
                                timeTicks: String(modelData.frameTimeTicks || root.effectivePlayheadTicks),
                                rateNumerator: root.effectiveRateNumerator, rateDenominator: root.effectiveRateDenominator,
                                size: ({width: Math.max(1, card.width), height: Tk.Theme.size.thumbnailHeight})})
                            document: root.document
                            viewId: root.viewId + ":angle:" + card.angleId
                            active: root.enabled && modelData.available !== false && !!root.assetProvider
                            crop: true
                            placeholderIcon: "video"
                            caption: ""
                            tooltip: qsTr("Angle %1 frame %2").arg(String(modelData.label || card.angleId)).arg(String(modelData.frameId || "—"))
                            width: parent.width
                            height: Tk.Theme.size.thumbnailHeight
                        }
                        Tk.Flex {
                            width: parent.width
                            Tk.Label { text: String(modelData.label || card.angleId); color: Tk.Theme.color.text; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                            Tk.Badge {
                                visible: card.isProgram
                                text: qsTr("PROGRAM")
                                variant: "accent"
                                tooltip: qsTr("Host-confirmed program angle")
                            }
                        }
                        Tk.Caption {
                            text: modelData.available === false ? qsTr("Media missing")
                                : qsTr("Offset %1 ticks · frame %2").arg(String(modelData.syncOffsetTicks)).arg(String(modelData.frameId || "—"))
                            color: modelData.available === false ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
                            elide: Text.ElideRight
                        }
                    }
                    Keys.onReturnPressed: root.switchTo(angleId)
                    Keys.onEnterPressed: root.switchTo(angleId)
                    Keys.onSpacePressed: root.switchTo(angleId)
                    Tk.ToolTip { text: card.Accessible.name; visible: card.hovered }
                }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Caption {
                objectName: "multicamStatus"
                text: root.effectiveError.length ? root.effectiveError : root.statusText
                color: root.effectiveError.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
                wrapMode: Text.WordWrap
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "multicamSwitchActive"
                text: qsTr("Program: %1").arg(root.effectiveActiveAngleId || qsTr("none"))
                enabled: false
                tooltip: qsTr("The program angle changes only after host acknowledgment")
            }
        }
    }
}
