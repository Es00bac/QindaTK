// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Sloom Studio's Video workspace rebuilt from QindaTK: workspace chips and
// project field, a menu bar whose View menu is driven by the dock host,
// the dock host (source bin left, two monitors in the centre zone above
// the timeline canvas, inspector and clip panels right), and a status bar.
// Run:
//   qtk-preview examples/studio-shell/Main.qml
//   qtk-preview examples/studio-shell/Main.qml --qst qinda-dusk --grab studio.png
Rectangle {
    id: root
    width: 1280
    height: 800
    color: Tk.Theme.color.bg

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column

        // ---- workspace row ----
        Tk.Box {
            objectName: "workspaceBar"
            color: Tk.Theme.color.surface
            borderBottom: 1
            borderColor: Tk.Theme.color.divider
            implicitHeight: Tk.Theme.size.toolbar + Tk.Theme.space.sm
            Tk.Flex.shrink: 0

            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                paddingLeft: Tk.Theme.space.md
                paddingRight: Tk.Theme.space.md

                Tk.Overline { title: "Sloom"; wide: true; color: Tk.Theme.color.accent }
                Tk.Spacer { size: Tk.Theme.space.sm }
                Tk.Chip { text: "Flow"; iconName: "workflow" }
                Tk.Chip { text: "Paper"; iconName: "file-text" }
                Tk.Chip { text: "Image"; iconName: "image" }
                Tk.Chip { text: "Video"; iconName: "film"; active: true }
                Tk.Spacer { size: Tk.Theme.space.md }
                Tk.TextField {
                    objectName: "projectName"
                    text: "composition-04982a"
                    iconName: "folder-open"
                    implicitWidth: 220
                }
                Tk.Chip { text: "Source Library · 3 assets"; iconName: "database"; small: true }
                Tk.Button { text: "Undo"; iconName: "undo-2"; variant: "ghost"; small: true }
                Tk.Button { text: "Redo"; iconName: "redo-2"; variant: "ghost"; small: true }
                Tk.Spacer {}
                Tk.Island {
                    Tk.ZoomControl { value: 1.0 }
                    Tk.Divider { vertical: true; inset: 4 }
                    Tk.Chip { text: "Fit"; rounded: true; small: true }
                }
                Tk.Badge { text: "$0.94"; variant: "success" }
                Tk.IconButton { iconName: "settings"; tooltip: "Settings" }
                Tk.IconButton { iconName: "circle-question-mark"; tooltip: "Help" }
            }
        }

        // ---- menu bar ----
        Tk.MenuBar {
            objectName: "studioMenuBar"
            Tk.Flex.shrink: 0
            Tk.Menu {
                title: "Project"
                Tk.MenuItem { text: "New project"; shortcut: "Ctrl+N"; iconName: "file-plus" }
                Tk.MenuItem { text: "Open…"; shortcut: "Ctrl+O"; iconName: "folder-open" }
                Tk.MenuItem { text: "Save"; shortcut: "Ctrl+S"; iconName: "save" }
                Tk.MenuSeparator {}
                Tk.MenuItem { text: "Quit"; shortcut: "Ctrl+Q" }
            }
            Tk.Menu {
                title: "Edit"
                Tk.MenuItem { text: "Undo"; shortcut: "Ctrl+Z"; iconName: "undo-2" }
                Tk.MenuItem { text: "Redo"; shortcut: "Ctrl+Shift+Z"; iconName: "redo-2" }
                Tk.MenuSeparator {}
                Tk.MenuItem { text: "Cut clip"; shortcut: "C"; iconName: "scissors" }
                Tk.MenuItem { text: "Delete clip"; shortcut: "Del"; danger: true }
            }
            Tk.Menu {
                title: "Timeline"
                Tk.MenuItem { text: "Snap to clips"; checkable: true; checked: true }
                Tk.MenuItem { text: "Add keyframe"; shortcut: "K"; iconName: "diamond" }
                Tk.MenuItem { text: "Zoom to fit"; shortcut: "Shift+Z" }
            }
            Tk.Menu {
                id: viewMenu
                title: "View"
                Instantiator {
                    model: dock.panelMenuModel
                    delegate: Tk.MenuItem {
                        required property var modelData
                        text: modelData.title
                        checkable: true
                        checked: !modelData.hidden
                        onTriggered: dock.togglePanel(modelData.panelId)
                    }
                    onObjectAdded: function(index, object) { viewMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { viewMenu.removeItem(object) }
                }
                Tk.MenuSeparator {}
                Tk.MenuItem { text: "Reset layout"; iconName: "rotate-ccw"; onTriggered: dock.resetLayout() }
            }
            Tk.Menu {
                title: "Window"
                Tk.MenuItem { text: "Command palette"; shortcut: "Ctrl+K"; iconName: "command" }
                Tk.MenuItem { text: "Full screen"; shortcut: "F11" }
            }
            Tk.Menu {
                title: "Help"
                Tk.MenuItem { text: "User guide"; iconName: "circle-question-mark" }
                Tk.MenuItem { text: "Keyboard shortcuts"; shortcut: "?" }
            }
        }

        // ---- workspace ----
        Tk.DockHost {
            id: dock
            objectName: "videoDock"
            workspace: "video"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0

            canvas: TimelineCanvas {
                objectName: "timeline"
                playheadSeconds: 0.9
            }

            Tk.DockPanel {
                panelId: "sourceBin"; title: "Project Source Bin"; iconName: "folder"
                zone: "left"; extent: 250; minWidth: 200
                SourceBinPanel {}
            }
            Tk.DockPanel {
                panelId: "sourceMonitor"; title: "Source Monitor"; iconName: "monitor"
                zone: "center"; order: 0; extent: 330; minHeight: 200
                MonitorPanel { program: false }
            }
            Tk.DockPanel {
                panelId: "programMonitor"; title: "Program Monitor"; iconName: "monitor"
                zone: "center"; order: 1; extent: 330; minHeight: 200
                MonitorPanel { program: true; timecode: "0:00 / 0:12" }
            }
            Tk.DockPanel {
                panelId: "inspector"; title: "Inspector"; iconName: "sliders-horizontal"
                zone: "right"; order: 0; extent: 270; share: 1; minWidth: 220
                InspectorPanel {}
            }
            Tk.DockPanel {
                panelId: "clip"; title: "Selected clip"; iconName: "film"
                zone: "right"; order: 1; share: 1; minWidth: 220
                ClipPanel {}
            }
        }

        // ---- status bar ----
        Tk.StatusBar {
            objectName: "studioStatus"
            Tk.Flex.shrink: 0
            Tk.StatusField { text: "00:00 / 00:12"; iconName: "clock" }
            Tk.StatusField { text: "2 clips · V1 V2 A1" }
            Tk.StatusField { text: "Preview ready"; iconName: "circle-check"; muted: false }
            Tk.Spacer {}
            Tk.StatusField { text: dock.hiddenPanels.length > 0 ? dock.hiddenPanels.length + " hidden panels" : "all panels shown" }
            Tk.StatusField { text: "185%"; iconName: "zoom-in" }
        }
    }
}
