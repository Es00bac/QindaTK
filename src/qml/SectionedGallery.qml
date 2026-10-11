// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// D-338: slide-sorter sections with host-rendered previews and host-owned
// collapse/selection. Collapsing never changes selection or removes entries.
T.Control {
    id: control
    property var sections: [] // [{id,label,items:[{id,label,...}]}]
    property var collapsedIds: []
    property string currentId: ""
    property Component preview: null
    property real tileWidth: Tk.Theme.size.thumbnail
    property real tileHeight: Tk.Theme.size.thumbnailHeight + Tk.Theme.size.row
    property string tooltip: qsTr("Sections")
    signal sectionToggled(string id, bool collapsed)
    signal activated(string id)
    signal highlighted(string id)
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: contentItem.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Repeater {
            model: control.sections
            delegate: Tk.Flex {
                id: section
                required property var modelData
                readonly property string sectionId: String(modelData.id)
                readonly property bool collapsed: control.collapsedIds.indexOf(sectionId) >= 0
                width: parent.width
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Button {
                    objectName: "gallerySection_" + section.sectionId
                    text: String(section.modelData.label || section.sectionId) + " (" + (section.modelData.items || []).length + ")"
                    iconName: section.collapsed ? "chevron-right" : "chevron-down"
                    variant: "ghost"
                    width: parent.width
                    tooltip: section.collapsed ? qsTr("Expand section") : qsTr("Collapse section")
                    onClicked: control.sectionToggled(section.sectionId, !section.collapsed)
                }
                Tk.Gallery {
                    id: sectionGallery
                    objectName: "gallerySectionItems_" + section.sectionId
                    visible: !section.collapsed
                    width: parent.width
                    model: section.modelData.items || []
                    currentId: control.currentId
                    preview: control.preview
                    tileWidth: control.tileWidth
                    tileHeight: control.tileHeight
                    rows: Math.max(1, Math.ceil(sectionGallery.model.length / Math.max(1, Math.floor((sectionGallery.width - Tk.Theme.size.control) / sectionGallery.tileWidth))))
                    onActivated: id => control.activated(id)
                    onHighlighted: id => control.highlighted(id)
                }
            }
        }
    }
}
