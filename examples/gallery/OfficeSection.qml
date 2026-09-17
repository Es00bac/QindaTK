// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Document app parts"
    note: "Viewport · ColorButton · ColorSwatches · MessageDialog · PromptDialog · TreeRow editable · ToolBar wrap · MenuItem radio"

    readonly property var swatchList: [
        { name: "Black", color: "#000000" }, { name: "Grey", color: "#7f7f7f" },
        { name: "Blue", color: "#1c62d4" }, { name: "Red", color: "#d21c1c" },
        { name: "Green", color: "#1f9d3a" }, { name: "Orange", color: "#f28c19" },
        { name: "Purple", color: "#7d3ac1" }, { name: "Brown", color: "#8d5a2b" }
    ]

    GalleryRow {
        label: "Wrapping ToolBar"
        Item {
            implicitWidth: 320
            implicitHeight: wrapBar.implicitHeight
            Tk.ToolBar {
                id: wrapBar
                wrap: true
                compact: true
                anchors.left: parent.left
                anchors.right: parent.right
                Repeater {
                    model: ["file-plus", "folder-open", "save", "undo-2", "redo-2", "scissors", "copy", "clipboard-paste", "search", "zoom-out", "zoom-in", "printer", "bold", "italic", "underline"]
                    Tk.IconButton { required property string modelData; iconName: modelData; tooltip: modelData }
                }
            }
        }
    }
    GalleryRow {
        label: "Colour"
        Tk.ColorButton { id: penColor; color: "#1c62d4"; colors: section.swatchList; tooltip: "Pen colour" }
        Tk.ColorButton { color: "#ffd200"; small: true; colors: ["#ffd200", "#7ee787", "#ffa7c4", "#7ec8ff", "#ffbe6e"]; columns: 5; tooltip: "Highlighter colour" }
        Tk.ColorSwatches { colors: section.swatchList; current: penColor.color; onSelected: function(c) { penColor.color = c } }
    }
    GalleryRow {
        label: "TreeRow editable"
        Tk.Flex {
            direction: Tk.Flex.Column
            Tk.Flex.basis: 240
            Tk.TreeRow { text: "Notes"; expandable: true; expanded: true }
            Tk.TreeRow { id: renamable; text: "Page 1"; depth: 1; editable: true; onRenamed: function(t) { renamable.text = t } }
            Tk.TreeRow { text: "Page 2"; depth: 1; editable: true; selected: true }
        }
        Tk.Button { text: "Rename Page 1"; small: true; variant: "outline"; onClicked: renamable.startEditing() }
    }
    GalleryRow {
        label: "Radio items"
        Tk.Button {
            text: "Ink menu"; iconName: "pen-tool"
            onClicked: inkMenu.popup()
            Tk.Menu {
                id: inkMenu
                property string tool: "pen"
                Tk.MenuItem { text: "Pen"; shortcut: "P"; checkable: true; radio: true; checked: inkMenu.tool === "pen"; onTriggered: inkMenu.tool = "pen" }
                Tk.MenuItem { text: "Highlighter"; shortcut: "H"; checkable: true; radio: true; checked: inkMenu.tool === "highlighter"; onTriggered: inkMenu.tool = "highlighter" }
                Tk.MenuItem { text: "Eraser"; shortcut: "E"; checkable: true; radio: true; checked: inkMenu.tool === "eraser"; onTriggered: inkMenu.tool = "eraser" }
            }
        }
    }
    GalleryRow {
        label: "Dialogs"
        Tk.Button { text: "Save / Discard / Cancel"; onClicked: consent.open() }
        Tk.Button { text: "Prompt"; onClicked: prompt.open() }
        Tk.Button { text: "Danger"; variant: "danger"; onClicked: danger.open() }
        Tk.Caption { id: officeResult; text: "nothing decided yet" }
    }
    GalleryRow {
        label: "Viewport"
        Item {
            implicitWidth: 360
            implicitHeight: 200
            Tk.Viewport {
                id: viewport
                anchors.fill: parent
                fitMode: "width"
                Rectangle {
                    implicitWidth: 300; implicitHeight: 420
                    color: "white"; border.color: "#c8c8d2"
                    Tk.Caption { x: 16; y: 16; text: "A page in a Viewport"; color: "black" }
                }
            }
            Tk.Island {
                anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: Tk.Theme.space.xs
                Tk.ZoomControl { value: viewport.zoom; onValueModified: function(v) { viewport.setZoom(v) } }
            }
        }
    }

    Tk.MessageDialog {
        id: consent
        title: "Unsaved changes"; variant: "question"
        text: "Save changes to Meeting notes before closing?"
        primaryText: "Save"; secondaryText: "Cancel"; tertiaryText: "Discard"
        onAccepted: officeResult.text = "saved"
        onRejected: officeResult.text = "cancelled"
        onTertiary: officeResult.text = "discarded"
    }
    Tk.PromptDialog {
        id: prompt
        title: "Rename page"; label: "Page title"; text: "Agenda"
        onAccepted: officeResult.text = "renamed to " + text
    }
    Tk.MessageDialog {
        id: danger
        title: "Delete page"; variant: "danger"; destructive: true
        text: "Delete “Agenda” and its ink? This cannot be undone."
        primaryText: "Delete"
        onAccepted: officeResult.text = "deleted"
    }
}
