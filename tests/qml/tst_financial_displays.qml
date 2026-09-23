// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "FinancialDisplays"
    when: windowShown
    visible: true
    width: 760
    height: 540

    Component {
        id: candleFixture
        Tk.CandlestickView {
            width: 700; height: 240
            Tk.OhlcModel {
                id: source
                Component.onCompleted: replaceCandles([
                    {id: "a", utcMs: "1", open: 10, high: 12, low: 9, close: 11},
                    {id: "gap", utcMs: "2", missing: true},
                    {id: "c", utcMs: "3", open: 11, high: 13, low: 10, close: 10}
                ], "USD/share", "Host session", "UTC", 10)
            }
            candleModel: source
        }
    }
    Component {
        id: targetFixture
        Tk.TargetDisplay {
            width: 700; height: 240
            Tk.TargetModel {
                id: source
                Component.onCompleted: replaceTargets([
                    {id: "first", label: "First", unit: "%", minimum: 0, maximum: 100,
                     value: 70, target: 80, direction: "higher", thresholdMeaning: "Availability",
                     bands: [{end: 50, meaning: "Low"}, {end: 100, meaning: "High"}]},
                    {id: "second", label: "Second", unit: "ms", minimum: 0, maximum: 10,
                     missing: true, target: 5, direction: "lower", thresholdMeaning: "Latency",
                     bands: [{end: 5, meaning: "Good"}, {end: 10, meaning: "Slow"}]}
                ])
            }
            targetModel: source
        }
    }
    Component { id: selectionSpy; SignalSpy { signalName: "candleSelected" } }
    Component { id: targetSpy; SignalSpy { signalName: "targetSelected" } }

    function test_candles_selection_missing_and_table() {
        const view = createTemporaryObject(candleFixture, root)
        verify(view !== null)
        tryCompare(view.candleModel, "count", 3)
        compare(view.selectedCandleId, "a")
        compare(view.visibleRecords.length, 3)
        verify(view.legendText.includes("USD/share"))
        verify(view.csvData().includes("gap"))
        const spy = createTemporaryObject(selectionSpy, root, {target: view})
        view.selectedCandleId = "c"
        compare(spy.count, 0)
        view.selectIndex(1, true)
        compare(view.selectedCandleId, "gap")
        compare(spy.count, 1)
        waitForRendering(view)
        const canvas = findChild(view, "candleCanvas")
        mouseClick(view, canvas.x + canvas.leftEdge() +
                   (canvas.rightEdge() - canvas.leftEdge()) * 5 / 6,
                   canvas.y + findChild(view, "candleBody").y + canvas.height / 2)
        compare(view.selectedCandleId, "c")
        compare(spy.count, 2)
        view.selectedCandleId = "gap"
        view.showDataTable = true
        verify(findChild(view, "candleDataTable").visible)
        const source = view.candleModel
        verify(source.replaceCandles([
            {id: "c", utcMs: "3", open: 11, high: 13, low: 10, close: 10},
            {id: "gap", utcMs: "4", missing: true}
        ], "USD/share", "Host session", "UTC", null))
        compare(view.selectedCandleId, "gap")
        compare(spy.count, 2)
        view.candleModel = null
        compare(view.selectedCandleId, "")
    }

    function test_targets_modes_selection_and_table() {
        const view = createTemporaryObject(targetFixture, root)
        verify(view !== null)
        tryCompare(view.targetModel, "count", 2)
        compare(view.selectedTargetId, "first")
        verify(view.legendText.includes("Availability"))
        const spy = createTemporaryObject(targetSpy, root, {target: view})
        view.selectedTargetId = "second"
        compare(spy.count, 0)
        view.selectIndex(0, true)
        compare(spy.count, 1)
        waitForRendering(view)
        const canvas = findChild(view, "targetCanvas")
        mouseClick(view, canvas.x + canvas.width / 2,
                   findChild(view, "targetBody").y + canvas.y + canvas.height * 3 / 4)
        compare(view.selectedTargetId, "second")
        compare(spy.count, 2)
        view.mode = "gauge"
        compare(view.visibleRecords.length, 2)
        mouseClick(view, canvas.x + canvas.width / 4,
                   findChild(view, "targetBody").y + canvas.y + canvas.height / 2)
        compare(view.selectedTargetId, "first")
        compare(spy.count, 3)
        view.showDataTable = true
        verify(findChild(view, "targetDataTable").visible)
        verify(view.csvData().includes("missing"))
        view.targetModel = null
        compare(view.selectedTargetId, "")
    }
}
