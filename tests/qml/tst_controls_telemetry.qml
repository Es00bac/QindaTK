// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Telemetry controls: Graph/GraphSeries, Meter, Sparkline, DataTable.
// These run under the software adaptation, which draws none of Graph's
// scene-graph nodes, so the assertions here are about state and geometry;
// the pixels are verified with `qtk-preview --gpu --grab`.
TestCase {
    id: root
    name: "ControlsTelemetry"
    when: windowShown
    // AGENT-GUARD: a TestCase is invisible by default and Flex skips hidden
    // children, so every geometry assertion below needs this line.
    visible: true
    width: 640
    height: 480

    Component {
        id: graphComponent
        Tk.Graph {
            width: 200
            height: 60
            capacity: 8
            maxValue: 100
            Tk.GraphSeries { objectName: "a" }
            Tk.GraphSeries { objectName: "b" }
        }
    }
    Component {
        id: meterComponent
        Tk.Meter { width: 120; height: 8; value: 40; ramp: Tk.Theme.ramp.load }
    }
    Component {
        id: sparklineComponent
        Tk.Sparkline { ramp: Tk.Theme.ramp.load }
    }
    Component {
        id: tableComponent
        Tk.DataTable {
            width: 400
            height: 200
            model: [
                { pid: 1, name: "init", cpu: 0.0 },
                { pid: 2, name: "kthreadd", cpu: 1.5 },
                { pid: 3, name: "rcu_gp", cpu: 9.0 }
            ]
            columns: [
                Tk.TableColumn { key: "pid"; title: "PID"; width: 60 },
                Tk.TableColumn { key: "name"; title: "Name"; width: 120; flex: 1 },
                Tk.TableColumn {
                    key: "cpu"; title: "CPU"; width: 60; descendingFirst: true
                    formatter: function(v) { return v.toFixed(1) }
                }
            ]
        }
    }

    function test_graph_series_are_adopted() {
        const graph = createTemporaryObject(graphComponent, root)
        verify(graph)
        compare(graph.seriesCount(), 2)
        // The graph owns the x axis: adopting a series applies its capacity.
        compare(graph.seriesAt(0).capacity, 8)
        compare(graph.seriesAt(1).capacity, 8)
    }

    function test_graph_append_row_aligns_series() {
        const graph = createTemporaryObject(graphComponent, root)
        graph.appendRow([10, 20])
        graph.appendRow([30, 40])
        compare(graph.seriesAt(0).count, 2)
        compare(graph.seriesAt(1).count, 2)
        compare(graph.seriesAt(0).last, 30)
        compare(graph.seriesAt(1).last, 40)
    }

    function test_graph_capacity_drops_oldest() {
        const graph = createTemporaryObject(graphComponent, root)
        for (let i = 0; i < 12; ++i) {
            graph.append(i)
        }
        const series = graph.seriesAt(0)
        compare(series.count, 8)
        compare(series.at(0), 4)
        compare(series.last, 11)
    }

    function test_graph_auto_scale_reports_effective_max() {
        const graph = createTemporaryObject(graphComponent, root)
        compare(graph.effectiveMax, 100)
        graph.autoScale = true
        graph.autoScaleFloor = 1
        graph.headroom = 2
        graph.appendTo(0, 25)
        compare(graph.effectiveMax, 50)
    }

    function test_meter_reports_position_and_colour() {
        const meter = createTemporaryObject(meterComponent, root)
        compare(meter.position, 0.4)
        compare(meter.valueColor, Tk.Theme.ramp.load.at(0.4))
        meter.value = 1000
        compare(meter.position, 1)
    }

    function test_sparkline_proxies_its_series() {
        const spark = createTemporaryObject(sparklineComponent, root)
        verify(spark.implicitHeight === Tk.Theme.size.sparklineHeight)
        spark.append(5)
        spark.append(9)
        compare(spark.last, 9)
        compare(spark.peak, 9)
        spark.values = [1, 2, 3]
        compare(spark.last, 3)
    }

    function test_table_counts_rows_and_columns() {
        const table = createTemporaryObject(tableComponent, root)
        compare(table.count, 3)
        compare(table.visibleColumns.length, 3)
        // recordAt() is the single way a cell reaches its data.
        compare(table.recordAt(1).name, "kthreadd")
        compare(table.recordAt(99), undefined)
    }

    function test_table_header_click_requests_sort_without_reordering() {
        const table = createTemporaryObject(tableComponent, root)
        const spy = signalSpy.createObject(root, { target: table, signalName: "sortRequested" })
        const header = findChild(table, "headerCell_cpu")
        verify(header)
        waitForRendering(table)
        mouseClick(header)
        compare(spy.count, 1)
        // descendingFirst: the first click on a "most of something" column
        // asks for the largest values.
        compare(spy.signalArguments[0][0], "cpu")
        compare(spy.signalArguments[0][1], Qt.DescendingOrder)
        // AGENT-GUARD: the table must not have reordered anything itself.
        compare(table.recordAt(0).pid, 1)
    }

    function test_table_sort_toggles_direction() {
        const table = createTemporaryObject(tableComponent, root)
        table.sortKey = "cpu"
        table.sortOrder = Qt.DescendingOrder
        const spy = signalSpy.createObject(root, { target: table, signalName: "sortRequested" })
        waitForRendering(table)
        mouseClick(findChild(table, "headerCell_cpu"))
        compare(spy.signalArguments[0][1], Qt.AscendingOrder)
    }

    Component { id: signalSpy; SignalSpy {} }
}
