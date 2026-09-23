// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/financial-displays.md): band labels, direction,
// target and range are validated by TargetModel; drawing infers no category.
Item {
    id: display
    objectName: "targetDisplay"
    property Tk.TargetModel targetModel: null
    property string mode: "bullet" // bullet or gauge
    property string tooltip: qsTr("Target display")
    property string selectedTargetId: ""
    property int pageStart: 0
    property bool showDataTable: false
    readonly property int pageCapacity: mode === "gauge" ? 4 : 8
    readonly property int visibleStart: targetModel
        ? Math.max(0, Math.min(pageStart, Math.max(0, targetModel.count - 1))) : 0
    readonly property int visibleCount: targetModel
        ? Math.min(pageCapacity, targetModel.count - visibleStart) : 0
    readonly property var visibleRecords: {
        if (!targetModel) return []
        const generation = targetModel.generation
        return targetModel.page(visibleStart, visibleCount)
    }
    readonly property var selectedRecord: targetModel
        ? targetModel.get(targetModel.indexOfId(selectedTargetId)) : ({})
    readonly property string legendText: selectedRecord.targetId
        ? qsTr("%1 · %2 %3 · target %4 %3 · %5 is better · %6 · bands: %7")
            .arg(selectedRecord.label)
            .arg(selectedRecord.missing ? qsTr("missing") : selectedRecord.value)
            .arg(selectedRecord.unit).arg(selectedRecord.target)
            .arg(selectedRecord.direction).arg(selectedRecord.thresholdMeaning)
            .arg(selectedRecord.bandsText)
        : qsTr("No target selected")
    signal targetSelected(string targetId)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: legendText + qsTr(". Use arrow keys to select a target. Data table available.")
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    function csvData() { return targetModel ? targetModel.csv() : "" }
    function selectIndex(index, userAction) {
        if (!targetModel || index < 0 || index >= targetModel.count) return false
        const id = targetModel.get(index).targetId
        selectedTargetId = id
        if (index < visibleStart || index >= visibleStart + visibleCount)
            pageStart = Math.floor(index / pageCapacity) * pageCapacity
        if (userAction) targetSelected(id)
        canvas.requestPaint()
        return true
    }
    function navigate(step) {
        if (!targetModel || !targetModel.count) return false
        const index = targetModel.indexOfId(selectedTargetId)
        return selectIndex(Math.max(0, Math.min(targetModel.count - 1,
                           index < 0 ? 0 : index + step)), true)
    }
    function shiftPage(step) {
        if (!targetModel) return
        pageStart = Math.max(0, Math.min(targetModel.count - 1,
                                        visibleStart + step * pageCapacity))
    }
    function reconcileSelection() {
        if (!targetModel) selectedTargetId = ""
        else if (targetModel.indexOfId(selectedTargetId) < 0)
            selectedTargetId = targetModel.count ? targetModel.get(0).targetId : ""
        canvas.requestPaint()
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
            event.accepted = navigate(1)
        else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
            event.accepted = navigate(-1)
        else if (event.key === Qt.Key_PageDown) {
            shiftPage(1); event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            shiftPage(-1); event.accepted = true
        }
    }
    onTargetModelChanged: reconcileSelection()
    onSelectedTargetIdChanged: canvas.requestPaint()
    onPageStartChanged: canvas.requestPaint()
    onModeChanged: {
        if (mode !== "bullet" && mode !== "gauge") mode = "bullet"
        canvas.requestPaint()
    }
    Connections {
        target: display.targetModel
        function onStateChanged() { display.reconcileSelection() }
    }
    Connections {
        target: Tk.Theme
        function onChanged() { canvas.requestPaint() }
    }
    Component.onCompleted: reconcileSelection()

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Rectangle {
            objectName: "targetToolbar"
            color: Tk.Theme.color.panel
            implicitHeight: Tk.Theme.size.toolbar
            Tk.Flex.shrink: 0
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                padding: Tk.Theme.space.xs
                Tk.Button { text: qsTr("Bullet"); small: true
                    variant: display.mode === "bullet" ? "accent" : "default"
                    tooltip: qsTr("Show linear target bands")
                    onClicked: display.mode = "bullet" }
                Tk.Button { text: qsTr("Gauge"); small: true
                    variant: display.mode === "gauge" ? "accent" : "default"
                    tooltip: qsTr("Show radial target bands")
                    onClicked: display.mode = "gauge" }
                Tk.Button { text: qsTr("Previous"); small: true
                    tooltip: qsTr("Previous target page")
                    onClicked: display.shiftPage(-1) }
                Tk.Button { text: qsTr("Next"); small: true
                    tooltip: qsTr("Next target page")
                    onClicked: display.shiftPage(1) }
                Tk.Label { text: qsTr("%1–%2 / %3")
                    .arg(display.visibleCount ? display.visibleStart + 1 : 0)
                    .arg(display.visibleStart + display.visibleCount)
                    .arg(display.targetModel ? display.targetModel.count : 0)
                    muted: true; Tk.Flex.grow: 1 }
                Tk.Button { text: qsTr("Data"); small: true
                    tooltip: qsTr("Show accessible target table")
                    variant: display.showDataTable ? "accent" : "default"
                    onClicked: display.showDataTable = !display.showDataTable }
            }
        }
        Item {
            objectName: "targetBody"
            Tk.Flex.grow: 1
            Canvas {
                id: canvas
                objectName: "targetCanvas"
                anchors.fill: parent
                visible: !display.showDataTable
                Accessible.role: Accessible.Pane
                Accessible.name: display.tooltip + qsTr(" visual display")
                Accessible.description: display.legendText
                function selectAt(x, y) {
                    if (!display.visibleCount) return
                    const slot = display.mode === "gauge"
                        ? width / display.visibleCount : height / display.visibleCount
                    const index = Math.floor((display.mode === "gauge" ? x : y) / slot)
                    if (index >= 0 && index < display.visibleCount) {
                        display.selectIndex(display.visibleStart + index, true)
                        display.forceActiveFocus()
                    }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!display.visibleCount) return
                    ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
                    ctx.textBaseline = "middle"
                    for (let index = 0; index < display.visibleRecords.length; ++index) {
                        const record = display.visibleRecords[index]
                        const span = record.maximum - record.minimum
                        const normalized = value => (value - record.minimum) / span
                        if (display.mode === "gauge") {
                            const slot = width / display.visibleCount
                            const centerX = slot * (index + 0.5)
                            const centerY = height * 0.65
                            const radius = Math.min(slot * 0.38, height * 0.5)
                            const thickness = Math.min(radius * 0.25, Tk.Theme.size.controlSm / 2)
                            if (radius <= thickness) continue
                            let start = record.minimum
                            for (let band = 0; band < record.bands.length; ++band) {
                                const segment = record.bands[band]
                                ctx.strokeStyle = band % 2 ? Tk.Theme.color.border
                                                           : Tk.Theme.color.panelAlt
                                ctx.lineWidth = thickness
                                ctx.beginPath()
                                ctx.arc(centerX, centerY, radius,
                                        Math.PI + normalized(start) * Math.PI,
                                        Math.PI + normalized(segment.end) * Math.PI)
                                ctx.stroke()
                                start = segment.end
                            }
                            const targetAngle = Math.PI + normalized(record.target) * Math.PI
                            ctx.strokeStyle = Tk.Theme.color.accent
                            ctx.lineWidth = Tk.Theme.size.border * 2
                            ctx.beginPath()
                            ctx.moveTo(centerX + Math.cos(targetAngle) * (radius - thickness),
                                       centerY + Math.sin(targetAngle) * (radius - thickness))
                            ctx.lineTo(centerX + Math.cos(targetAngle) * (radius + thickness),
                                       centerY + Math.sin(targetAngle) * (radius + thickness))
                            ctx.stroke()
                            if (!record.missing) {
                                const valueAngle = Math.PI + normalized(record.value) * Math.PI
                                ctx.strokeStyle = Tk.Theme.color.text
                                ctx.lineWidth = Tk.Theme.size.border * 2
                                ctx.beginPath(); ctx.moveTo(centerX, centerY)
                                ctx.lineTo(centerX + Math.cos(valueAngle) * radius,
                                           centerY + Math.sin(valueAngle) * radius)
                                ctx.stroke()
                            }
                            ctx.fillStyle = Tk.Theme.color.text
                            ctx.textAlign = "center"
                            ctx.fillText(record.label, centerX, centerY + Tk.Theme.size.row / 2)
                            ctx.fillText(record.missing ? qsTr("Missing")
                                         : record.value + " " + record.unit,
                                         centerX, centerY + Tk.Theme.size.row * 1.5)
                            if (record.targetId === display.selectedTargetId) {
                                ctx.strokeStyle = Tk.Theme.color.focus
                                ctx.lineWidth = Tk.Theme.size.border
                                ctx.strokeRect(index * slot + Tk.Theme.size.border,
                                               Tk.Theme.space.xs,
                                               slot - Tk.Theme.size.border * 2,
                                               height - Tk.Theme.space.xs * 2)
                            }
                        } else {
                            const slot = height / display.visibleCount
                            const y = slot * (index + 0.5)
                            const left = Tk.Theme.size.labelWidth
                            const right = width - Tk.Theme.space.md
                            const barHeight = Math.min(Tk.Theme.size.controlSm / 2, slot * 0.35)
                            if (right <= left) continue
                            let start = record.minimum
                            for (let band = 0; band < record.bands.length; ++band) {
                                const segment = record.bands[band]
                                ctx.fillStyle = band % 2 ? Tk.Theme.color.border
                                                         : Tk.Theme.color.panelAlt
                                ctx.fillRect(left + normalized(start) * (right - left),
                                             y - barHeight / 2,
                                             (normalized(segment.end) - normalized(start)) * (right - left),
                                             barHeight)
                                start = segment.end
                            }
                            if (!record.missing) {
                                ctx.fillStyle = Tk.Theme.color.textMuted
                                ctx.fillRect(left, y - barHeight / 4,
                                             normalized(record.value) * (right - left), barHeight / 2)
                            }
                            const targetX = left + normalized(record.target) * (right - left)
                            ctx.strokeStyle = Tk.Theme.color.accent
                            ctx.lineWidth = Tk.Theme.size.border * 2
                            ctx.beginPath(); ctx.moveTo(targetX, y - barHeight)
                            ctx.lineTo(targetX, y + barHeight); ctx.stroke()
                            ctx.fillStyle = Tk.Theme.color.text
                            ctx.textAlign = "left"
                            ctx.fillText(record.label, Tk.Theme.space.xs, y)
                            ctx.textAlign = "right"
                            ctx.fillText(record.missing ? qsTr("Missing")
                                         : record.value + " " + record.unit,
                                         right, y - barHeight)
                            if (record.targetId === display.selectedTargetId) {
                                ctx.strokeStyle = Tk.Theme.color.focus
                                ctx.lineWidth = Tk.Theme.size.border
                                ctx.strokeRect(Tk.Theme.size.border, index * slot + Tk.Theme.size.border,
                                               width - Tk.Theme.size.border * 2,
                                               slot - Tk.Theme.size.border * 2)
                            }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: function(event) { canvas.selectAt(event.x, event.y) }
                }
            }
            Tk.DataTable {
                objectName: "targetDataTable"
                anchors.fill: parent
                visible: display.showDataTable
                model: display.targetModel
                tooltip: display.tooltip + qsTr(" target data")
                columns: [
                    Tk.TableColumn { key: "label"; title: qsTr("Measure"); width: Tk.Theme.size.fieldWidth },
                    Tk.TableColumn { key: "value"; title: qsTr("Current"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "target"; title: qsTr("Target"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "unit"; title: qsTr("Unit"); width: Tk.Theme.size.fieldWidth },
                    Tk.TableColumn { key: "direction"; title: qsTr("Better"); width: Tk.Theme.size.fieldWidth },
                    Tk.TableColumn { key: "bandsText"; title: qsTr("Threshold bands"); flex: 1; minWidth: Tk.Theme.size.fieldWidth },
                    Tk.TableColumn { key: "targetId"; title: qsTr("ID"); width: Tk.Theme.size.fieldWidth }
                ]
                onActivated: function(index) { display.selectIndex(index, true) }
            }
        }
        Tk.Label { objectName: "targetLegend"; text: display.legendText
            muted: true; wrapMode: Text.WordWrap; Tk.Flex.shrink: 0 }
        Tk.Label { objectName: "targetError"
            text: display.targetModel ? display.targetModel.error : qsTr("No target model")
            color: Tk.Theme.color.danger; visible: text.length > 0; Tk.Flex.shrink: 0 }
    }
}
