// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: channels, ordering, selection and meter snapshots are
// host-owned. The horizontal ListView recycles strips; every edit/request keeps
// the channel's stable ID so delegate reuse cannot redirect an operation.
T.Control {
    id: mixer
    objectName: "mixerView"
    property var channels: [] // [{id,name,kind,gainParameterId,gainValue,gainSilent,muted,soloed,routeSummary}]
    property var meterModel: null
    property string selectedChannelId: ""
    property string tooltip: qsTr("Mixer channels")
    property alias channelList: strips
    readonly property int channelCount: strips.count

    signal channelSelected(string channelId)
    signal gainEditRequested(string channelId, string parameterId, string gestureId,
                             string phase, real value, bool silent)
    signal panEditRequested(string channelId, string parameterId, string gestureId,
                            string phase, real value)
    signal channelCommandRequested(string channelId, string requestId, string command, bool enabled)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 3
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.List
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 channels; selected %2")
            .arg(channels ? channels.length : 0).arg(selectedChannelId || qsTr("none"))

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: mixer.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Item {
        ListView {
            id: strips
            objectName: "mixerChannelList"
            anchors.fill: parent
            orientation: ListView.Horizontal
            clip: true
            model: mixer.channels
            spacing: Tk.Theme.space.sm
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            keyNavigationWraps: false
            Accessible.role: Accessible.List
            Accessible.name: mixer.Accessible.name
            delegate: Tk.ChannelStrip {
                id: strip
                required property int index
                required property var modelData
                objectName: "mixerChannelStrip"
                width: Tk.Theme.size.fieldWidth
                height: strips.height
                property var rowData: modelData || ({})
                channelId: String(rowData.id || "")
                channelName: String(rowData.name || channelId)
                channelKind: String(rowData.kind || "audio")
                gainParameterId: String(rowData.gainParameterId || (channelId + "/gain"))
                gainValue: Number(rowData.gainValue || 0)
                gainSilent: !!rowData.gainSilent
                muted: !!rowData.muted
                soloed: !!rowData.soloed
                panValue: Number(rowData.panValue === undefined ? 0 : rowData.panValue)
                stereoWidth: Number(rowData.stereoWidth === undefined ? 1 : rowData.stereoWidth)
                speakerLayout: String(rowData.speakerLayout || "Stereo")
                commandState: String(rowData.commandState || "idle")
                commandMessage: String(rowData.commandMessage || "")
                selected: mixer.selectedChannelId === channelId
                meterModel: mixer.meterModel
                meterChannel: Number(rowData.meterChannel === undefined ? index : rowData.meterChannel)
                peakDbfs: Number(rowData.peakDbfs === undefined ? -120 : rowData.peakDbfs)
                rmsDbfs: Number(rowData.rmsDbfs === undefined ? -120 : rowData.rmsDbfs)
                routeSummary: String(rowData.routeSummary || qsTr("No route details"))
                tooltip: qsTr("Channel %1").arg(channelName)
                onChannelSelected: mixer.channelSelected(channelId)
                onGainEditRequested: mixer.gainEditRequested(channelId, parameterId, gestureId, phase, value, silent)
                onPanEditRequested: mixer.panEditRequested(channelId, parameterId, gestureId, phase, value)
                onChannelCommandRequested: mixer.channelCommandRequested(channelId, requestId, command, enabled)
                Accessible.role: Accessible.ListItem
                Accessible.name: channelName
                Accessible.selected: selected
                Accessible.focusable: true
            }
        }
        Tk.EmptyState {
            objectName: "mixerEmptyState"
            anchors.fill: parent
            visible: !mixer.channels || mixer.channels.length === 0
            title: qsTr("NO CHANNELS")
            text: qsTr("The host has not supplied any mixer channels.")
        }
    }
    Tk.ToolTip { visible: mixer.hovered && mixer.tooltip.length > 0; text: mixer.tooltip }
}
