// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/distribution-plot.md): all statistical values and
// interval meanings come from DistributionModel. This view only projects them.
Item {
    id: plot
    objectName: "distributionPlot"
    property Tk.DistributionModel distributionModel: null
    property string mode: "box" // box, violin, interval
    property string tooltip: qsTr("Distribution plot")
    property string selectedGroupId: ""
    property bool showDataTable: false
    property bool showSourceTable: false
    readonly property int groupCount: distributionModel ? distributionModel.count : 0
    readonly property var sourceRows: {
        if (!showSourceTable || !distributionModel || !selectedGroupId) return []
        const generation = distributionModel.generation
        return distributionModel.sourceValues(selectedGroupId)
    }
    readonly property string legendText: {
        const unit = distributionModel ? distributionModel.unit : ""
        const meaning = distributionModel ? distributionModel.intervalMeaning : ""
        const marks = mode === "violin"
            ? qsTr("Violin: Gaussian KDE; line: median")
            : mode === "interval" ? qsTr("Band: interval; line: estimate")
            : qsTr("Box: Q1–Q3; line: median; whiskers: min–max")
        const intervalNote = mode === "interval" ? qsTr(" · Interval: %1")
                                                  : qsTr(" · Interval available: %1")
        return marks + (meaning ? intervalNote.arg(meaning) : "")
            + (unit ? qsTr(" · Unit: %1").arg(unit) : "")
    }
    signal groupSelected(string groupId)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: legendText + qsTr(". Use Left and Right to select a group. Summary and source tables are available.")
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    function csvData() { return distributionModel ? distributionModel.summaryCsv() : "" }
    function selectIndex(index, userAction) {
        if (!distributionModel || index < 0 || index >= groupCount) return false
        const id = distributionModel.get(index).groupId
        selectedGroupId = id
        if (userAction) groupSelected(id)
        canvas.requestPaint()
        return true
    }
    function navigate(step) {
        if (!distributionModel || groupCount === 0) return false
        const current = distributionModel.indexOfId(selectedGroupId)
        const next = current < 0 ? (step < 0 ? groupCount - 1 : 0)
                                 : (current + step + groupCount) % groupCount
        return selectIndex(next, true)
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
            event.accepted = navigate(1)
        else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
            event.accepted = navigate(-1)
    }
    onModeChanged: {
        if (mode !== "box" && mode !== "violin" && mode !== "interval")
            mode = "box"
        canvas.requestPaint()
    }
    onDistributionModelChanged: {
        if (!distributionModel) selectedGroupId = ""
        else if (distributionModel.indexOfId(selectedGroupId) < 0)
            selectedGroupId = distributionModel.count ? distributionModel.get(0).groupId : ""
        if (canvas) canvas.requestPaint()
    }
    onSelectedGroupIdChanged: canvas.requestPaint()
    onShowDataTableChanged: canvas.requestPaint()
    onShowSourceTableChanged: canvas.requestPaint()
    Connections {
        target: plot.distributionModel
        function onStateChanged() {
            if (!plot.distributionModel) return
            if (plot.distributionModel.indexOfId(plot.selectedGroupId) < 0)
                plot.selectedGroupId = plot.groupCount ? plot.distributionModel.get(0).groupId : ""
            canvas.requestPaint()
        }
    }
    Connections {
        target: Tk.Theme
        function onChanged() { canvas.requestPaint() }
    }
    Component.onCompleted: {
        if (distributionModel && groupCount && !selectedGroupId)
            selectedGroupId = distributionModel.get(0).groupId
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Rectangle {
            objectName: "distributionToolbar"
            color: Tk.Theme.color.panel
            implicitHeight: Tk.Theme.size.toolbar
            Tk.Flex.shrink: 0
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                padding: Tk.Theme.space.xs
                Tk.Button { text: qsTr("Box"); small: true
                    variant: plot.mode === "box" ? "accent" : "default"
                    tooltip: qsTr("Show quartiles and whiskers")
                    onClicked: plot.mode = "box" }
                Tk.Button { text: qsTr("Violin"); small: true
                    variant: plot.mode === "violin" ? "accent" : "default"
                    tooltip: qsTr("Show Gaussian density")
                    onClicked: plot.mode = "violin" }
                Tk.Button { text: qsTr("Interval"); small: true
                    variant: plot.mode === "interval" ? "accent" : "default"
                    tooltip: qsTr("Show declared intervals")
                    available: !!plot.distributionModel && !!plot.distributionModel.intervalMeaning
                    onClicked: plot.mode = "interval" }
                Tk.Label { text: qsTr("%1 groups").arg(plot.groupCount); muted: true; Tk.Flex.grow: 1 }
                Tk.Button { text: qsTr("Summary"); small: true
                    variant: plot.showDataTable ? "accent" : "default"
                    tooltip: qsTr("Accessible summary table")
                    onClicked: { plot.showDataTable = !plot.showDataTable; plot.showSourceTable = false } }
                Tk.Button { text: qsTr("Values"); small: true
                    variant: plot.showSourceTable ? "accent" : "default"
                    tooltip: qsTr("Inspect selected source values")
                    available: !!plot.selectedGroupId
                    onClicked: { plot.showSourceTable = !plot.showSourceTable; plot.showDataTable = false } }
            }
        }
        Item {
            objectName: "distributionBody"
            Tk.Flex.grow: 1
            Canvas {
                id: canvas
                objectName: "distributionCanvas"
                anchors.fill: parent
                visible: !plot.showDataTable && !plot.showSourceTable
                Accessible.role: Accessible.Pane
                Accessible.name: plot.tooltip + qsTr(" visual chart")
                Accessible.description: plot.legendText
                function chartLeft() { return Tk.Theme.size.labelWidth / 2 }
                function chartRight() { return width - Tk.Theme.space.md }
                function chartTop() { return Tk.Theme.space.md }
                function chartBottom() { return height - Tk.Theme.size.row - Tk.Theme.space.sm }
                function yFor(value) {
                    const model = plot.distributionModel
                    const span = model ? model.maximum - model.minimum : 1
                    return chartBottom() - (value - model.minimum) / span
                        * (chartBottom() - chartTop())
                }
                function selectAt(x) {
                    if (!plot.groupCount) return
                    const slot = (chartRight() - chartLeft()) / plot.groupCount
                    const index = Math.floor((x - chartLeft()) / slot)
                    plot.selectIndex(index, true)
                    plot.forceActiveFocus()
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!plot.distributionModel || !plot.groupCount) return
                    const model = plot.distributionModel
                    const top = chartTop(), bottom = chartBottom()
                    const left = chartLeft(), right = chartRight()
                    if (right <= left || bottom <= top || model.maximum <= model.minimum) return
                    const slot = (right - left) / plot.groupCount
                    ctx.lineWidth = Tk.Theme.size.border
                    ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
                    ctx.textBaseline = "middle"
                    for (let tick = 0; tick <= 4; ++tick) {
                        const fraction = tick / 4
                        const y = bottom - fraction * (bottom - top)
                        ctx.strokeStyle = Tk.Theme.color.border
                        ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
                        ctx.fillStyle = Tk.Theme.color.textMuted
                        const value = model.minimum + fraction * (model.maximum - model.minimum)
                        ctx.fillText(Number(value).toPrecision(3), Tk.Theme.space.xs, y)
                    }
                    for (let index = 0; index < plot.groupCount; ++index) {
                        const group = model.get(index)
                        const center = left + slot * (index + 0.5)
                        const half = Math.min(slot * 0.3, Tk.Theme.size.fieldWidth / 2)
                        const selected = group.groupId === plot.selectedGroupId
                        if (slot >= Tk.Theme.size.fieldWidth / 2) {
                            ctx.fillStyle = Tk.Theme.color.text
                            ctx.textAlign = "center"
                            ctx.fillText(group.label, center, bottom + Tk.Theme.size.row / 2)
                            ctx.textAlign = "left"
                        }
                        if (selected) {
                            ctx.strokeStyle = Tk.Theme.color.focus
                            ctx.strokeRect(center - slot / 2 + Tk.Theme.size.border,
                                           top, slot - Tk.Theme.size.border * 2, bottom - top)
                        }
                        if (plot.mode === "violin") {
                            const density = group.density
                            let peak = 0
                            for (let j = 0; j < density.length; ++j)
                                peak = Math.max(peak, density[j].density)
                            ctx.fillStyle = Tk.Theme.color.accentSubtle
                            ctx.strokeStyle = Tk.Theme.color.accent
                            ctx.beginPath()
                            for (let j = 0; j < density.length; ++j) {
                                const widthAt = peak ? half * density[j].density / peak : 0
                                const x = center - widthAt, y = yFor(density[j].value)
                                if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                            }
                            for (let j = density.length - 1; j >= 0; --j) {
                                const widthAt = peak ? half * density[j].density / peak : 0
                                ctx.lineTo(center + widthAt, yFor(density[j].value))
                            }
                            ctx.closePath(); ctx.fill(); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(center - half, yFor(group.median))
                            ctx.lineTo(center + half, yFor(group.median)); ctx.stroke()
                        } else if (plot.mode === "interval") {
                            if (!group.hasInterval) continue
                            ctx.fillStyle = Tk.Theme.color.accentSubtle
                            ctx.fillRect(center - half, yFor(group.intervalHigh),
                                         half * 2, yFor(group.intervalLow) - yFor(group.intervalHigh))
                            ctx.strokeStyle = Tk.Theme.color.accent
                            ctx.strokeRect(center - half, yFor(group.intervalHigh),
                                           half * 2, yFor(group.intervalLow) - yFor(group.intervalHigh))
                            ctx.beginPath(); ctx.moveTo(center - half, yFor(group.estimate))
                            ctx.lineTo(center + half, yFor(group.estimate)); ctx.stroke()
                        } else {
                            ctx.strokeStyle = Tk.Theme.color.accent
                            ctx.fillStyle = Tk.Theme.color.accentSubtle
                            ctx.beginPath(); ctx.moveTo(center, yFor(group.minimum))
                            ctx.lineTo(center, yFor(group.maximum)); ctx.stroke()
                            ctx.strokeRect(center - half / 2, yFor(group.q3),
                                           half, yFor(group.q1) - yFor(group.q3))
                            ctx.fillRect(center - half / 2, yFor(group.q3),
                                         half, yFor(group.q1) - yFor(group.q3))
                            for (const value of [group.minimum, group.median, group.maximum]) {
                                ctx.beginPath(); ctx.moveTo(center - half / 2, yFor(value))
                                ctx.lineTo(center + half / 2, yFor(value)); ctx.stroke()
                            }
                        }
                    }
                }
                TapHandler { onTapped: function(event) { canvas.selectAt(event.position.x) } }
            }
            Tk.DataTable {
                objectName: "distributionSummaryTable"
                anchors.fill: parent
                visible: plot.showDataTable
                model: plot.distributionModel
                tooltip: plot.tooltip + qsTr(" summary data")
                columns: [
                    Tk.TableColumn { key: "label"; title: qsTr("Group"); width: Tk.Theme.size.fieldWidth },
                    Tk.TableColumn { key: "sampleCount"; title: qsTr("N"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "minimum"; title: qsTr("Min"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "q1"; title: qsTr("Q1"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "median"; title: qsTr("Median"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "q3"; title: qsTr("Q3"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "maximum"; title: qsTr("Max"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "intervalLow"; title: qsTr("Interval low"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "intervalHigh"; title: qsTr("Interval high"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "groupId"; title: qsTr("ID"); flex: 1; minWidth: Tk.Theme.size.fieldWidth; mono: true }
                ]
                onActivated: function(index) { plot.selectIndex(index, true) }
            }
            Tk.DataTable {
                objectName: "distributionSourceTable"
                anchors.fill: parent
                visible: plot.showSourceTable
                model: plot.sourceRows
                tooltip: plot.tooltip + qsTr(" source values")
                columns: [
                    Tk.TableColumn { key: "index"; title: qsTr("Source index"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "value"; title: qsTr("Value"); flex: 1; minWidth: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "unit"; title: qsTr("Unit"); width: Tk.Theme.size.fieldWidth }
                ]
            }
        }
        Tk.Label {
            objectName: "distributionLegend"
            text: plot.legendText
            muted: true
            wrapMode: Text.WordWrap
            width: parent.width
            Tk.Flex.shrink: 0
        }
        Tk.Label {
            objectName: "distributionError"
            text: plot.distributionModel ? plot.distributionModel.error : qsTr("No distribution model")
            color: Tk.Theme.color.danger
            visible: text.length > 0
            Tk.Flex.shrink: 0
        }
    }
}
