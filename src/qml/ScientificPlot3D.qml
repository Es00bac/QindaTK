// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// 3D scientific plot: a sampled surface (ScientificGrid) and/or 3D scatter
// points (ScientificDataset, dimensions 3) in a calibrated domain cube.
// AGENT-NOTE: ScientificScene3D (C++) owns the data-to-scene transform, the
// explicit interpolation lattice and the orbit camera. The Qt Quick 3D
// renderer (ScientificPlot3DRenderer.qml, GPU backends only) and the software
// view (ScientificScene3DView, also the export painter) both draw from it, so
// they agree; see docs/scientific-plot.md and decision D-140.
T.Control {
    id: control
    objectName: "scientificPlot3D"
    property Tk.ScientificGrid grid: null
    property Tk.ScientificDataset points: null
    property int interpolation: Tk.ScientificGrid.Bilinear
    property int resolution: 48
    property string title: ""
    property string tooltip: qsTr("3D plot: drag to orbit, wheel or +/- to zoom, arrow keys orbit, click a point to select it")
    property bool showDataTable: false
    // Draw with the software painter even when a GPU backend is available.
    property bool forceSoftware: false
    property real orbitStep: 5
    readonly property alias scene: plotScene
    readonly property bool gpuRenderer: renderer.status === Loader.Ready && renderer.active
    readonly property string selectedPointId: plotScene.selectedPointId
    signal pointSelected(string id)
    function fitDomain() { return plotScene.fitDomain() }
    function project(x, y, z) { return plotScene.project(x, y, z, contentItem.width, contentItem.height) }
    function csvData() { return plotScene.csvData() }
    function textSummary() { return plotScene.textSummary() }
    function metadataJson(width, height) { return plotScene.metadataJson(width, height) }
    function exportPng(path, size) { return plotScene.exportPng(path, size) }
    function exportSvg(path, size) { return plotScene.exportSvg(path, size) }

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Graphic
    Accessible.name: title || tooltip
    Accessible.description: plotScene.summary

    Tk.ScientificScene3D {
        id: plotScene
        grid: control.grid
        points: control.points
        interpolation: control.interpolation
        resolutionX: control.resolution
        resolutionY: control.resolution
        title: control.title
        surfaceColors: Tk.Theme.ramp.thermal.colors
        pointColor: Tk.Theme.color.accent
        frameColor: Tk.Theme.color.border
        backgroundColor: Tk.Theme.color.canvas
        textColor: Tk.Theme.color.textMuted
        selectionColor: Tk.Theme.color.focus
        metrics: ({ fontFamily: Tk.Theme.font.family, fontSize: Tk.Theme.font.caption,
                    pointRadius: Tk.Theme.size.iconSm / 3, lineWidth: Tk.Theme.size.border,
                    hitRadius: Tk.Theme.size.controlSm / 2, labelPadding: Tk.Theme.space.xs })
        onPointSelected: function(id) { control.pointSelected(id) }
    }

    background: Rectangle {
        color: Tk.Theme.color.canvas
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: control.activeFocus ? Tk.Theme.size.focusRing : Tk.Theme.size.border
    }

    contentItem: Item {
        id: stage
        Loader {
            id: renderer
            objectName: "scientificPlot3DRendererLoader"
            anchors.fill: parent
            active: control.GraphicsInfo.api !== GraphicsInfo.Software && !control.forceSoftware
            visible: !control.showDataTable
            source: "ScientificPlot3DRenderer.qml"
            onLoaded: item.plotScene = plotScene
        }
        Tk.ScientificScene3DView {
            objectName: "scientificPlot3DSoftware"
            anchors.fill: parent
            visible: !control.gpuRenderer && !control.showDataTable
            scene: plotScene
        }
        // Axis labels over the GPU view (the software view paints its own
        // chips). Same anchors, chip style and clamping as the painter.
        Repeater {
            model: {
                plotScene.azimuth; plotScene.elevation; plotScene.distance; plotScene.fieldOfView
                plotScene.meshRevision
                return control.gpuRenderer && !control.showDataTable
                    ? plotScene.axisLabels(stage.width, stage.height) : []
            }
            delegate: Rectangle {
                id: chip
                required property var modelData
                required property int index
                readonly property real pad: Tk.Theme.space.xs
                objectName: "scientificPlot3DAxisLabel" + index
                visible: modelData.visible === true
                width: chipText.implicitWidth + pad * 2
                height: chipText.implicitHeight + pad
                x: Math.max(pad, Math.min(stage.width - width - pad, modelData.x + Tk.Theme.size.iconSm / 3))
                y: Math.max(pad, Math.min(stage.height - height - pad, modelData.y - Tk.Theme.size.iconSm / 3 - height))
                z: 1
                color: Tk.Theme.color.canvas
                border.color: Tk.Theme.color.border
                border.width: Tk.Theme.size.border
                Tk.Label {
                    id: chipText
                    anchors.centerIn: parent
                    muted: true
                    font.pixelSize: Tk.Theme.font.caption
                    text: chip.modelData.text
                }
            }
        }
        MouseArea {
            id: gesture
            objectName: "scientificPlot3DGesture"
            anchors.fill: parent
            enabled: !control.showDataTable
            property real lastX: 0
            property real lastY: 0
            property bool moved: false
            onPressed: function(mouse) {
                control.forceActiveFocus()
                lastX = mouse.x; lastY = mouse.y; moved = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const dx = mouse.x - lastX, dy = mouse.y - lastY
                if (Math.abs(dx) + Math.abs(dy) > 0) moved = true
                // Half a turn across the view width.
                plotScene.orbit(-dx * 180 / Math.max(1, width), dy * 180 / Math.max(1, height))
                lastX = mouse.x; lastY = mouse.y
            }
            onClicked: function(mouse) {
                if (moved) return
                const id = plotScene.hitPoint(mouse.x, mouse.y, width, height)
                if (id.length) plotScene.selectPoint(id)
            }
            onWheel: function(wheel) { plotScene.dolly(Math.pow(2, -wheel.angleDelta.y / 480)) }
        }
        Tk.DataTable {
            objectName: "scientificPlot3DDataTable"
            anchors.fill: parent
            visible: control.showDataTable
            model: control.grid ? control.grid : control.points
            tooltip: control.tooltip
            columns: [
                Tk.TableColumn { key: "id"; title: qsTr("ID"); width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "x"; title: qsTr("X"); width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "y"; title: qsTr("Y"); width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "z"; title: qsTr("Z"); width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "zUnit"; title: qsTr("Z unit"); width: Tk.Theme.size.controlLg }
            ]
        }
        Tk.Label {
            anchors.centerIn: parent
            visible: plotScene.problem.length > 0 && control.gpuRenderer
            text: plotScene.problem
        }
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Left) plotScene.orbit(-control.orbitStep, 0)
        else if (event.key === Qt.Key_Right) plotScene.orbit(control.orbitStep, 0)
        else if (event.key === Qt.Key_Up) plotScene.orbit(0, control.orbitStep)
        else if (event.key === Qt.Key_Down) plotScene.orbit(0, -control.orbitStep)
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) plotScene.dolly(0.8)
        else if (event.key === Qt.Key_Minus) plotScene.dolly(1.25)
        else return
        event.accepted = true
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: control.tooltip; visible: hover.hovered && !control.showDataTable }
}
