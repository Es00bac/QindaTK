// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Filters and pivot results"
    note: "QueryController · FilterBar · QueryBuilder · PivotView"

    Tk.EditableGridModel { id: sourceGrid }
    Tk.QueryController {
        id: controller
        source: sourceGrid
        schema: ({ "id": "gallery-sales", "revision": "1", "fields": [
            { "id": "region", "label": "Region", "type": "text", "nullable": false },
            { "id": "quarter", "label": "Quarter", "type": "text", "nullable": false },
            { "id": "amount", "label": "Revenue", "type": "number", "nullable": true }
        ] })
    }
    Component.onCompleted: {
        sourceGrid.setRowIds(["g1", "g2", "g3", "g4"])
        sourceGrid.setColumnIds(["region", "quarter", "amount"])
        sourceGrid.beginEdit("Load query gallery data")
        const sample = [["North", "Q1", 42], ["South", "Q1", 35.5],
                        ["North", "Q2", 50], ["South", "Q2", 41]]
        const ids = ["g1", "g2", "g3", "g4"]
        const columns = ["region", "quarter", "amount"]
        for (let r = 0; r < sample.length; ++r)
            for (let c = 0; c < columns.length; ++c)
                sourceGrid.stageCell(ids[r], columns[c], sample[r][c])
        sourceGrid.commitEdit()
        builder.rowFields = ["region"]
        builder.columnFields = ["quarter"]
        builder.measures = [{ "id": "revenue", "field": "amount", "function": "sum" }]
        builder.requestPreview()
    }

    Tk.QueryBuilder {
        id: builder
        width: 940
        controller: controller
        Tk.Flex.shrink: 0
    }
    Tk.PivotView {
        width: 940
        height: 240
        controller: controller
        tooltip: qsTr("Activate an aggregate to inspect its contributing rows")
        Tk.Flex.shrink: 0
    }
}
