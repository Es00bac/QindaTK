// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QindaTK as Tk

// AGENT-NOTE (docs/android.md): a phone-shaped screen built only from QindaTK
// controls and Flex/Grid layout. objectNames are stable for adb-driven checks.
Window {
    id: win
    visible: true
    color: Tk.Theme.color.bg
    title: "QindaTK smoke"

    // AGENT-NOTE: theme and density are process-wide singletons (docs/theming.md).
    Component.onCompleted: {
        Tk.Theme.applyPreset("sloom-dark")
        Tk.Density.mode = Tk.Density.Touch
    }

    // Edge-to-edge window: keep content out of the status and navigation bars.
    readonly property real insetTop: win.SafeArea.margins.top
    readonly property real insetBottom: win.SafeArea.margins.bottom

    ListModel {
        id: devices
        ListElement { key: "d1"; label: "Desktop"; secondary: "QindaQt, paired"; icon: "monitor" }
        ListElement { key: "d2"; label: "Laptop"; secondary: "Last seen 4 min ago"; icon: "cpu" }
        ListElement { key: "d3"; label: "Tablet"; secondary: "Not paired"; icon: "smartphone" }
        ListElement { key: "d4"; label: "Media PC"; secondary: "Offline"; icon: "monitor-play" }
    }

    Tk.Flex {
        objectName: "smokeRoot"
        anchors.fill: parent
        anchors.topMargin: win.insetTop
        anchors.bottomMargin: win.insetBottom
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md

        Tk.Heading { text: "QindaTK on Android"; level: 1 }
        Tk.Caption { text: "Touch density, sloom-dark theme" }

        Tk.Panel {
            objectName: "smokePanel"
            title: "Pairing"
            collapsible: false
            grip: false
            Tk.Flex.shrink: 0

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md

                Tk.TextField {
                    objectName: "smokeName"
                    placeholderText: "This phone's name"
                    clearable: true
                    tooltip: "Device name"
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                }
                Tk.Switch {
                    objectName: "smokeSwitch"
                    text: "Allow notification mirroring"
                    checked: true
                    tooltip: "Mirror notifications"
                }
                Tk.Grid {
                    columns: "1fr 1fr"
                    columnGap: Tk.Theme.space.md
                    Tk.Button {
                        objectName: "smokeCancel"
                        text: "Cancel"
                        tooltip: "Cancel pairing"
                    }
                    Tk.Button {
                        objectName: "smokePair"
                        text: "Pair"
                        variant: "accent"
                        tooltip: "Start pairing"
                    }
                }
            }
        }

        Tk.SectionHeader { title: "Devices"; count: devices.count }
        Tk.VirtualList {
            objectName: "smokeList"
            model: devices
            idRole: "key"
            tooltip: "Known devices"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
    }
}
