// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/audio-measurement.md): this view
// displays provider-supplied, time-windowed measurement snapshots. It never
// computes a reading and never labels a generic value as standards-based
// LUFS: bind `loudness` to a Tk.AudioLoudnessModel (for example
// AudioEngine.outputLoudness) for ITU-R BS.1770-4 readings, or pass provider
// `samples` with their own metric, unit and window. objectNames: "historyPlot",
// "historyScaleLabel", "loudnessReadout", "historyReset", "historyExport".
Item {
    id: root
    property var loudness: null // Tk.AudioLoudnessModel
    property var samples: [] // [{time, value}] in `unit`, oldest first
    property string metricId: ""
    property string unit: ""
    property string integrationWindow: ""
    property string channelLayout: ""
    property string targetProfile: ""
    property real minimum: -60
    property real maximum: 0
    property real scaleStep: 10
    property real targetMinimum: NaN
    property real targetMaximum: NaN
    property string tooltip: ""
    signal resetHistoryRequested()
    signal exportRequested(string metricId, string unit, string integrationWindow,
                           string channelLayout, string targetProfile, var samples)
    // With `loudness`: the model's full report (schema qindatk.loudness-report/1).
    signal reportExported(var report)

    Binding { target: root; property: "samples"; when: root.loudness !== null; value: root.loudness ? root.loudness.history : [] }
    Binding { target: root; property: "metricId"; when: root.loudness !== null; value: root.loudness ? root.loudness.historyMetricId : "" }
    Binding { target: root; property: "unit"; when: root.loudness !== null; value: root.loudness ? root.loudness.unit : "" }
    Binding { target: root; property: "integrationWindow"; when: root.loudness !== null; value: root.loudness ? root.loudness.historyWindow : "" }
    Binding { target: root; property: "channelLayout"; when: root.loudness !== null; value: root.loudness ? root.loudness.channelLayout : "" }

    readonly property int sampleCount: samples ? samples.length : 0
    readonly property var latestSample: sampleCount > 0 ? samples[sampleCount - 1] : null
    readonly property var gridMarks: {
        const marks = []
        if (!(scaleStep > 0) || !(maximum > minimum)) return marks
        for (let mark = Math.ceil(minimum / scaleStep) * scaleStep; mark <= maximum; mark += scaleStep) marks.push(mark)
        return marks
    }
    readonly property string readout: loudness
        ? qsTr("M %1 · S %2 · I %3 %4").arg(fmt(loudness.momentaryLufs)).arg(fmt(loudness.shortTermLufs))
            .arg(fmt(loudness.integratedLufs)).arg(loudness.unit)
        : ""
    readonly property string summary: (sampleCount === 0 ? qsTr("No history data")
        : qsTr("%1 samples; latest %2 %3; metric %4; window %5; channels %6")
            .arg(sampleCount).arg(fmt(latestSample.value)).arg(unit)
            .arg(metricId).arg(integrationWindow).arg(channelLayout))
        + (loudness ? qsTr("; %1 (%2)").arg(loudness.summary).arg(loudness.algorithm) : "")
    implicitWidth: Tk.Theme.size.fieldWidth * 2
    implicitHeight: Tk.Theme.size.controlLg * 5
    Accessible.role: Accessible.Graphic
    Accessible.name: tooltip.length ? tooltip : qsTr("Loudness and measurement history")
    Accessible.description: summary + (targetProfile.length ? qsTr("; target profile %1").arg(targetProfile) : "")

    function fmt(v) {
        const n = Number(v)
        if (isNaN(n)) return "—"
        if (!isFinite(n)) return n < 0 ? "−∞" : "+∞"
        return n.toFixed(1)
    }
    function yFor(value) {
        const n = Number(value)
        const p = isNaN(n) ? 0 : Math.max(0, Math.min(1, (n - minimum) / (maximum - minimum)))
        return plot.height - p * plot.height
    }
    function resetHistory() {
        if (loudness) loudness.reset()
        resetHistoryRequested()
    }
    function exportReport() {
        exportRequested(metricId, unit, integrationWindow, channelLayout, targetProfile, samples)
        if (loudness) reportExported(loudness.report())
    }

    Item {
        id: axis
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: controls.top
        anchors.bottomMargin: Tk.Theme.space.sm
        width: Tk.Theme.size.control
        Repeater {
            model: root.gridMarks
            delegate: Tk.Mono {
                id: gridLabel
                required property real modelData
                objectName: "historyScaleLabel"
                x: axis.width - gridLabel.implicitWidth - Tk.Theme.space.xs
                y: Math.max(0, Math.min(axis.height - gridLabel.implicitHeight,
                                        root.yFor(gridLabel.modelData) - gridLabel.implicitHeight / 2))
                text: Number(gridLabel.modelData).toFixed(0)
                color: Tk.Theme.color.textMuted
                font.pixelSize: Tk.Theme.font.caption
                Accessible.ignored: true
            }
        }
    }
    Canvas {
        id: plot
        objectName: "historyPlot"
        anchors.left: axis.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: controls.top
        anchors.bottomMargin: Tk.Theme.space.sm
        onPaint: {
            const ctx = getContext("2d"); ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = Tk.Theme.color.divider; ctx.lineWidth = Tk.Theme.size.border
            for (let i = 0; i < root.gridMarks.length; ++i) {
                const y = root.yFor(root.gridMarks[i])
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
            if (isFinite(root.targetMinimum) && isFinite(root.targetMaximum)) {
                ctx.fillStyle = Tk.Theme.alpha(Tk.Theme.color.success, Tk.Theme.opacity.island)
                const y1 = root.yFor(root.targetMaximum), y2 = root.yFor(root.targetMinimum)
                ctx.fillRect(0, y1, width, y2 - y1)
            }
            if (root.sampleCount < 1) return
            // x follows each reading's time when the provider supplies times.
            const first = Number(root.samples[0].time), last = Number(root.samples[root.sampleCount - 1].time)
            const timed = isFinite(first) && isFinite(last) && last > first
            ctx.strokeStyle = Tk.Theme.color.accent; ctx.lineWidth = Tk.Theme.size.border + 1
            ctx.beginPath()
            for (let i = 0; i < root.sampleCount; ++i) {
                const x = timed ? (Number(root.samples[i].time) - first) / (last - first) * width
                        : root.sampleCount === 1 ? width : i * width / (root.sampleCount - 1)
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
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Mono {
            text: root.metricId + (root.unit.length ? " · " + root.unit : "")
            color: Tk.Theme.color.textMuted; font.pixelSize: Tk.Theme.font.caption
            Accessible.ignored: true
        }
        Tk.Mono {
            objectName: "loudnessReadout"
            visible: root.loudness !== null
            text: root.readout
            font.pixelSize: Tk.Theme.font.caption
            Accessible.name: root.readout
        }
        Tk.Button {
            objectName: "historyReset"
            text: qsTr("Reset history")
            tooltip: root.loudness ? qsTr("Reset the integrated measurement, maxima and history")
                                   : qsTr("Ask the measurement provider to reset its history")
            onClicked: root.resetHistory()
        }
        Tk.Button {
            objectName: "historyExport"
            text: qsTr("Export report")
            tooltip: qsTr("Export the current provider measurements and provenance")
            enabled: root.sampleCount > 0
            onClicked: root.exportReport()
        }
    }
    onSamplesChanged: plot.requestPaint()
    onMinimumChanged: plot.requestPaint()
    onMaximumChanged: plot.requestPaint()
    onScaleStepChanged: plot.requestPaint()
    onTargetMinimumChanged: plot.requestPaint()
    onTargetMaximumChanged: plot.requestPaint()
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && hover.hovered }
    HoverHandler { id: hover }
}
