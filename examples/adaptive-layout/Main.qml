// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Narrow inspector fixture: sections flow by their wrapped content height.
// At large text sizes the viewport scrolls instead of overlapping sections.
Item {
    id: root
    objectName: "adaptiveRoot"
    width: 360
    height: 300
    property int interfaceDirection: Qt.application.layoutDirection

    Tk.Scroll {
        objectName: "adaptiveScroll"
        anchors.fill: parent
        accessibleName: "Adaptive inspector"

        Tk.Flex {
            objectName: "adaptiveColumn"
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.lg
            padding: Tk.Theme.space.sm

            Tk.Flex {
                objectName: "adaptiveFlex"
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Start
                alignContent: Tk.Flex.Start
                gap: Tk.Theme.space.xs
                layoutDirection: root.interfaceDirection

                Tk.Label {
                    objectName: "adaptiveActionPrimary"
                    text: "Consolidate selected media"
                    wrapMode: Text.Wrap
                    Tk.Flex.minWidth: 104
                    Tk.Flex.maxWidth: 164
                }
                Tk.Label {
                    objectName: "adaptiveActionSecondary"
                    text: "Reveal source in project"
                    wrapMode: Text.Wrap
                    Tk.Flex.minWidth: 104
                    Tk.Flex.maxWidth: 164
                }
                Tk.Label {
                    objectName: "adaptiveActionTertiary"
                    text: "Render selected sequence"
                    wrapMode: Text.Wrap
                    Tk.Flex.minWidth: 104
                    Tk.Flex.maxWidth: 164
                }
            }

            Item {
                objectName: "adaptiveGridHost"
                implicitHeight: Math.max(cardA.implicitHeight, cardB.implicitHeight)
                Tk.Grid {
                    objectName: "adaptiveGrid"
                    anchors.fill: parent
                    columns: "repeat(auto-fill, minmax(144, 1fr))"
                    rows: "auto"
                    gap: Tk.Theme.space.xs
                    layoutDirection: root.interfaceDirection

                    Tk.Label {
                        id: cardA
                        objectName: "adaptiveCardA"
                        text: "Audio conforms to the selected delivery profile"
                        wrapMode: Text.Wrap
                        Tk.Grid.column: 0
                    }
                    Tk.Label {
                        id: cardB
                        objectName: "adaptiveCardB"
                        text: "Color space and frame rate follow sequence settings"
                        wrapMode: Text.Wrap
                        Tk.Grid.column: 1
                    }
                }
            }

            Tk.Stack {
                objectName: "adaptiveStack"
                padding: Tk.Theme.space.sm
                layoutDirection: root.interfaceDirection

                Tk.Label {
                    objectName: "adaptiveStackMessage"
                    text: "Background analysis continues while this panel is open"
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignTop
                    Tk.Stack.fill: true
                    Tk.Stack.left: 0
                    Tk.Stack.right: taskBadge.implicitWidth + Tk.Theme.space.xs
                    Tk.Stack.top: 0
                    Tk.Stack.bottom: 0
                }
                Tk.Badge {
                    id: taskBadge
                    objectName: "adaptiveStackBadge"
                    text: "3 tasks"
                    variant: "accent"
                    Tk.Stack.fill: false
                    Tk.Stack.top: 0
                    Tk.Stack.right: 0
                }
            }
        }
    }
}
