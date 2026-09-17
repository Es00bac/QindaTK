// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

Section {
    title: "Layout"
    note: "Flex · Grid areas · Stack · Box · Scroll · Splitter"

    component Cell: Tk.Box {
        property string name: ""
        color: Tk.Theme.color.accentSubtle
        borderWidth: 1
        borderColor: Tk.Theme.color.controlBorder
        radius: Tk.Theme.radius.xs
        padding: Tk.Theme.space.xs
        implicitHeight: Tk.Theme.size.control
        Tk.Caption { text: name; color: Tk.Theme.color.accentText; horizontalAlignment: Text.AlignHCenter }
    }

    GalleryRow {
        label: "Flex justify"
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs; padding: Tk.Theme.space.xs
            implicitWidth: 360
            Tk.Flex {
                direction: Tk.Flex.Row; justify: Tk.Flex.SpaceBetween; gap: Tk.Theme.space.xs
                Cell { name: "start"; implicitWidth: 60 }
                Cell { name: "grow 1"; Tk.Flex.grow: 1 }
                Cell { name: "end"; implicitWidth: 60 }
            }
        }
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs; padding: Tk.Theme.space.xs
            implicitWidth: 260
            Tk.Flex {
                direction: Tk.Flex.Row; wrap: Tk.Flex.Wrap; gap: Tk.Theme.space.xs
                Repeater { model: 7; Cell { required property int index; name: "wrap " + (index + 1); implicitWidth: 72 } }
            }
        }
    }
    GalleryRow {
        label: "Grid areas"
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs; padding: Tk.Theme.space.xs
            implicitWidth: 360
            Tk.Grid {
                columns: "96 1fr 72"
                rows: "auto 64 auto"
                areas: ["head head head", "side main tools", "foot foot foot"]
                gap: Tk.Theme.space.xs
                Cell { name: "head"; Tk.Grid.area: "head" }
                Cell { name: "side"; Tk.Grid.area: "side" }
                Cell { name: "main 1fr"; Tk.Grid.area: "main" }
                Cell { name: "tools"; Tk.Grid.area: "tools" }
                Cell { name: "foot"; Tk.Grid.area: "foot" }
            }
        }
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs; padding: Tk.Theme.space.xs
            implicitWidth: 260
            Tk.Grid {
                columns: "repeat(3, 1fr)"; autoFlow: Tk.Grid.RowDense; gap: Tk.Theme.space.xs
                Cell { name: "span 2"; Tk.Grid.columnSpan: 2 }
                Cell { name: "dense" }
                Cell { name: "a" }
                Cell { name: "b" }
                Cell { name: "c" }
            }
        }
    }
    GalleryRow {
        label: "Stack insets"
        Tk.Box {
            color: Tk.Theme.color.canvas; borderWidth: 1; radius: Tk.Theme.radius.xs
            implicitWidth: 360; implicitHeight: 96
            fill: false
            Tk.Stack {
                anchors.fill: parent
                padding: Tk.Theme.space.sm
                Tk.Caption { text: "Stack: fill child"; Tk.Stack.fill: false; Tk.Stack.centerX: true; Tk.Stack.centerY: true }
                Tk.Badge { text: "top-right"; variant: "accent"; Tk.Stack.top: 0; Tk.Stack.right: 0 }
                Tk.Badge { text: "bottom-left"; Tk.Stack.bottom: 0; Tk.Stack.left: 0 }
                Tk.Divider { Tk.Stack.left: 40; Tk.Stack.right: 40; Tk.Stack.bottom: 20 }
            }
        }
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderLeft: 3; borderRight: 0; borderTop: 0; borderBottom: 1
            borderColor: Tk.Theme.color.accent; radius: 0; padding: Tk.Theme.space.sm
            implicitWidth: 260
            Tk.Label { text: "Box with a 3px left edge and a 1px bottom edge"; wrapMode: Text.WordWrap; elide: Text.ElideNone }
        }
    }
    GalleryRow {
        label: "Scroll"
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs
            implicitWidth: 360; implicitHeight: 80
            Tk.Scroll {
                Tk.Flex {
                    direction: Tk.Flex.Column; gap: 1; padding: Tk.Theme.space.xs
                    Repeater { model: 12; Tk.ListRow { required property int index; text: "Row " + (index + 1) + " of a scrolling list"; small: true } }
                }
            }
        }
    }
    GalleryRow {
        label: "Splitter"
        Tk.Splitter {
            implicitWidth: 630; implicitHeight: 72
            Tk.Box { T.SplitView.preferredWidth: 200; T.SplitView.minimumWidth: 80; color: Tk.Theme.color.panelAlt; Tk.Caption { anchors.centerIn: parent; text: "pane 200px" } }
            Tk.Box { T.SplitView.fillWidth: true; color: Tk.Theme.color.canvas; Tk.Caption { anchors.centerIn: parent; text: "fills (drag the seam)" } }
            Tk.Box { T.SplitView.preferredWidth: 160; color: Tk.Theme.color.panelAlt; Tk.Caption { anchors.centerIn: parent; text: "pane 160px" } }
        }
    }
}
