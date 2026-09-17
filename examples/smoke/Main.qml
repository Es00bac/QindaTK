// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The smallest useful QindaTK screen: a panel with an inspector-style
// grid inside a scroll, beside a canvas area. Run:
//   qtk-preview examples/smoke/Main.qml --dump
Rectangle {
    id: root
    width: 640
    height: 400
    color: Tk.Theme.color.bg

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm

        Tk.Box {
            objectName: "canvas"
            color: Tk.Theme.color.canvas
            borderWidth: 1
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Island {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Tk.Theme.space.sm
                Tk.Caption { text: "185%" }
                Tk.IconButton { iconName: "zoom-in"; tooltip: "Zoom in" }
                Tk.IconButton { iconName: "zoom-out"; tooltip: "Zoom out" }
            }
        }

        Tk.Panel {
            objectName: "inspector"
            title: "Inspector"
            closable: true
            floatable: true
            Tk.Flex.basis: 240
            Tk.Flex.shrink: 0

            Tk.Scroll {
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm

                    Tk.SectionHeader { title: "Document"; count: "3 of 5" }
                    Tk.Grid {
                        columns: "auto 1fr"
                        gap: Tk.Theme.space.xs
                        columnGap: Tk.Theme.space.sm
                        alignItems: Tk.Grid.Center
                        Tk.Caption { text: "Page size" }
                        Tk.Label { text: "Comic Book (170 x 260)" }
                        Tk.Caption { text: "Width mm" }
                        Tk.Mono { text: "170" }
                        Tk.Caption { text: "Bleed mm" }
                        Tk.Mono { text: "3.17" }
                    }
                    Tk.Divider {}
                    Tk.SectionHeader { title: "Baseline grid" }
                    Tk.Label { text: "Start mm 12.7"; muted: true }
                }
            }
        }
    }
}
