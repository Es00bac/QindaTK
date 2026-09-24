// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "ScaleLabels.js" as ScaleLabels

// AGENT-CONTRACT (docs/controls.md, docs/audio-measurement.md): the
// measurement provider owns channel layout, dBFS calculation, hold/decay and
// the clip latch; this view only projects the latest bounded snapshot. Bind
// `meter` to a Tk.AudioMeterModel (for example AudioEngine.outputMeter) or
// pass provider `levels` rows directly. objectNames: "meterRow",
// "channelTrack", "levelValue", "meterScale", "meterScaleLabel",
// "meterScaleUnit", "meterClipReset".
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
    // AGENT-GUARD: every row gives its name and value columns these exact
    // widths (Tk.Flex.basis; a child's `width` is ignored by Flex), so the
    // bars span [trackX, trackX + trackWidth] and the scale row lines up.
    readonly property real trackX: Tk.Theme.size.control + Tk.Theme.space.sm
    readonly property real trackWidth: Math.max(0, width - Tk.Theme.size.control - Tk.Theme.size.controlLg - 2 * Tk.Theme.space.sm)
    // Labels kept after thinning: {"<mark value>": true}.
    readonly property var _labelled: labelledMarks(scaleMarks, minimumDbfs, maximumDbfs, trackX, trackWidth,
                                                   scaleProbe.implicitWidth)
    readonly property string summary: channelCount === 0 ? qsTr("No measurement snapshot")
        : levels.map(function(c) { return channelName(c) + ": peak " + fmt(c.peakDbfs) + " " + unit
            + (showRms ? ", RMS " + fmt(c.rmsDbfs) + " " + unit : "")
            + (c.heldPeakDbfs !== undefined ? ", hold " + fmt(c.heldPeakDbfs) + " " + unit : "")
            + (c.maxPeakDbfs !== undefined ? ", max " + fmt(c.maxPeakDbfs) + " " + unit : "")
            + (c.clipped ? ", clipped" : "") }).join("; ")
    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: Math.max(Tk.Theme.size.controlLg, column.implicitHeight)
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
    function markText(db) { return Number(db).toFixed(0) }
    // Left edge of a scale label of `labelWidth`: centred on its tick, except
    // the end labels, which align inward so they stay over the track and clear
    // the unit label.
    function labelX(db, labelWidth) {
        const tick = trackX + trackWidth * position(db)
        if (db <= minimumDbfs) return tick
        if (db >= maximumDbfs) return tick - labelWidth
        return tick - labelWidth / 2
    }
    // The range ends first, then the other marks keep a label only where it
    // clears the labels already placed (ticks stay on the bars).
    function labelledMarks(marks, low, high, _trackX, _trackWidth, advance) {
        const inRange = (marks || []).filter(function(v) { return v >= low && v <= high })
        const widthOf = function(v) { return root.markText(v).length * advance }
        return ScaleLabels.thin(inRange, function(v) { return root.labelX(v, widthOf(v)) + widthOf(v) / 2 },
                                widthOf, [high, low], Tk.Theme.space.sm, [])
    }
    // The readout holds the peak (falls after the provider's hold time).
    function readout(c) { return c.heldPeakDbfs !== undefined ? c.heldPeakDbfs : c.peakDbfs }
    function resetClip() {
        if (!clipResetEnabled || !enabled) return
        if (meter) meter.resetClip()
        clipResetRequested()
    }

    // Measures one mono caption character for label thinning; never shown.
    Tk.Mono {
        id: scaleProbe
        visible: false
        text: "0"
        font.pixelSize: Tk.Theme.font.caption
    }
    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Repeater {
            model: root.levels ? root.levels : []
            delegate: Tk.Flex {
                id: row
                required property var modelData
                objectName: "meterRow"
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Label {
                    Tk.Flex.basis: Tk.Theme.size.control
                    Tk.Flex.shrink: 0
                    text: root.channelName(row.modelData)
                    color: Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                Item {
                    id: channelTrack
                    objectName: "channelTrack"
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    implicitHeight: Tk.Theme.size.controlSm
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
                    Tk.Flex.basis: Tk.Theme.size.controlLg
                    Tk.Flex.shrink: 0
                    text: root.fmt(root.readout(row.modelData))
                    color: row.modelData.clipped ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
        // AGENT-GUARD: Flex sizes this row from its implicit size (a `height`
        // would be ignored and the labels would spill onto the next row).
        Item {
            id: scaleRow
            objectName: "meterScale"
            visible: root.showScale && root.channelCount > 0
            implicitWidth: root.width
            implicitHeight: Math.max(Tk.Theme.size.controlSm, scaleProbe.implicitHeight)
            Repeater {
                model: root.scaleMarks
                delegate: Tk.Mono {
                    id: scaleLabel
                    required property real modelData
                    objectName: "meterScaleLabel"
                    // AGENT-GUARD: never name the row `scale`; Item.scale shadows it here.
                    visible: root._labelled[String(scaleLabel.modelData)] === true
                    x: root.labelX(scaleLabel.modelData, scaleLabel.implicitWidth)
                    y: (scaleRow.height - scaleLabel.implicitHeight) / 2
                    text: root.markText(scaleLabel.modelData)
                    color: Tk.Theme.color.textMuted
                    font.pixelSize: Tk.Theme.font.caption
                    Accessible.ignored: true
                }
            }
            Tk.Mono {
                objectName: "meterScaleUnit"
                x: Math.max(root.trackX + root.trackWidth + Tk.Theme.space.sm, scaleRow.width - implicitWidth)
                y: (scaleRow.height - implicitHeight) / 2
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
