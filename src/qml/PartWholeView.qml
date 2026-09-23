// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: view
    property Tk.PartWholeModel model: null
    property int mode: Tk.PartWholeRenderer.Pie
    property string tooltip: qsTr("Part-to-whole chart")
    property bool showDataTable: false
    readonly property string selectedId: renderer.selectedId
    readonly property var selectedNode: renderer.selectedNode
    signal itemSelected(string id)
    function selectId(id) { return renderer.selectId(id) }
    function navigateSelection(key) { return renderer.navigateSelection(key) }
    function hitAt(x, y) { return renderer.hitAt(x, y) }
    function geometryFor(id) { return renderer.geometryFor(id) }
    function renderImage(size) { return renderer.renderImage(size) }
    function exportPng(path, size) { return renderer.exportPng(path, size) }
    function exportSvg(path, size) { return renderer.exportSvg(path, size) }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    objectName: "partWholeView"
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: model ? qsTr("Known total %1 %2. %3 missing parts and %7 zero parts. Selected %4: %5 %6. Toggle the data table to inspect all IDs and values.")
        .arg(model.knownTotal).arg(model.unit).arg(model.missingCount)
        .arg(selectedId || qsTr("none"))
        .arg(selectedNode.status || qsTr("none"))
        .arg(selectedNode.knownTotal === undefined ? "" : selectedNode.knownTotal)
        .arg(model.zeroCount)
        : qsTr("No data")
    Accessible.focusable: true
    Accessible.focused: activeFocus || renderer.activeFocus
    Keys.onPressed: function(event) {
        if (renderer.navigateSelection(event.key)) event.accepted = true
    }

    Tk.PartWholeRenderer {
        id: renderer
        objectName: "partWholePlot"
        anchors.fill: parent
        visible: !view.showDataTable
        model: view.model
        mode: view.mode
        sliceColors: [Tk.Theme.color.accent, Tk.Theme.color.warning,
                  Tk.Theme.color.success, Tk.Theme.color.info,
                  Tk.Theme.color.danger]
        backgroundColor: Tk.Theme.color.canvas
        borderColor: Tk.Theme.color.border
        textColor: Tk.Theme.color.textMuted
        selectionColor: Tk.Theme.color.focus
        metrics: ({ margin: Tk.Theme.space.sm,
                    bottomMargin: Tk.Theme.font.body + Tk.Theme.space.sm,
                    fontSize: Tk.Theme.font.caption,
                    labelPadding: Tk.Theme.space.xs,
                    lineWidth: Tk.Theme.size.border })
        onItemSelected: function(id) { view.itemSelected(id) }
    }
    Rectangle {
        objectName: "partWholeFocusRing"
        anchors.fill: renderer
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.alpha(Tk.Theme.color.canvas, Tk.Theme.opacity.hidden)
        border.color: Tk.Theme.color.focus
        border.width: Tk.Theme.size.focusRing
        visible: (view.activeFocus || renderer.activeFocus) && !view.showDataTable
    }
    Tk.DataTable {
        id: table
        objectName: "partWholeDataTable"
        anchors.fill: parent
        visible: view.showDataTable
        model: view.model
        tooltip: view.tooltip + qsTr(" data")
        columns: [
            Tk.TableColumn { key: "label"; title: qsTr("Part"); flex: 1; minWidth: Tk.Theme.size.fieldWidth },
            Tk.TableColumn { key: "parentId"; title: qsTr("Parent ID"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "knownTotal"; title: qsTr("Known total"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "status"; title: qsTr("Status"); width: Tk.Theme.size.fieldWidth },
            Tk.TableColumn { key: "share"; title: qsTr("Known share"); width: Tk.Theme.size.fieldWidth; mono: true;
                formatter: function(value) { return Math.round(value * 100) + "%" } },
            Tk.TableColumn { key: "id"; title: qsTr("ID"); width: Tk.Theme.size.fieldWidth; mono: true }
        ]
    }
}
