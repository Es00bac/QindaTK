// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Telemetry"
    note: "Graph · GraphSeries · Sparkline · Meter · DataTable · TableColumn"

    // A believable trace rather than noise: a slow swell with bursts on it,
    // so the ramp colouring has something to say.
    function trace(i, phase) {
        return Math.max(2, Math.min(100,
            48 + 36 * Math.sin(i / 13 + phase) + ((i % 19) < 3 ? 26 : 0)))
    }

    GalleryRow {
        label: "Graph"
        Tk.Graph {
            id: loadGraph
            implicitWidth: 300
            implicitHeight: 72
            capacity: 120
            maxValue: 100
            gridRows: 3
            gridColor: Tk.Theme.color.divider
            baselineColor: Tk.Theme.color.border
            Tk.GraphSeries { id: loadSeries; ramp: Tk.Theme.ramp.load; fillOpacity: 0.55 }
            Component.onCompleted: {
                for (let i = 0; i < 120; ++i) {
                    loadSeries.append(section.trace(i, 0))
                }
            }
        }
        Tk.Graph {
            id: duplex
            implicitWidth: 300
            implicitHeight: 72
            capacity: 120
            autoScale: true
            autoScaleFloor: 20
            baselineColor: Tk.Theme.color.border
            // Receive above the line, send hanging below it.
            Tk.GraphSeries { id: rx; ramp: Tk.Theme.ramp.network; fillOpacity: 0.5 }
            Component.onCompleted: {
                for (let i = 0; i < 120; ++i) {
                    rx.append(section.trace(i, 2) * 8)
                }
            }
        }
    }

    GalleryRow {
        label: "Meter"
        Tk.Meter { implicitWidth: 140; implicitHeight: 8; value: 34; ramp: Tk.Theme.ramp.load }
        Tk.Meter { implicitWidth: 140; implicitHeight: 8; value: 88; ramp: Tk.Theme.ramp.load }
        Tk.Meter {
            implicitWidth: 140; implicitHeight: 8; value: 88
            ramp: Tk.Theme.ramp.load; rampMode: Tk.Meter.ByValue
        }
        Tk.Meter {
            implicitWidth: 140; implicitHeight: Tk.Theme.size.meter; value: 64
            segments: 24; ramp: Tk.Theme.ramp.memory; radius: Tk.Theme.radius.xs
        }
        Tk.Meter {
            implicitWidth: 12; implicitHeight: 40; vertical: true; value: 72
            segments: 10; ramp: Tk.Theme.ramp.thermal; radius: Tk.Theme.radius.xs
        }
    }

    GalleryRow {
        label: "Sparkline"
        Repeater {
            model: 4
            delegate: Tk.Sparkline {
                id: spark
                required property int index
                ramp: Tk.Theme.ramp.load
                Component.onCompleted: {
                    for (let i = 0; i < 40; ++i) {
                        spark.append(section.trace(i, spark.index * 1.7))
                    }
                }
            }
        }
    }

    GalleryRow {
        label: "DataTable"
        Tk.DataTable {
            id: table
            implicitWidth: 520
            implicitHeight: 130
            alternatingRows: true
            sortKey: "mem"
            sortOrder: Qt.DescendingOrder
            model: [
                { pid: 2154578, name: "cc1plus", user: "portage", mem: 2415, cpu: 3.9 },
                { pid: 2051249, name: "qindaqt-shell", user: "cabewse", mem: 407, cpu: 4.0 },
                { pid: 1930560, name: "kwin_wayland", user: "cabewse", mem: 275, cpu: 0.8 },
                { pid: 1474, name: "pipewire", user: "cabewse", mem: 136, cpu: 0.1 }
            ]
            // The gallery has no model to re-sort, so it only records the ask.
            onSortRequested: function(key, order) {
                table.sortKey = key
                table.sortOrder = order
            }
            columns: [
                Tk.TableColumn {
                    key: "pid"; title: "PID"; width: 72
                    align: Qt.AlignRight; mono: true; descendingFirst: true
                },
                Tk.TableColumn { key: "name"; title: "Program"; width: 120; flex: 1 },
                Tk.TableColumn { key: "user"; title: "User"; width: 70; muted: true },
                Tk.TableColumn {
                    key: "mem"; title: "Memory"; width: 78
                    align: Qt.AlignRight; mono: true; descendingFirst: true
                    formatter: function(v) { return v + " M" }
                },
                Tk.TableColumn {
                    key: "cpu"; title: "CPU%"; width: 60
                    align: Qt.AlignRight; mono: true; descendingFirst: true
                    ramp: Tk.Theme.ramp.load; rampTo: 5
                    formatter: function(v) { return v.toFixed(1) }
                }
            ]
        }
    }
}
