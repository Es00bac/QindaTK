// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// This component displays provider-supplied, time-windowed measurement
// snapshots. It never computes or labels generic values as standards-based LUFS.
Item {
    id: root
    property var samples: [] // [{time, value, metricId, unit, channelLayout, window}]
    property string metricId: ""
    property string unit: ""
    property string integrationWindow: ""
    property string channelLayout: ""
    property string targetProfile: ""
    property real minimum: -60
    property real maximum: 0
    property real targetMinimum: NaN
    property real targetMaximum: NaN
    property string tooltip: ""
    signal resetHistoryRequested()
    signal exportRequested(string metricId, string unit, string integrationWindow,
                           string channelLayout, string targetProfile, var samples)

    readonly property int sampleCount: samples ? samples.length : 0
    readonly property var latestSample: sampleCount > 0 ? samples[sampleCount - 1] : null
    readonly property string summary: sampleCount === 0 ? qsTr("No history data")
        : qsTr("%1 samples; latest %2 %3; metric %4; window %5; channels %6")
            .arg(sampleCount).arg(Number(latestSample.value).toFixed(1)).arg(unit)
            .arg(metricId).arg(integrationWindow).arg(channelLayout)
    implicitWidth: Tk.Theme.size.fieldWidth * 2
    implicitHeight: Tk.Theme.size.controlLg * 5
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip.length ? tooltip : qsTr("Loudness and measurement history")
    Accessible.description: summary + (targetProfile.length ? qsTr("; target profile %1").arg(targetProfile) : "")

    function yFor(value) {
        return plot.height - Math.max(0, Math.min(1, (Number(value) - minimum) / (maximum - minimum))) * plot.height
    }
    Canvas {
        id: plot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: controls.top
        anchors.bottomMargin: Tk.Theme.space.sm
        onPaint: {
            const ctx = getContext("2d"); ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = Tk.Theme.color.divider; ctx.lineWidth = Tk.Theme.size.border
            for (let mark = Math.ceil(root.minimum / 10) * 10; mark <= root.maximum; mark += 10) {
                const y = root.yFor(mark)
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
            if (isFinite(root.targetMinimum) && isFinite(root.targetMaximum)) {
                ctx.fillStyle = Tk.Theme.alpha(Tk.Theme.color.success, Tk.Theme.opacity.island)
                const y1 = root.yFor(root.targetMaximum), y2 = root.yFor(root.targetMinimum)
                ctx.fillRect(0, y1, width, y2 - y1)
            }
            if (root.sampleCount < 1) return
            ctx.strokeStyle = Tk.Theme.color.accent; ctx.lineWidth = Tk.Theme.size.border + 1
            ctx.beginPath()
            for (let i = 0; i < root.sampleCount; ++i) {
                const x = root.sampleCount === 1 ? width : i * width / (root.sampleCount - 1)
                const y = root.yFor(root.samples[i].value)
                if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
            }
            ctx.stroke()
        }
    }
    Tk.Flex {
        id: controls
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        Tk.Mono {
            text: root.metricId + (root.unit.length ? " · " + root.unit : "")
            color: Tk.Theme.color.textMuted; font.pixelSize: Tk.Theme.font.caption
            Accessible.ignored: true
        }
        Tk.Button {
            text: qsTr("Reset history")
            tooltip: qsTr("Ask the measurement provider to reset its history")
            onClicked: root.resetHistoryRequested()
        }
        Tk.Button {
            text: qsTr("Export report")
            tooltip: qsTr("Export the current provider measurements and provenance")
            enabled: root.sampleCount > 0
            onClicked: root.exportRequested(root.metricId, root.unit, root.integrationWindow,
                root.channelLayout, root.targetProfile, root.samples)
        }
    }
    onSamplesChanged: plot.requestPaint()
    onMinimumChanged: plot.requestPaint()
    onMaximumChanged: plot.requestPaint()
    onTargetMinimumChanged: plot.requestPaint()
    onTargetMaximumChanged: plot.requestPaint()
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && hover.hovered }
    HoverHandler { id: hover }
}
