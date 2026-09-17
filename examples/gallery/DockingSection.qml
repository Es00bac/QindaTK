// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Docking"
    note: "DockHost · DockPanel · DockModel — drag headers, seams and tabs"

    Tk.Box {
        color: Tk.Theme.color.bg
        borderWidth: 1
        borderColor: Tk.Theme.color.divider
        implicitHeight: 300

        Tk.DockHost {
            id: dock
            objectName: "galleryDock"
            workspace: "gallery"

            canvas: Tk.Box {
                color: Tk.Theme.color.canvas
                borderWidth: 1
                fill: false
                Tk.Caption { anchors.centerIn: parent; text: "canvas" }
                Tk.Island {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Tk.Theme.space.sm
                    Tk.Button { text: "Float tools"; small: true; variant: "ghost"; onClicked: dock.floatPanel("tools") }
                    Tk.Button { text: "Reset"; small: true; variant: "ghost"; onClicked: dock.resetLayout() }
                }
            }
            Tk.DockPanel {
                panelId: "tools"; title: "Tools"; iconName: "wrench"; zone: "left"; extent: 160; minWidth: 120
                Tk.Flex { direction: Tk.Flex.Column; gap: 1
                    Tk.ListRow { text: "Select"; iconName: "mouse-pointer-2"; small: true; selected: true }
                    Tk.ListRow { text: "Brush"; iconName: "brush"; small: true }
                    Tk.ListRow { text: "Eraser"; iconName: "eraser"; small: true }
                    Tk.ListRow { text: "Text"; iconName: "type"; small: true }
                }
            }
            Tk.DockPanel {
                panelId: "layers"; title: "Layers"; iconName: "layers"; zone: "right"; extent: 200; order: 0
                group: "stack"; groupActive: true
                Tk.Flex { direction: Tk.Flex.Column; gap: 1
                    Tk.ListRow { text: "Lettering"; iconName: "eye"; small: true }
                    Tk.ListRow { text: "Line art"; iconName: "eye"; small: true; selected: true }
                }
            }
            Tk.DockPanel {
                panelId: "channels"; title: "Channels"; iconName: "blend"; zone: "right"; order: 1
                group: "stack"; groupActive: false
                Tk.Label { text: "RGB · R · G · B"; muted: true }
            }
            Tk.DockPanel {
                panelId: "history"; title: "History"; iconName: "history"; zone: "bottom"; extent: 90; minHeight: 60
                Tk.Flex { direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
                    Tk.Chip { text: "Import"; small: true }
                    Tk.Chip { text: "Add clip"; small: true }
                    Tk.Chip { text: "Trim"; small: true; active: true }
                }
            }
        }
    }
}
