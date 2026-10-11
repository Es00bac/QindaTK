// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Flex {
    property string filter: ""
    readonly property bool matches: !filter.length || "border shading preview checklist filter field sections".indexOf(filter.toLowerCase()) >= 0
    visible: matches
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    Tk.Heading { text: qsTr("Borders and shading"); level: 2 }
    Tk.BorderEditor {
        width: parent.width
        borders: ({ top: {style: "double", color: "#447799", width: 1},
                    bottom: {style: "solid", color: "#447799", width: 2} })
        onBordersEdited: next => borders = next
    }
    Tk.SearchChecklist {
        width: parent.width; height: Tk.Theme.size.panelMinHeight * 2
        items: [{id:"one",label:"First value"},{id:"two",label:"Second value"}]
        onCheckedIdsEdited: ids => checkedIds=ids
    }
    Tk.FieldWells { width: parent.width; fields: [{id:"region",label:"Region"},{id:"amount",label:"Amount"}] }
    Tk.SectionedGallery {
        width: parent.width
        sections: [{id:"intro",label:"Introduction",items:[{id:"one",label:"Title slide"}]}]
        onSectionToggled: (id, collapsed) => collapsedIds = collapsed ? [id] : []
    }
}
