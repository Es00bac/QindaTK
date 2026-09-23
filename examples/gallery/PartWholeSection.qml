// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Part-to-whole charts"
    note: "PartWholeView · known totals · hierarchy · missing values"
    Tk.PartWholeModel {
        id: allocation
        unit: "hours"
    }
    Component.onCompleted: allocation.replaceNodes([
        {id: "all", label: "All work", value: 100},
        {id: "design", parentId: "all", label: "Design", value: 25},
        {id: "design-ui", parentId: "design", label: "UI", value: 15},
        {id: "design-research", parentId: "design", label: "Research", value: 10},
        {id: "engineering", parentId: "all", label: "Engineering", value: 55},
        {id: "engineering-client", parentId: "engineering", label: "Client", value: 25},
        {id: "engineering-core", parentId: "engineering", label: "Core", value: 30},
        {id: "qa", parentId: "all", label: "QA", value: 20}
    ])
    Tk.PartWholeView {
        objectName: "galleryPartWhole"
        width: 940
        height: 250
        Tk.Flex.shrink: 0
        model: allocation
        mode: Tk.PartWholeRenderer.Sunburst
        tooltip: qsTr("Gallery work allocation")
    }
}
