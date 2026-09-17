// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A document application's window built from QindaTK: menu bar with radio
// items, a wrapping tool strip with colour buttons, a zoomable page in a
// Viewport with a zoom island, a find bar, a status bar, and the three
// dialogs an editor needs (Save/Discard/Cancel, rename, warning). Run:
//   qtk-preview examples/office-shell/Main.qml
//   qtk-preview examples/office-shell/Main.qml --dump --size 520x700
Tk.AppWindow {
    id: window
    width: 1100
    height: 760
    title: "Meeting notes — Notes"

    readonly property var penColors: [
        { name: "Black", color: "#000000" }, { name: "Grey", color: "#7f7f7f" },
        { name: "Blue", color: "#1c62d4" }, { name: "Red", color: "#d21c1c" },
        { name: "Green", color: "#1f9d3a" }, { name: "Orange", color: "#f28c19" },
        { name: "Purple", color: "#7d3ac1" }, { name: "Brown", color: "#8d5a2b" }
    ]
    property string tool: "pen"
    property color penColor: "#1c62d4"
    property string status: "Opened Meeting notes"

    menuBar: Tk.MenuBar {
        objectName: "menu"
        Tk.Menu {
            title: "File"
            Tk.MenuItem { text: "New"; shortcut: "Ctrl+N"; iconName: "file-plus" }
            Tk.MenuItem { text: "Open…"; shortcut: "Ctrl+O"; iconName: "folder-open" }
            Tk.Menu { title: "Open Recent"; Tk.MenuItem { text: "Meeting notes" } Tk.MenuItem { text: "Research" } }
            Tk.MenuSeparator {}
            Tk.MenuItem { text: "Save"; shortcut: "Ctrl+S"; iconName: "save" }
            Tk.MenuItem { text: "Print…"; shortcut: "Ctrl+P"; iconName: "printer" }
            Tk.MenuSeparator {}
            Tk.MenuItem { text: "Close Window"; shortcut: "Ctrl+W"; onTriggered: consent.open() }
        }
        Tk.Menu {
            title: "Edit"
            Tk.MenuItem { text: "Undo"; shortcut: "Ctrl+Z"; iconName: "undo-2" }
            Tk.MenuItem { text: "Redo"; shortcut: "Ctrl+Y"; iconName: "redo-2"; enabled: false }
            Tk.MenuSeparator {}
            Tk.MenuItem { text: "Find…"; shortcut: "Ctrl+F"; onTriggered: findBar.visible = true }
        }
        Tk.Menu {
            title: "Ink"
            Tk.MenuItem { text: "Pen"; shortcut: "P"; checkable: true; radio: true; checked: window.tool === "pen"; onTriggered: window.tool = "pen" }
            Tk.MenuItem { text: "Highlighter"; shortcut: "H"; checkable: true; radio: true; checked: window.tool === "highlighter"; onTriggered: window.tool = "highlighter" }
            Tk.MenuItem { text: "Eraser"; shortcut: "E"; checkable: true; radio: true; checked: window.tool === "eraser"; onTriggered: window.tool = "eraser" }
            Tk.MenuSeparator {}
            Tk.Menu {
                title: "Pen Colour"
                Repeater {
                    model: window.penColors
                    Tk.MenuItem {
                        required property var modelData
                        text: modelData.name
                        checkable: true
                        radio: true
                        checked: Qt.colorEqual(window.penColor, modelData.color)
                        onTriggered: window.penColor = modelData.color
                    }
                }
            }
        }
        Tk.Menu {
            title: "Page"
            Tk.MenuItem { text: "Rename Page…"; shortcut: "F2"; onTriggered: rename.open() }
            Tk.MenuItem { text: "Delete Page"; danger: true; onTriggered: warning.open() }
        }
    }

    toolBars: [
        Tk.ToolBar {
            objectName: "mainBar"
            wrap: true
            Tk.IconButton { iconName: "file-plus"; tooltip: "New" }
            Tk.IconButton { iconName: "folder-open"; tooltip: "Open" }
            Tk.IconButton { iconName: "save"; tooltip: "Save" }
            Tk.ToolSeparator {}
            Tk.IconButton { iconName: "undo-2"; tooltip: "Undo" }
            Tk.IconButton { iconName: "redo-2"; tooltip: "Redo"; enabled: false }
            Tk.ToolSeparator {}
            Tk.IconButton { iconName: "scissors"; tooltip: "Cut" }
            Tk.IconButton { iconName: "copy"; tooltip: "Copy" }
            Tk.IconButton { iconName: "clipboard-paste"; tooltip: "Paste" }
            Tk.ToolSeparator {}
            Tk.IconButton { iconName: "search"; tooltip: "Find"; onClicked: findBar.visible = true }
            Tk.IconButton { iconName: "zoom-out"; tooltip: "Zoom out"; onClicked: viewport.zoomOut() }
            Tk.IconButton { iconName: "zoom-in"; tooltip: "Zoom in"; onClicked: viewport.zoomIn() }
        },
        Tk.ToolBar {
            objectName: "inkBar"
            compact: true
            wrap: true
            Tk.IconButton { iconName: "pen-tool"; tooltip: "Pen"; checkable: true; checked: window.tool === "pen"; onClicked: window.tool = "pen" }
            Tk.IconButton { iconName: "highlighter"; tooltip: "Highlighter"; checkable: true; checked: window.tool === "highlighter"; onClicked: window.tool = "highlighter" }
            Tk.IconButton { iconName: "eraser"; tooltip: "Eraser"; checkable: true; checked: window.tool === "eraser"; onClicked: window.tool = "eraser" }
            Tk.IconButton { iconName: "type"; tooltip: "Text"; checkable: true; checked: window.tool === "text"; onClicked: window.tool = "text" }
            Tk.ToolSeparator {}
            Tk.ColorButton { objectName: "penColorButton"; tooltip: "Pen colour"; colors: window.penColors; color: window.penColor; onColorSelected: (c) => window.penColor = c }
            Tk.ToolSeparator {}
            Tk.IconButton { iconName: "square-check"; tooltip: "Add checklist item" }
            Tk.IconButton { iconName: "image"; tooltip: "Add image" }
            Tk.Spacer {}
            Tk.ComboBox { objectName: "background"; model: ["Blank", "Ruled", "Grid", "Dotted"]; implicitWidth: 110; small: true }
        }
    ]

    findBar: Tk.Box {
        id: findBar
        objectName: "findBar"
        visible: false
        color: Tk.Theme.color.surface
        borderTop: 1
        borderColor: Tk.Theme.color.divider
        padding: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: "Find" }
            Tk.TextField { placeholderText: "Search the notebook"; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.IconButton { iconName: "chevron-down"; tooltip: "Next" }
            Tk.IconButton { iconName: "chevron-up"; tooltip: "Previous" }
            Tk.CheckBox { text: "Case" }
            Tk.IconButton { iconName: "x"; tooltip: "Close"; onClicked: findBar.visible = false }
        }
    }

    statusBar: Tk.StatusBar {
        objectName: "status"
        Tk.StatusField { text: window.status }
        Tk.Spacer {}
        Tk.StatusField { text: "Notes — page 1 of 2 · " + window.tool + " · Zoom " + Math.round(viewport.zoom * 100) + " %" }
    }

    Tk.Stack {
        Tk.Viewport {
            id: viewport
            objectName: "viewport"
            fitMode: "width"
            Rectangle {
                objectName: "page"
                implicitWidth: 760
                implicitHeight: 1004
                color: "white"      // paper: the one literal a document app keeps
                border.color: "#c8c8d2"
                Tk.Caption { x: 40; y: 40; text: "Agenda"; color: "black" }
            }
        }
        Tk.Island {
            Tk.Stack.top: Tk.Theme.space.sm
            Tk.Stack.centerX: true
            Tk.ZoomControl { value: viewport.zoom; onValueModified: (v) => viewport.setZoom(v); onResetRequested: viewport.fitMode = "none" }
            Tk.Divider { vertical: true; inset: 6 }
            Tk.IconButton { iconName: "maximize"; tooltip: "Fit width"; onClicked: viewport.fitWidth() }
        }
    }

    Tk.MessageDialog {
        id: consent
        title: "Unsaved changes"
        variant: "question"
        text: "Save changes to Meeting notes before closing?"
        primaryText: "Save"
        secondaryText: "Cancel"
        tertiaryText: "Discard"
        onAccepted: window.status = "Saved"
        onTertiary: window.status = "Discarded"
    }
    Tk.PromptDialog {
        id: rename
        title: "Rename page"
        label: "Page title"
        text: "Agenda"
        onAccepted: window.status = "Renamed to " + text
    }
    Tk.MessageDialog {
        id: warning
        title: "Delete page"
        variant: "danger"
        text: "Delete “Agenda” and its ink? This cannot be undone."
        primaryText: "Delete"
        destructive: true
        onAccepted: window.status = "Deleted"
    }
}
