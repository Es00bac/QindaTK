// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Synchronized angle snapshot and explicit switch requests. Playback, source
// offsets and cut creation belong to the sequence host.
Item {
    id: root
    property var angles: [] // {angleId,label,resourceId,syncOffsetTicks,frameId,available,active}
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

    readonly property int angleCount: angles ? angles.length : 0
    readonly property bool snapshotValid: root.validateAngles()

    objectName: "multicamView"
    implicitWidth: Tk.Theme.size.thumbnail * 5
    implicitHeight: Tk.Theme.size.thumbnailHeight * 3
        + Tk.Theme.size.controlLg * 2 + Tk.Theme.space.lg * 3
    Accessible.role: Accessible.Pane
    Accessible.name: root.tooltip
    Accessible.description: root.errorText.length ? root.errorText
        : !snapshotValid ? qsTr("Angle snapshot has invalid IDs, sync offsets, or playhead time.")
        : qsTr("%1 angles. Active angle %2 at %3 ticks.").arg(angleCount).arg(activeAngleId).arg(playheadTicks)

    function isTicks(value) { return typeof value === "string" && /^-?[0-9]+$/.test(value) }
    function validateAngles() {
        if (!root.angles || root.angles.length === 0 || !isTicks(String(root.playheadTicks))) return false
        const ids = Object.create(null)
        for (let i = 0; i < root.angles.length; ++i) {
            const item = root.angles[i], id = String(item.angleId || "")
            if (!id.length || ids[id] || !isTicks(String(item.syncOffsetTicks))) return false
            ids[id] = true
        }
        return ids[root.activeAngleId] === true
    }
    function angleById(id) {
        for (let i = 0; i < angleCount; ++i)
            if (String(angles[i].angleId) === String(id)) return angles[i]
        return null
    }
    function requestId() {
        root._serial = String(Number(root._serial) + 1)
        return (viewId.length ? viewId : "multicam") + ":switch:" + root._serial
    }
    function switchTo(id) {
        const angle = angleById(id)
        if (!enabled || !snapshotValid || !angle || angle.available === false
            || String(id) === activeAngleId || pendingRequestId.length > 0) return false
        const request = requestId()
        angleSwitchRequested(request, String(angle.angleId), String(playheadTicks))
        return true
    }
    function requestSeek(id) {
        const angle = angleById(id)
        if (!enabled || !snapshotValid || !angle || angle.available === false
            || !isTicks(String(angle.frameTimeTicks || playheadTicks))) return false
        seekAngleRequested(String(angle.angleId), String(angle.frameId || ""), String(angle.frameTimeTicks || playheadTicks))
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
            Tk.Caption { text: qsTr("PROGRAM · %1").arg(root.activeAngleId); color: Tk.Theme.color.accent }
        }
        Tk.Caption {
            objectName: "multicamSyncReadout"
            text: qsTr("Playhead %1 ticks · switch requests are cut at the acknowledged time").arg(root.playheadTicks)
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
                model: root.angles || []
                delegate: T.AbstractButton {
                    id: card
                    required property var modelData
                    readonly property string angleId: String(modelData.angleId)
                    readonly property bool isProgram: angleId === root.activeAngleId
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
                                timeTicks: String(modelData.frameTimeTicks || root.playheadTicks),
                                rateNumerator: root.rateNumerator, rateDenominator: root.rateDenominator,
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
                text: root.errorText.length ? root.errorText : root.statusText
                color: root.errorText.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
                wrapMode: Text.WordWrap
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "multicamSwitchActive"
                text: qsTr("Program: %1").arg(root.activeAngleId || qsTr("none"))
                enabled: false
                tooltip: qsTr("The program angle changes only after host acknowledgment")
            }
        }
    }
}
