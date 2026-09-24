// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Measurement provider owns channel layout, dBFS calculation, hold/decay and
// clip latch. This view only projects the latest bounded GUI snapshot.
Item {
    id: root
    property var levels: [] // [{channel, peakDbfs, rmsDbfs, displayPeakDbfs, heldPeakDbfs, clipped, silent}]
    property string metric: "peak-rms"
    property string unit: "dBFS"
    property string integrationWindow: "provider-defined"
    property string tooltip: ""
    property real minimumDbfs: -60
    property real maximumDbfs: 0
    property var scaleMarks: [-60, -48, -36, -24, -18, -12, -6, 0]
    property bool showRms: true
    property bool showScale: true
    property bool clipResetEnabled: true
    signal clipResetRequested()

    readonly property int channelCount: levels ? levels.length : 0
    readonly property bool hasClip: levels ? levels.some(function(c) { return c.clipped }) : false
    readonly property string summary: channelCount === 0 ? qsTr("No measurement snapshot")
        : levels.map(function(c) { return c.channel + ": peak " + fmt(c.peakDbfs) + " " + unit
            + (showRms ? ", RMS " + fmt(c.rmsDbfs) + " " + unit : "")
            + (c.clipped ? ", clipped" : "") }).join("; ")
    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: Math.max(Tk.Theme.size.controlLg,
        channelCount * Tk.Theme.size.controlSm
        + (clipResetEnabled && hasClip ? Tk.Theme.size.control + Tk.Theme.space.xs : 0))
    Accessible.role: Accessible.Indicator
    Accessible.name: tooltip.length ? tooltip : qsTr("Audio level meter")
    Accessible.description: summary + qsTr(". Metric: %1; window: %2").arg(metric).arg(integrationWindow)

    function fmt(v) { return !isFinite(Number(v)) ? "−∞" : Number(v).toFixed(1) }
    function position(db) {
        const n = Number(db)
        if (!isFinite(n)) return 0
        return Math.max(0, Math.min(1, (n - minimumDbfs) / (maximumDbfs - minimumDbfs)))
    }
    function resetClip() { if (clipResetEnabled && enabled) clipResetRequested() }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Repeater {
            model: root.levels ? root.levels : []
            delegate: Tk.Flex {
                required property var modelData
                objectName: "meterRow"
                width: root.width
                height: Tk.Theme.size.controlSm
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.Label {
                    width: Tk.Theme.size.control
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.channel
                    color: Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    Accessible.ignored: true
                }
                Item {
                    id: channelTrack
                    objectName: "channelTrack"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    width: Math.max(0, root.width - Tk.Theme.size.control
                        - Tk.Theme.size.controlLg - 2 * Tk.Theme.space.sm)
                    height: Tk.Theme.size.controlSm
                    Rectangle { anchors.fill: parent; radius: Tk.Theme.radius.sm; color: Tk.Theme.color.divider }
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: modelData.silent ? 0 : channelTrack.width * root.position(modelData.rmsDbfs)
                        radius: Tk.Theme.radius.sm
                        color: Tk.Theme.alpha(Tk.Theme.color.accent, 0.55)
                        visible: root.showRms
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: modelData.silent ? 0 : channelTrack.width * root.position(modelData.displayPeakDbfs)
                        radius: Tk.Theme.radius.sm
                        color: root.position(modelData.displayPeakDbfs) > 0.82 ? Tk.Theme.color.warning : Tk.Theme.color.success
                    }
                    Rectangle {
                        visible: modelData.clipped
                        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: Tk.Theme.space.sm
                        color: Tk.Theme.color.danger
                    }
                    Rectangle {
                        x: channelTrack.width * root.position(modelData.heldPeakDbfs)
                        width: Tk.Theme.size.border + 1; height: parent.height
                        color: modelData.clipped ? Tk.Theme.color.danger : Tk.Theme.color.text
                    }
                    Repeater {
                        model: root.showScale ? root.scaleMarks : []
                        delegate: Rectangle {
                            required property real modelData
                            x: channelTrack.width * root.position(modelData)
                            width: Tk.Theme.size.border
                            height: parent.height
                            color: Tk.Theme.color.bg
                            opacity: Tk.Theme.opacity.muted
                            z: 2
                        }
                    }
                }
                Tk.Mono {
                    objectName: "levelValue"
                    width: Tk.Theme.size.controlLg
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.fmt(modelData.peakDbfs)
                    color: modelData.clipped ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
        Tk.Button {
            visible: root.clipResetEnabled && root.hasClip
            text: qsTr("Reset clip")
            tooltip: qsTr("Ask the measurement provider to clear clip latches")
            onClicked: root.resetClip()
        }
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && hover.hovered }
}
