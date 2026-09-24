// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Shape definitions and factories belong to the document host. This palette
// only requests insertion by stable definition ID and passes default params.
Item {
    id: control
    property var definitions: [] // [{id, label, category, iconName, parameters, enabled, description}]
    property var recentIds: []
    property string searchText: ""
    property string category: "all"
    property string tooltip: qsTr("Choose a shape to insert into the document")
    property int columns: 3
    readonly property var categories: collectCategories()
    readonly property var visibleDefinitions: filterDefinitions()

    signal shapeRequested(string shapeId, var parameters)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: content.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function collectCategories() {
        const result = ["all"]
        for (let i = 0; i < definitions.length; ++i) {
            const value = definitions[i].category || "Shapes"
            if (result.indexOf(value) < 0) result.push(value)
        }
        return result
    }
    function filterDefinitions() {
        const query = searchText.trim().toLocaleLowerCase()
        return definitions.filter(function(shape) {
            const categoryMatch = control.category === "all" || (shape.category || "Shapes") === control.category
            const text = (shape.label || shape.id || "").toLocaleLowerCase()
            const description = (shape.description || "").toLocaleLowerCase()
            return categoryMatch && (!query || text.indexOf(query) >= 0 || description.indexOf(query) >= 0)
        })
    }
    function requestInsert(shape) {
        if (!enabled || !shape || shape.enabled === false) return
        control.shapeRequested(shape.id, Object.assign({}, shape.parameters || {}))
        const updated = [shape.id].concat(control.recentIds.filter(function(id) { return id !== shape.id })).slice(0, 8)
        control.recentIds = updated
    }

    Tk.Flex {
        id: content
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Overline { title: qsTr("SHAPES") }
        Tk.TextField {
            objectName: "shapeSearch"
            placeholderText: qsTr("Search shapes")
            iconName: "search"
            tooltip: qsTr("Filter shape definitions")
            onTextChanged: if (activeFocus) control.searchText = text
        }
        Tk.Segmented {
            objectName: "shapeCategories"
            model: control.categories
            currentIndex: Math.max(0, control.categories.indexOf(control.category))
            tooltip: qsTr("Shape category")
            onActivated: function(index) { control.category = control.categories[index] }
        }
        Tk.Caption {
            visible: control.recentIds.length > 0
            text: qsTr("Recent: %1").arg(control.recentIds.map(function(id) {
                for (let i = 0; i < control.definitions.length; ++i)
                    if (control.definitions[i].id === id) return control.definitions[i].label
                return id
            }).join(" · "))
            elide: Text.ElideRight
        }
        Tk.Grid {
            objectName: "shapeGrid"
            columns: "repeat(" + Math.max(1, control.columns) + ", 1fr)"
            gap: Tk.Theme.space.xs
            Repeater {
                model: control.visibleDefinitions
                delegate: Tk.Button {
                    required property var modelData
                    objectName: "shape_" + modelData.id
                    text: modelData.label
                    iconName: modelData.iconName || "shapes"
                    tooltip: modelData.description || modelData.label
                    enabled: modelData.enabled !== false
                    variant: "outline"
                    onClicked: control.requestInsert(modelData)
                }
            }
        }
        Tk.Caption {
            visible: control.visibleDefinitions.length === 0
            text: qsTr("No shapes match this search.")
        }
    }
}
