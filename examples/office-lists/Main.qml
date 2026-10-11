// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk
Rectangle {
    id: root
    width: 900; height: 900
    color: Tk.Theme.color.bg
    Tk.Scroll {
        anchors.fill: parent
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Heading { text: "Filter and field controls"; level: 1 }
            Tk.SearchChecklist {
                width: parent.width; height: Tk.Theme.size.panelMinHeight * 2
                items: [{id:"north",label:"North"},{id:"south",label:"South"},{id:"west",label:"West"}]
                checkedIds: ["north","west"]
                onCheckedIdsEdited: ids => checkedIds=ids
            }
            Tk.FieldWells {
                width: parent.width
                fields: [{id:"region",label:"Region"},{id:"amount",label:"Amount"},{id:"date",label:"Date"}]
                selectedFieldId: "region"
            }
            Tk.SectionedGallery {
                width: parent.width
                sections: [{id:"intro",label:"Introduction",items:[{id:"one",label:"Title"},{id:"two",label:"Agenda"}]},
                           {id:"appendix",label:"Appendix",items:[{id:"three",label:"Sources"}]}]
                collapsedIds: ["appendix"]
                currentId: "one"
                onSectionToggled: (id, collapsed) => collapsedIds = collapsed ? collapsedIds.concat([id]) : collapsedIds.filter(value => value!==id)
                preview: Component { Rectangle {
                    property var entry: ({})
                    property bool selected: false
                    color: selected ? Tk.Theme.color.accentSubtle : Tk.Theme.color.surface
                    Tk.Label { anchors.centerIn: parent; text: parent.entry.label }
                } }
            }
        }
    }
}
