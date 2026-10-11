// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// D-338: a filter checklist. Hosts own checkedIds; every user edit proposes
// the complete set. Select All affects only currently visible enabled rows.
T.Control {
    id: control
    property var items: [] // [{id,label,disabled?}]
    property var checkedIds: []
    property string searchText: ""
    property string tooltip: qsTr("Filter values")
    readonly property var visibleItems: items.filter(entry => String(entry.label || entry.id).toLowerCase().indexOf(searchText.toLowerCase()) >= 0)
    readonly property var selectableItems: visibleItems.filter(entry => entry.disabled !== true)
    readonly property bool allSelected: selectableItems.length > 0 && selectableItems.every(entry => checkedIds.indexOf(String(entry.id)) >= 0)
    signal checkedIdsEdited(var ids)
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    function setChecked(id, checked) {
        const next = checkedIds.filter(value => String(value) !== String(id))
        if (checked) next.push(String(id))
        checkedIdsEdited(next)
    }
    function selectVisible(checked) {
        const visible = selectableItems.map(entry => String(entry.id))
        const next = checkedIds.filter(id => visible.indexOf(String(id)) < 0)
        checkedIdsEdited(checked ? next.concat(visible) : next)
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.SearchField {
            objectName: "checklistSearch"
            width: parent.width
            text: control.searchText
            tooltip: qsTr("Search values")
            onTextEdited: control.searchText = text
            Keys.onDownPressed: list.forceActiveFocus()
        }
        Tk.CheckBox {
            objectName: "checklistAll"
            text: qsTr("Select all shown")
            checked: control.allSelected
            enabled: control.selectableItems.length > 0
            onToggled: control.selectVisible(checked)
        }
        ListView {
            id: list
            objectName: "checklistItems"
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            clip: true
            model: control.visibleItems
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            keyNavigationEnabled: true
            Keys.onSpacePressed: {
                const entry = control.visibleItems[currentIndex]
                if (entry && !entry.disabled) control.setChecked(String(entry.id), control.checkedIds.indexOf(String(entry.id)) < 0)
            }
            delegate: Tk.CheckBox {
                required property var modelData
                objectName: "checklistItem_" + String(modelData.id)
                width: list.width
                text: String(modelData.label || modelData.id)
                checked: control.checkedIds.indexOf(String(modelData.id)) >= 0
                enabled: modelData.disabled !== true
                onToggled: control.setChecked(String(modelData.id), checked)
            }
        }
    }
}
