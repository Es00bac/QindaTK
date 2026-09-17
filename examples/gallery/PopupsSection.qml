// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Menus and popups"
    note: "Menu · MenuBar · Popover · Dialog · CommandPalette · ToolTip"

    GalleryRow {
        label: "MenuBar"
        Tk.MenuBar {
            implicitWidth: 400
            Tk.Menu {
                title: "Project"
                Tk.MenuItem { text: "New"; shortcut: "Ctrl+N"; iconName: "file-plus" }
                Tk.MenuItem { text: "Open…"; shortcut: "Ctrl+O"; iconName: "folder-open" }
                Tk.MenuSeparator {}
                Tk.MenuItem { text: "Quit"; shortcut: "Ctrl+Q"; danger: true }
            }
            Tk.Menu {
                title: "View"
                Tk.MenuItem { text: "Rulers"; checkable: true; checked: true }
                Tk.MenuItem { text: "Guides"; checkable: true }
                Tk.Menu { title: "Panels"
                    Tk.MenuItem { text: "Layers"; checkable: true; checked: true }
                    Tk.MenuItem { text: "History"; checkable: true }
                }
            }
            Tk.Menu { title: "Help"; Tk.MenuItem { text: "Shortcuts"; shortcut: "?" } }
        }
    }
    GalleryRow {
        label: "Open a popup"
        Tk.Button {
            id: menuButton
            text: "Context menu"; iconName: "ellipsis"
            onClicked: contextMenu.popup()
            Tk.Menu {
                id: contextMenu
                Tk.MenuItem { text: "Cut"; shortcut: "Ctrl+X"; iconName: "scissors" }
                Tk.MenuItem { text: "Copy"; shortcut: "Ctrl+C"; iconName: "copy" }
                Tk.MenuItem { text: "Paste"; shortcut: "Ctrl+V"; iconName: "clipboard"; enabled: false }
                Tk.MenuSeparator {}
                Tk.MenuItem { text: "Delete"; shortcut: "Del"; danger: true }
            }
        }
        Tk.Button {
            id: popoverButton
            text: "Popover"; iconName: "info"
            onClicked: popover.open()
            Tk.Popover {
                id: popover
                anchorItem: popoverButton
                placement: "bottom"
                Tk.Flex { direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                    Tk.Overline { title: "Export target" }
                    Tk.ComboBox { model: ["PDF/X-4", "PDF/X-1a", "EPUB"]; currentIndex: 0; implicitWidth: 180 }
                    Tk.CheckBox { text: "Include bleed"; checked: true }
                }
            }
        }
        Tk.Button {
            text: "Dialog"; iconName: "square-check"
            onClicked: dialog.open()
        }
        Tk.Button {
            text: "Command palette"; iconName: "command"
            onClicked: palette.open()
        }
        Tk.Button {
            text: "Hover for a tooltip"; variant: "outline"; tooltip: "Tooltips are Tk.ToolTip children"
        }
    }
    GalleryRow {
        label: "Result"
        Tk.Caption { id: result; text: "nothing activated yet" }
    }

    Tk.Dialog {
        id: dialog
        title: "Close document?"
        subtitle: "page-01.sloom has unsaved changes."
        primaryText: "Save and close"
        secondaryText: "Cancel"
        Tk.Label { text: "Saving writes the project back to its .sloom file."; wrapMode: Text.WordWrap }
        onAccepted: result.text = "dialog accepted"
        onRejected: result.text = "dialog rejected"
    }
    Tk.CommandPalette {
        id: palette
        commands: [
            { "id": "edit.undo", "label": "Undo", "shortcut": "Ctrl+Z", "section": "Edit", "iconName": "undo-2" },
            { "id": "edit.redo", "label": "Redo", "shortcut": "Ctrl+Shift+Z", "section": "Edit", "iconName": "redo-2" },
            { "id": "view.zoomFit", "label": "Zoom to fit", "shortcut": "Ctrl+0", "section": "View", "iconName": "maximize" },
            { "id": "view.panels.layers", "label": "Toggle Layers panel", "section": "View", "iconName": "layers", "keywords": "dock" },
            { "id": "render.queue", "label": "Add to render queue", "section": "Render", "iconName": "film" }
        ]
        onActivated: function(id) { result.text = "palette: " + id }
    }
}
