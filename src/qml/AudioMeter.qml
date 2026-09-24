// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/audio-measurement.md): the
// measurement provider owns channel layout, dBFS calculation, hold/decay and
// the clip latch; this view only projects the latest bounded snapshot. Bind
// `meter` to a Tk.AudioMeterModel (for example AudioEngine.outputMeter) or
// pass provider `levels` rows directly. objectNames: "meterRow",
// "channelTrack", "levelValue", "meterScale", "meterScaleLabel",
// "meterClipReset".
Item {
    id: root
    property var meter: null // Tk.AudioMeterModel
    property var levels: [] // [{channel, name, peakDbfs, rmsDbfs, displayPeakDbfs, heldPeakDbfs, maxPeakDbfs, clipped, silent}]
    property string metric: "peak-rms"
    property string unit: "dBFS"
    property string integrationWindow: "provider-defined"
    property string tooltip: ""
    property real minimumDbfs: -60
    property real maximumDbfs: 0
    property real warningDbfs: -6
    property var scaleMarks: [-60, -48, -36, -24, -18, -12, -6, 0]
    property bool showRms: true
    property bool showScale: true
    property bool clipResetEnabled: true
    signal clipResetRequested()

    Binding { target: root; property: "levels"; when: root.meter !== null; value: root.meter ? root.meter.levels : [] }
    Binding { target: root; property: "metric"; when: root.meter !== null; value: root.meter ? root.meter.metric : "" }
    Binding { target: root; property: "integrationWindow"; when: root.meter !== null; value: root.meter ? root.meter.integrationWindow : "" }

    readonly property int channelCount: levels ? levels.length : 0
    readonly property bool hasClip: levels ? levels.some(function(c) { return c.clipped }) : false
    readonly property real trackX: Tk.Theme.size.control + Tk.Theme.space.sm
    readonly property real trackWidth: Math.max(0, width - Tk.Theme.size.control - Tk.Theme.size.controlLg - 2 * Tk.Theme.space.sm)
    readonly property string summary: channelCount === 0 ? qsTr("No measurement snapshot")
        : levels.map(function(c) { return channelName(c) + ": peak " + fmt(c.peakDbfs) + " " + unit
            + (showRms ? ", RMS " + fmt(c.rmsDbfs) + " " + unit : "")
            + (c.heldPeakDbfs !== undefined ? ", hold " + fmt(c.heldPeakDbfs) + " " + unit : "")
            + (c.maxPeakDbfs !== undefined ? ", max " + fmt(c.maxPeakDbfs) + " " + unit : "")
            + (c.clipped ? ", clipped" : "") }).join("; ")
    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: Math.max(Tk.Theme.size.controlLg,
        channelCount * (Tk.Theme.size.controlSm + Tk.Theme.space.xs)
        + (showScale ? Tk.Theme.size.controlSm : 0)
        + (clipResetEnabled && hasClip ? Tk.Theme.size.control + Tk.Theme.space.xs : 0))
    Accessible.role: Accessible.Indicator
    Accessible.name: tooltip.length ? tooltip : qsTr("Audio level meter")
    Accessible.description: summary + qsTr(". Metric: %1; window: %2").arg(metric).arg(integrationWindow)

    function channelName(c) { return c.name !== undefined ? c.name : String(c.channel) }
    function fmt(v) { return !isFinite(Number(v)) || Number(v) <= -120 ? "−∞" : Number(v).toFixed(1) }
    function position(db) {
        const n = Number(db)
        if (!isFinite(n)) return 0
        return Math.max(0, Math.min(1, (n - minimumDbfs) / (maximumDbfs - minimumDbfs)))
    }
    // The readout holds the peak (falls after the provider's hold time).
    function readout(c) { return c.heldPeakDbfs !== undefined ? c.heldPeakDbfs : c.peakDbfs }
    function resetClip() {
        if (!clipResetEnabled || !enabled) return
        if (meter) meter.resetClip()
        clipResetRequested()
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Repeater {
            model: root.levels ? root.levels : []
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "meterRow"
                width: root.width
                height: Tk.Theme.size.controlSm
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.Label {
                    width: Tk.Theme.size.control
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.channelName(row.modelData)
                    color: Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    Accessible.ignored: true
                }
                Item {
                    id: channelTrack
                    objectName: "channelTrack"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    width: root.trackWidth
                    height: Tk.Theme.size.controlSm
                    Rectangle { anchors.fill: parent; radius: Tk.Theme.radius.sm; color: Tk.Theme.color.divider }
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: row.modelData.silent ? 0 : channelTrack.width * root.position(row.modelData.rmsDbfs)
                        radius: Tk.Theme.radius.sm
                        color: Tk.Theme.alpha(Tk.Theme.color.accent, 0.55)
                        visible: root.showRms
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: row.modelData.silent ? 0 : channelTrack.width * root.position(row.modelData.displayPeakDbfs)
                        radius: Tk.Theme.radius.sm
                        color: Number(row.modelData.displayPeakDbfs) >= root.warningDbfs ? Tk.Theme.color.warning : Tk.Theme.color.success
                    }
                    Rectangle {
                        visible: row.modelData.clipped
                        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: Tk.Theme.space.sm
                        color: Tk.Theme.color.danger
                    }
                    Rectangle {
                        x: channelTrack.width * root.position(row.modelData.heldPeakDbfs)
                        width: Tk.Theme.size.border + 1; height: parent.height
                        color: row.modelData.clipped ? Tk.Theme.color.danger : Tk.Theme.color.text
                    }
                    Repeater {
                        model: root.showScale ? root.scaleMarks : []
                        delegate: Rectangle {
                            required property real modelData
                            x: channelTrack.width * root.position(modelData)
                            width: Tk.Theme.size.border
                            height: channelTrack.height
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
                    text: root.fmt(root.readout(row.modelData))
                    color: row.modelData.clipped ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
        Item {
            id: scaleRow
            objectName: "meterScale"
            visible: root.showScale && root.channelCount > 0
            width: root.width
            height: Tk.Theme.size.controlSm
            Repeater {
                model: root.scaleMarks
                delegate: Tk.Mono {
                    id: scaleLabel
                    required property real modelData
                    objectName: "meterScaleLabel"
                    // AGENT-GUARD: never name the row `scale`; Item.scale shadows it here.
                    x: Math.max(0, Math.min(scaleRow.width - scaleLabel.implicitWidth,
                                            root.trackX + root.trackWidth * root.position(scaleLabel.modelData)
                                            - scaleLabel.implicitWidth / 2))
                    anchors.verticalCenter: parent.verticalCenter
                    text: Number(scaleLabel.modelData).toFixed(0)
                    color: Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    Accessible.ignored: true
                }
            }
            Tk.Mono {
                x: Math.max(0, root.trackX + root.trackWidth + Tk.Theme.space.sm)
                anchors.verticalCenter: parent.verticalCenter
                text: root.unit
                color: Tk.Theme.color.textMuted
                font.pixelSize: Tk.Theme.font.caption
                Accessible.ignored: true
            }
        }
        Tk.Button {
            objectName: "meterClipReset"
            visible: root.clipResetEnabled && root.hasClip
            text: qsTr("Reset clip")
            tooltip: qsTr("Clear the clip latches and peak maxima")
            onClicked: root.resetClip()
        }
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && hover.hovered }
}
