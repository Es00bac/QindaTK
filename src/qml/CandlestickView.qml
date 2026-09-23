// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/financial-displays.md): the model owns exact UTC time,
// price validation and host session meaning; this view uses equal session slots.
Item {
    id: view
    objectName: "candlestickView"
    property Tk.OhlcModel candleModel: null
    property string tooltip: qsTr("Candlestick chart")
    property string selectedCandleId: ""
    property int windowStart: 0
    property int windowCount: 64
    property bool showDataTable: false
    readonly property int visibleStart: candleModel
        ? Math.max(0, Math.min(windowStart, Math.max(0, candleModel.count - 1))) : 0
    readonly property int visibleCount: candleModel
        ? Math.min(512, Math.max(1, windowCount), candleModel.count - visibleStart) : 0
    readonly property var visibleRecords: {
        if (!candleModel) return []
        const generation = candleModel.generation
        return candleModel.page(visibleStart, visibleCount)
    }
    readonly property string legendText: candleModel
        ? qsTr("OHLC · %1 · %2 · %3 · equal session slots%4")
            .arg(candleModel.priceUnit).arg(candleModel.intervalMeaning)
            .arg(candleModel.timeZoneId)
            .arg(candleModel.hasReferencePrice
                 ? qsTr(" · reference %1").arg(candleModel.referencePrice) : "")
        : qsTr("No OHLC model")
    signal candleSelected(string candleId)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: legendText + qsTr(". Use arrow keys to select sessions. Data table available.")
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    function csvData() { return candleModel ? candleModel.csv() : "" }
    function selectIndex(index, userAction) {
        if (!candleModel || index < 0 || index >= candleModel.count) return false
        const id = candleModel.get(index).candleId
        selectedCandleId = id
        if (index < visibleStart || index >= visibleStart + visibleCount)
            windowStart = Math.max(0, index - Math.floor(windowCount / 2))
        if (userAction) candleSelected(id)
        canvas.requestPaint()
        return true
    }
    function navigate(step) {
        if (!candleModel || !candleModel.count) return false
        const index = candleModel.indexOfId(selectedCandleId)
        return selectIndex(Math.max(0, Math.min(candleModel.count - 1,
                           index < 0 ? 0 : index + step)), true)
    }
    function shiftWindow(step) {
        if (!candleModel) return
        windowStart = Math.max(0, Math.min(candleModel.count - 1,
                                          visibleStart + step))
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
            event.accepted = navigate(1)
        else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
            event.accepted = navigate(-1)
        else if (event.key === Qt.Key_PageDown) {
            shiftWindow(visibleCount); event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            shiftWindow(-visibleCount); event.accepted = true
        }
    }
    onCandleModelChanged: reconcileSelection()
    onSelectedCandleIdChanged: canvas.requestPaint()
    onWindowStartChanged: canvas.requestPaint()
    onWindowCountChanged: canvas.requestPaint()
    function reconcileSelection() {
        if (!candleModel) selectedCandleId = ""
        else if (candleModel.indexOfId(selectedCandleId) < 0)
            selectedCandleId = candleModel.count ? candleModel.get(0).candleId : ""
        canvas.requestPaint()
    }
    Connections {
        target: view.candleModel
        function onStateChanged() { view.reconcileSelection() }
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
            objectName: "candleToolbar"
            color: Tk.Theme.color.panel
            implicitHeight: Tk.Theme.size.toolbar
            Tk.Flex.shrink: 0
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                padding: Tk.Theme.space.xs
                Tk.Button { text: qsTr("Previous"); small: true
                    tooltip: qsTr("Previous session page")
                    onClicked: view.shiftWindow(-view.visibleCount) }
                Tk.Button { text: qsTr("Next"); small: true
                    tooltip: qsTr("Next session page")
                    onClicked: view.shiftWindow(view.visibleCount) }
                Tk.Label { text: qsTr("%1–%2 / %3 sessions")
                    .arg(view.visibleCount ? view.visibleStart + 1 : 0)
                    .arg(view.visibleStart + view.visibleCount)
                    .arg(view.candleModel ? view.candleModel.count : 0)
                    muted: true; Tk.Flex.grow: 1 }
                Tk.Button { text: qsTr("Data"); small: true
                    tooltip: qsTr("Show accessible OHLC table")
                    variant: view.showDataTable ? "accent" : "default"
                    onClicked: view.showDataTable = !view.showDataTable }
            }
        }
        Item {
            objectName: "candleBody"
            Tk.Flex.grow: 1
            Canvas {
                id: canvas
                objectName: "candleCanvas"
                anchors.fill: parent
                visible: !view.showDataTable
                Accessible.role: Accessible.Pane
                Accessible.name: view.tooltip + qsTr(" visual chart")
                Accessible.description: view.legendText
                function leftEdge() { return Tk.Theme.size.labelWidth / 2 }
                function rightEdge() { return width - Tk.Theme.space.md }
                function topEdge() { return Tk.Theme.space.md }
                function bottomEdge() { return height - Tk.Theme.size.row }
                function yFor(value) {
                    return bottomEdge() - (value - view.candleModel.minimum)
                        / (view.candleModel.maximum - view.candleModel.minimum)
                        * (bottomEdge() - topEdge())
                }
                function selectAt(x) {
                    if (!view.visibleCount) return
                    const slot = (rightEdge() - leftEdge()) / view.visibleCount
                    const index = Math.floor((x - leftEdge()) / slot)
                    if (index >= 0 && index < view.visibleCount) {
                        view.selectIndex(view.visibleStart + index, true)
                        view.forceActiveFocus()
                    }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!view.candleModel || !view.visibleCount) return
                    const left = leftEdge(), right = rightEdge()
                    const top = topEdge(), bottom = bottomEdge()
                    if (right <= left || bottom <= top) return
                    ctx.lineWidth = Tk.Theme.size.border
                    ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
                    ctx.textBaseline = "middle"
                    for (let tick = 0; tick <= 4; ++tick) {
                        const value = view.candleModel.minimum
                            + (view.candleModel.maximum - view.candleModel.minimum) * tick / 4
                        const y = yFor(value)
                        ctx.strokeStyle = Tk.Theme.color.border
                        ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
                        ctx.fillStyle = Tk.Theme.color.textMuted
                        ctx.fillText(Number(value).toPrecision(4), Tk.Theme.space.xs, y)
                    }
                    if (view.candleModel.hasReferencePrice) {
                        const y = yFor(view.candleModel.referencePrice)
                        ctx.strokeStyle = Tk.Theme.color.focus
                        ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
                    }
                    const slot = (right - left) / view.visibleCount
                    const bodyHalf = Math.min(slot * 0.3, Tk.Theme.size.controlSm / 2)
                    for (let index = 0; index < view.visibleRecords.length; ++index) {
                        const record = view.visibleRecords[index]
                        const x = left + (index + 0.5) * slot
                        if (record.candleId === view.selectedCandleId) {
                            ctx.strokeStyle = Tk.Theme.color.focus
                            ctx.strokeRect(x - slot / 2 + Tk.Theme.size.border, top,
                                           slot - 2 * Tk.Theme.size.border, bottom - top)
                        }
                        if (record.missing) {
                            ctx.strokeStyle = Tk.Theme.color.textMuted
                            ctx.beginPath(); ctx.moveTo(x - bodyHalf, bottom - bodyHalf)
                            ctx.lineTo(x + bodyHalf, bottom); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(x + bodyHalf, bottom - bodyHalf)
                            ctx.lineTo(x - bodyHalf, bottom); ctx.stroke()
                            continue
                        }
                        const color = record.close >= record.open
                            ? Tk.Theme.color.success : Tk.Theme.color.danger
                        ctx.strokeStyle = color
                        ctx.fillStyle = color
                        ctx.beginPath(); ctx.moveTo(x, yFor(record.high))
                        ctx.lineTo(x, yFor(record.low)); ctx.stroke()
                        const upper = Math.min(yFor(record.open), yFor(record.close))
                        const lower = Math.max(yFor(record.open), yFor(record.close))
                        const bodyHeight = Math.max(Tk.Theme.size.border, lower - upper)
                        if (record.close >= record.open)
                            ctx.strokeRect(x - bodyHalf, upper, bodyHalf * 2, bodyHeight)
                        else ctx.fillRect(x - bodyHalf, upper, bodyHalf * 2, bodyHeight)
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: function(event) { canvas.selectAt(event.x) }
                }
            }
            Tk.DataTable {
                objectName: "candleDataTable"
                anchors.fill: parent
                visible: view.showDataTable
                model: view.candleModel
                tooltip: view.tooltip + qsTr(" OHLC data")
                columns: [
                    Tk.TableColumn { key: "utcMs"; title: qsTr("UTC ms"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "open"; title: qsTr("Open"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "high"; title: qsTr("High"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "low"; title: qsTr("Low"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "close"; title: qsTr("Close"); width: Tk.Theme.size.fieldWidth; mono: true },
                    Tk.TableColumn { key: "missing"; title: qsTr("Missing"); width: Tk.Theme.size.fieldWidth },
                    Tk.TableColumn { key: "candleId"; title: qsTr("ID"); flex: 1; minWidth: Tk.Theme.size.fieldWidth }
                ]
                onActivated: function(index) { view.selectIndex(index, true) }
            }
        }
        Tk.Label { objectName: "candleLegend"; text: view.legendText
            muted: true; wrapMode: Text.WordWrap; Tk.Flex.shrink: 0 }
        Tk.Label { objectName: "candleError"
            text: view.candleModel ? view.candleModel.error : qsTr("No OHLC model")
            color: Tk.Theme.color.danger; visible: text.length > 0; Tk.Flex.shrink: 0 }
    }
}
