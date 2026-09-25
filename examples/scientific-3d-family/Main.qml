// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The scientific 3D family in one window (WP-085 + WP-086): the sampled
// saddle surface with 3D probe points (ScientificPlot3D) and the calibrated
// sphere phantom in linked slices and a volume rendering (ImageSliceView,
// VolumeView, TransferFunctionEditor). Both pages stay loaded, so switching
// keeps each page's camera, cursor and transfer state. Demo data only.
// AGENT-NOTE: the pages are the scientific-plot-3d and volume-imaging
// examples themselves, not copies; tests/qml/tst_scientific_3d_family.qml
// checks both display their known data through this page.
Rectangle {
    id: page
    width: 1280
    height: 960
    color: Tk.Theme.color.bg
    property int currentPage: 0

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.md
            padding: Tk.Theme.space.sm
            Tk.Heading { text: qsTr("Scientific 3D"); level: 2 }
            Tk.PillSwitcher {
                objectName: "familySwitcher"
                small: true
                tooltip: qsTr("Choose a scientific 3D view")
                model: [{text: qsTr("Surface and scatter"), value: "surface", iconName: "spline"},
                        {text: qsTr("Volume and slices"), value: "volume", iconName: "box"}]
                currentIndex: page.currentPage
                onActivated: function(index) { page.currentPage = index }
            }
            Tk.Caption {
                Tk.Flex.grow: 1
                text: page.currentPage === 0
                    ? qsTr("Sampled lattice z = (x² − y²) / 4 with a gap and three probes, in metres")
                    : qsTr("41 × 41 × 21 sphere phantom, 0.8 × 0.8 × 1.6 mm voxels")
            }
        }
        Item {
            Tk.Flex.grow: 1
            Loader {
                id: surfacePage
                objectName: "familySurfacePage"
                anchors.fill: parent
                visible: page.currentPage === 0
                source: Qt.resolvedUrl("../scientific-plot-3d/Main.qml")
            }
            Loader {
                id: volumePage
                objectName: "familyVolumePage"
                anchors.fill: parent
                visible: page.currentPage === 1
                source: Qt.resolvedUrl("../volume-imaging/Main.qml")
            }
        }
    }
}
