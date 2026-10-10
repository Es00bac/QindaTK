// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (D-327, docs/controls.md): an office gallery (Styles,
// Themes, Table Styles) — tiles of host-rendered previews. `model` is
// [{id, label, ...}]; `preview` is a Component the host renders each tile
// with (it gets `entry`, the model entry, and `selected`), so the toolkit
// never knows what a style or a theme looks like. The strip shows `rows`
// rows and scrolls by row with its arrows; "More" opens every tile in a
// popover. Hover or keyboard focus emits highlighted(id) (a host may
// preview live), a click, Return or Space emits activated(id); `currentId`
// is the host's state (a selection never writes it).
T.Control {
    id: gallery

    property var model: []
    property Component preview: null
    property string currentId: ""
    property int rows: 1
    property real tileWidth: Tk.Theme.size.thumbnail
    property real tileHeight: Tk.Theme.size.thumbnailHeight + Tk.Theme.size.row
    property string tooltip: qsTr("Gallery")
    readonly property int count: model ? model.length || 0 : 0
    property int focusIndex: -1

    signal activated(string id)
    signal highlighted(string id)

    function entryAt(index) { return index >= 0 && index < count ? model[index] : ({}) }
    function idAt(index) { return String(entryAt(index).id ?? "") }
    function activate(index) {
        if (index < 0 || index >= count || !enabled) return false
        focusIndex = index
        activated(idAt(index))
        return true
    }
    function highlight(index) {
        if (index < 0 || index >= count) return
        focusIndex = index
        highlighted(idAt(index))
    }
    function openMore() { more.open() }

    implicitWidth: tileWidth * 4 + Tk.Theme.size.control + Tk.Theme.space.xs * 6
    implicitHeight: tileHeight * Math.max(1, rows) + Tk.Theme.space.xs * 2
    padding: Tk.Theme.space.xs
    activeFocusOnTab: enabled && count > 0
    Accessible.role: Accessible.List
    Accessible.name: tooltip

    Keys.onPressed: function(event) {
        const step = event.key === Qt.Key_Right ? 1 : event.key === Qt.Key_Left ? -1
                   : event.key === Qt.Key_Down ? strip.perRow : event.key === Qt.Key_Up ? -strip.perRow : 0
        if (step !== 0) {
            gallery.highlight(Math.max(0, Math.min(gallery.count - 1, (gallery.focusIndex < 0 ? 0 : gallery.focusIndex + step))))
            strip.positionViewAtIndex(gallery.focusIndex, GridView.Contain)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            event.accepted = gallery.activate(gallery.focusIndex < 0 ? 0 : gallery.focusIndex)
        }
    }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.width: Tk.Theme.size.border
        border.color: gallery.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }

    // One tile: the host's preview over a caption, selected and focus states.
    component Tile: Rectangle {
        id: tile
        required property var modelData
        required property int index
        readonly property bool selected: String(modelData.id ?? "") === gallery.currentId
        objectName: "galleryTile_" + String(modelData.id ?? index)
        width: gallery.tileWidth
        height: gallery.tileHeight
        radius: Tk.Theme.radius.sm
        color: hover.hovered ? Tk.Theme.color.hover : "transparent"
        border.width: selected || index === gallery.focusIndex ? Tk.Theme.size.focusRing : 0
        border.color: selected ? Tk.Theme.color.accent : Tk.Theme.color.focus
        Accessible.role: Accessible.ListItem
        Accessible.name: String(modelData.label ?? modelData.id ?? "")
        Accessible.selected: selected
        Loader {
            objectName: "galleryPreview"
            x: Tk.Theme.space.xs
            y: Tk.Theme.space.xs
            width: parent.width - Tk.Theme.space.xs * 2
            height: Tk.Theme.size.thumbnailHeight - Tk.Theme.space.xs
            clip: true
            sourceComponent: gallery.preview
            onLoaded: {
                if ("entry" in item) item.entry = Qt.binding(() => tile.modelData)
                if ("selected" in item) item.selected = Qt.binding(() => tile.selected)
            }
        }
        Tk.Caption {
            objectName: "galleryCaption"
            y: Tk.Theme.size.thumbnailHeight
            width: parent.width
            height: Tk.Theme.size.row
            horizontalAlignment: Text.AlignHCenter
            text: String(tile.modelData.label ?? "")
            elide: Text.ElideRight
        }
        HoverHandler { id: hover; onHoveredChanged: if (hovered) gallery.highlight(tile.index) }
        TapHandler { onTapped: { gallery.forceActiveFocus(); gallery.activate(tile.index) } }
    }

    contentItem: Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        GridView {
            id: strip
            objectName: "galleryStrip"
            readonly property int perRow: Math.max(1, Math.floor(width / cellWidth))
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            clip: true
            interactive: false
            model: gallery.model
            cellWidth: gallery.tileWidth
            cellHeight: gallery.tileHeight
            delegate: Tile { }
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: 0
            Tk.Flex.shrink: 0
            Tk.IconButton {
                objectName: "galleryUp"
                small: true
                iconName: "chevron-up"
                tooltip: qsTr("Previous row")
                enabled: strip.contentY > 0
                onClicked: strip.contentY = Math.max(0, strip.contentY - strip.cellHeight)
            }
            Tk.IconButton {
                objectName: "galleryDown"
                small: true
                iconName: "chevron-down"
                tooltip: qsTr("Next row")
                enabled: strip.contentY + strip.height < strip.contentHeight
                onClicked: strip.contentY = Math.min(Math.max(0, strip.contentHeight - strip.height), strip.contentY + strip.cellHeight)
            }
            Tk.IconButton {
                objectName: "galleryMore"
                small: true
                iconName: "chevrons-down"
                tooltip: qsTr("More")
                onClicked: gallery.openMore()
            }
        }
    }

    Tk.Popover {
        id: more
        objectName: "galleryPopover"
        anchorItem: gallery
        placement: "bottom"
        GridView {
            objectName: "galleryAll"
            width: gallery.tileWidth * Math.max(1, Math.min(6, gallery.count))
            height: gallery.tileHeight * Math.max(1, Math.min(4, Math.ceil(gallery.count / 6)))
            implicitWidth: width
            implicitHeight: height
            clip: true
            model: gallery.model
            cellWidth: gallery.tileWidth
            cellHeight: gallery.tileHeight
            boundsBehavior: Flickable.StopAtBounds
            delegate: Tile { }
        }
        Connections {
            target: gallery
            function onActivated() { more.close() }
        }
    }
}
