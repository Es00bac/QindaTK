// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 960
    height: 640

    Component.onCompleted: {
        source.setRowIds(["source-east", "source-west"])
        source.setColumnIds(["region", "amount"])
        source.beginEdit()
        source.stageCell("source-east", "region", "East")
        source.stageCell("source-east", "amount", 10)
        source.stageCell("source-west", "region", "West")
        source.stageCell("source-west", "amount", 5)
        source.commitEdit()
        query.query = ({ version: 1, id: "installed-query", schemaId: "consumer-sales",
            schemaRevision: "1", sourceRevision: source.revision,
            filter: { kind: "constant", value: true }, parameters: {},
            rows: ["region"], columns: [],
            measures: [{ id: "sum", field: "amount", function: "sum" }] })
        query.refresh()
    }

    Tk.EditableGridModel {
        id: source
        objectName: "installedSourceGrid"
    }

    Tk.QueryController {
        id: query
        objectName: "installedQueryController"
        source: source
        schema: ({ id: "consumer-sales", revision: "1", fields: [
            { id: "region", type: "text", nullable: false },
            { id: "amount", type: "number", nullable: true }
        ] })
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.VirtualList {
                objectName: "installedVirtualList"
                width: Tk.Theme.size.panelMinWidth
                tooltip: qsTr("Installed list")
            }
            Tk.VirtualTree {
                objectName: "installedVirtualTree"
                width: Tk.Theme.size.panelMinWidth
                tooltip: qsTr("Installed tree")
            }
            Tk.AssetGrid {
                objectName: "installedAssetGrid"
                Tk.Flex.grow: 1
                tooltip: qsTr("Installed assets")
            }
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.EditableGrid {
                objectName: "installedEditableGrid"
                model: source
                Tk.Flex.grow: 1
                tooltip: qsTr("Installed grid")
            }
            Tk.PivotView {
                objectName: "installedPivotView"
                controller: query
                Tk.Flex.grow: 1
                tooltip: qsTr("Installed pivot")
            }
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.FilterBar {
                objectName: "installedFilterBar"
                Tk.Flex.grow: 1
                fields: [{ id: "region", type: "text", label: "Region" }]
            }
            Tk.QueryBuilder {
                objectName: "installedQueryBuilder"
                Tk.Flex.grow: 1
                controller: query
            }
        }
    }
}
