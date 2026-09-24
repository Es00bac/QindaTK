// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "ScientificPlot"
    when: windowShown
    visible: true
    width: 720
    height: 480

    Component {
        id: figureFixture
        Item {
            property alias plot: figure
            width: 640
            height: 400
            Tk.ScientificDataset {
                id: measured
                sourceId: "run-a"
                xUnit: "s"
                yUnit: "V"
                Component.onCompleted: replaceSamples([
                    { id: "a0", x: 0, y: 1, sigmaYLow: 0.1, sigmaYHigh: 0.3 },
                    { id: "a1", x: 1, y: 2, sigmaX: 0.2 },
                    { id: "a2", x: 2, missingY: true },
                    { id: "a3", x: 3, y: 4 }])
            }
            Tk.ScientificDataset {
                id: cloud
                sourceId: "run-b"
                kind: Tk.ScientificDataset.Scatter
                xUnit: "s"
                yUnit: "V"
                Component.onCompleted: replaceSamples([
                    { id: "b0", x: 2.5, y: 3 }, { id: "b1", x: 0.5, y: 1.5 }])
            }
            Tk.ScientificGrid {
                id: field
                sourceId: "field"
                xUnit: "s"
                yUnit: "V"
                zUnit: "K"
                Component.onCompleted: replaceGrid([0, 4], [0, 5], [0, 4, 5, 9])
            }
            Tk.ScientificPlot {
                id: figure
                anchors.fill: parent
                title: "Two runs over a field"
                dataset: measured
                series: [cloud]
                surface: field
                Component.onCompleted: {
                    xAxis.minimum = 0; xAxis.maximum = 4
                    yAxis.minimum = 0; yAxis.maximum = 5
                }
            }
        }
    }

    Component {
        id: volumeFixture
        Item {
            property alias plot: volume
            property alias probes: probeSet
            width: 480
            height: 360
            Tk.ScientificGrid {
                id: saddle
                sourceId: "saddle"
                xUnit: "m"
                yUnit: "m"
                zUnit: "m"
                Component.onCompleted: {
                    const axis = [-1, -0.5, 0, 0.5, 1], values = []
                    for (let r = 0; r < 5; ++r)
                        for (let c = 0; c < 5; ++c) values.push(axis[c] * axis[c] - axis[r] * axis[r])
                    replaceGrid(axis, axis, values)
                }
            }
            Tk.ScientificDataset {
                id: probeSet
                sourceId: "probes"
                kind: Tk.ScientificDataset.Scatter
                dimensions: 3
                xUnit: "m"
                yUnit: "m"
                zUnit: "m"
                Component.onCompleted: replaceSamples([{ id: "top", x: 0, y: 0, z: 1 }])
            }
            Tk.ScientificPlot3D {
                id: volume
                anchors.fill: parent
                title: "Saddle"
                grid: saddle
                points: probeSet
                resolution: 9
                forceSoftware: true
            }
        }
    }

    SignalSpy { id: selectedSpy; signalName: "seriesSampleSelected" }
    SignalSpy { id: pointSpy; signalName: "pointSelected" }

    function test_two_series_surface_keyboard_and_table() {
        const root = createTemporaryObject(figureFixture, this)
        verify(root !== null)
        const figure = root.plot
        compare(figure.problem, "")
        compare(figure.allSeries.length, 2)
        const canvas = findChild(figure, "scientificPlotCanvas")
        verify(canvas !== null)
        compare(canvas.surface.rows, 2)
        verify(figure.summary.indexOf("Two runs over a field") === 0)
        verify(figure.summary.indexOf("Surface field") > 0)
        selectedSpy.target = figure
        selectedSpy.clear()
        figure.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(figure.selectedSampleId, "a0")
        keyClick(Qt.Key_Down)
        compare(figure.selectedSeries, 1)
        compare(figure.selectedSampleId, "b1") // nearest x to 0 in the scatter run
        compare(selectedSpy.count, 2)
        compare(selectedSpy.signalArguments[1][0], 1)
        const b0 = figure.projected(1, "b0")
        mouseClick(canvas, b0.x, b0.y)
        compare(figure.selectedSampleId, "b0")
        const cursor = figure.dataAt(b0.x, b0.y)
        compare(cursor.zUnit, "K")
        verify(Math.abs(cursor.z - 5.5) < 0.05) // the 2 x 2 field is z = x + y; bilinear is exact
        figure.showDataTable = true
        verify(findChild(figure, "scientificPlotDataTable").visible)
        figure.tableSource = -1
        verify(findChild(figure, "scientificPlotSurfaceTable").visible)
        verify(!findChild(figure, "scientificPlotDataTable").visible)
        compare(figure.implicitWidth, Tk.Theme.size.panelMinWidth)
    }

    function test_software_3d_orbit_pick_and_summary() {
        const root = createTemporaryObject(volumeFixture, this)
        verify(root !== null)
        const volume = root.plot
        verify(volume.fitDomain())
        compare(volume.scene.triangleCount, 2 * 8 * 8)
        compare(volume.scene.holeCount, 0)
        verify(!volume.gpuRenderer)
        verify(findChild(volume, "scientificPlot3DSoftware").visible)
        const before = volume.scene.azimuth
        volume.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(volume.scene.azimuth, before + volume.orbitStep)
        pointSpy.target = volume
        pointSpy.clear()
        const top = volume.project(0, 0, 1)
        verify(top.visible)
        mouseClick(volume, volume.leftPadding + top.x, volume.topPadding + top.y)
        compare(volume.selectedPointId, "top")
        compare(pointSpy.count, 1)
        verify(volume.textSummary().indexOf("bilinear") > 0)
        verify(volume.Accessible.description.indexOf("1 points") > 0)
    }
}
