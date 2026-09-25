// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A sampled saddle surface z = (x² - y²) / 4 with a gap, plus 3D probe points.
// Qt Quick 3D draws it on GPU backends; the software painter (the same one
// that exports) draws it elsewhere. Demo data only.
Rectangle {
    width: 1080
    height: 720
    color: Tk.Theme.color.bg

    Tk.ScientificGrid {
        id: saddle
        sourceId: "demo:saddle"
        label: "Saddle"
        xUnit: "m"
        yUnit: "m"
        zUnit: "m"
        parameters: ({ model: "(x² - y²) / 4", lattice: "21 x 21" })
    }
    Tk.ScientificDataset {
        id: probes
        sourceId: "demo:saddle-probes"
        label: "Probes"
        kind: Tk.ScientificDataset.Scatter
        dimensions: 3
        xUnit: "m"
        yUnit: "m"
        zUnit: "m"
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Heading { text: qsTr("Scientific 3D surface"); level: 1 }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "scientific3DInterpolation"
                text: volume.interpolation === Tk.ScientificGrid.Bilinear ? qsTr("Bilinear") : qsTr("Nearest")
                tooltip: qsTr("Switch between nearest-node and bilinear sampling of the lattice")
                onClicked: volume.interpolation = volume.interpolation === Tk.ScientificGrid.Bilinear
                    ? Tk.ScientificGrid.Nearest : Tk.ScientificGrid.Bilinear
            }
            Tk.Button {
                objectName: "scientific3DTable"
                text: volume.showDataTable ? qsTr("Plot") : qsTr("Table")
                tooltip: qsTr("Show the surface lattice or the probe points as a table")
                onClicked: volume.showDataTable = !volume.showDataTable
            }
            Tk.Caption { text: volume.selectedPointId ? qsTr("Selected %1").arg(volume.selectedPointId) : qsTr("Click a probe to select it") }
        }
        Tk.ScientificPlot3D {
            id: volume
            objectName: "scientific3DExample"
            Tk.Flex.flex: 1
            title: "Saddle z = (x² − y²) / 4"
            grid: saddle
            points: probes
            resolution: 41
        }
    }

    Component.onCompleted: {
        const axis = [], values = []
        for (let i = 0; i <= 20; ++i) axis.push(-2 + i * 0.2)
        for (let r = 0; r <= 20; ++r)
            for (let c = 0; c <= 20; ++c)
                values.push(r >= 14 && r <= 15 && c >= 3 && c <= 4 ? null
                            : (axis[c] * axis[c] - axis[r] * axis[r]) / 4)
        saddle.replaceGrid(axis, axis, values)
        probes.replaceSamples([
            { id: "ridge", x: 1.8, y: 0, z: 0.81 },
            { id: "valley", x: 0, y: -1.8, z: -0.81 },
            { id: "centre", x: 0, y: 0, z: 0 }])
        volume.fitDomain()
    }
}
