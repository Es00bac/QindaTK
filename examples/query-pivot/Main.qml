// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1120
    height: 760
    color: Tk.Theme.color.bg

    Tk.EditableGridModel { id: sourceGrid }
    Tk.QueryController {
        id: controller
        source: sourceGrid
        schema: ({ "id": "sales", "revision": "schema-1", "fields": [
            { "id": "region", "label": qsTr("Region"), "type": "text", "nullable": false },
            { "id": "quarter", "label": qsTr("Quarter"), "type": "text", "nullable": false },
            { "id": "amount", "label": qsTr("Revenue"), "type": "number", "nullable": true },
            { "id": "units", "label": qsTr("Units"), "type": "integer", "nullable": false },
            { "id": "active", "label": qsTr("Active"), "type": "boolean", "nullable": false }
        ] })
    }

    Component.onCompleted: {
        sourceGrid.setRowIds(["r-01", "r-02", "r-03", "r-04", "r-05", "r-06"])
        sourceGrid.setColumnIds(["region", "quarter", "amount", "units", "active"])
        sourceGrid.beginEdit("Load sample sales data")
        const rows = [
            ["North", "Q1", 42.0, 7, true], ["South", "Q1", 35.5, 5, true],
            ["North", "Q2", 50.0, 8, true], ["West", "Q1", null, 0, false],
            ["South", "Q2", 41.0, 6, true], ["West", "Q2", 28.5, 4, true]
        ]
        const ids = ["r-01", "r-02", "r-03", "r-04", "r-05", "r-06"]
        const fields = ["region", "quarter", "amount", "units", "active"]
        for (let r = 0; r < rows.length; ++r)
            for (let c = 0; c < fields.length; ++c)
                sourceGrid.stageCell(ids[r], fields[c], rows[r][c])
        sourceGrid.commitEdit()
        builder.rowFields = ["region", "quarter"]
        builder.columnFields = []
        builder.measures = [{ "id": "revenue", "field": "amount", "function": "sum" }]
        builder.requestPreview()
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Overline { title: qsTr("Query and pivot"); color: Tk.Theme.color.text; wide: true }
            Tk.Caption { text: qsTr("Expand region and quarter subtotals, then drill through to source rows.") }
        }
        Tk.QueryBuilder {
            id: builder
            objectName: "salesQueryBuilder"
            width: parent.width
            controller: controller
            Tk.Flex.shrink: 0
        }
        Tk.PivotView {
            objectName: "salesPivotView"
            controller: controller
            tooltip: qsTr("Sales revenue by region and quarter; activate a cell for source-row provenance")
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
        }
    }
}
