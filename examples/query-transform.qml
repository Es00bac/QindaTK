// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1280
    height: 820
    color: Tk.Theme.color.bg
    property string savedDocument: ""
    Tk.EditableGridModel { id: orders }
    Tk.EditableGridModel { id: catalog }
    Tk.QueryController {
        id: controller
        source: orders
        joinSource: catalog
        schema: ({ id: "orders", revision: "orders-schema-2", fields: [
            { id: "region", label: "Region", type: "text", nullable: false },
            { id: "sku", label: "SKU", type: "text", nullable: false },
            { id: "amount", label: "Amount", type: "number", nullable: true }
        ] })
        joinSchema: ({ id: "catalog", revision: "catalog-schema-1", fields: [
            { id: "sku", label: "SKU", type: "text", nullable: false },
            { id: "category", label: "Category", type: "text", nullable: false }
        ] })
    }
    Component.onCompleted: {
        orders.setRowIds(["o-01", "o-02", "o-03", "o-04"])
        orders.setColumnIds(["region", "sku", "amount"])
        orders.beginEdit("Demo orders")
        const left = [["East", "A", 20], ["West", "B", 9],
                      ["East", "A", 12], ["North", "C", 6]]
        const leftFields = ["region", "sku", "amount"]
        for (let r = 0; r < left.length; ++r)
            for (let c = 0; c < leftFields.length; ++c)
                orders.stageCell(orders.rowIdAt(r), leftFields[c], left[r][c])
        orders.commitEdit()
        catalog.setRowIds(["p-01", "p-02", "p-03"])
        catalog.setColumnIds(["sku", "category"])
        catalog.beginEdit("Demo catalog")
        const right = [["A", "Tools"], ["A", "Supplies"], ["B", "Office"]]
        for (let r = 0; r < right.length; ++r) {
            catalog.stageCell(catalog.rowIdAt(r), "sku", right[r][0])
            catalog.stageCell(catalog.rowIdAt(r), "category", right[r][1])
        }
        catalog.commitEdit()
        builder.joinEnabled = true
        builder.joinLeftField = "sku"
        builder.joinRightField = "sku"
        builder.joinLeftOuter = true
        builder.detailMode = true
        builder.projection = ["region", "sku", "catalog.category", "amount"]
        builder.sortField = "catalog.category"
        builder.sortAscending = true
        builder.requestPreview()
        savedDocument = controller.saveQuery()
        controller.reopenQuery(savedDocument)
    }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Overline { title: qsTr("Joined detail query"); color: Tk.Theme.color.text; wide: true }
            Tk.Caption {
                text: qsTr("Typed left join · projected fields · category sort · versioned save and reopen")
            }
        }
        Tk.QueryBuilder {
            id: builder
            objectName: "joinedQueryBuilder"
            width: parent.width
            controller: controller
            Tk.Flex.shrink: 0
        }
        Tk.PivotView {
            objectName: "joinedDetailView"
            controller: controller
            rowHeaderWidth: Tk.Theme.size.panelMinWidth * 0.85
            tooltip: qsTr("Joined detail rows with source references")
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
        }
    }
}
