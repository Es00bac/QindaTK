// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Narrow inspector fixture: long labels must wrap, card tracks must reflow,
// and overlay controls must remain reachable in either reading direction.
Item {
    id: root
    objectName: "adaptiveRoot"
    width: 360
    height: 300
    property int interfaceDirection: Qt.application.layoutDirection

    Tk.Flex {
        id: actionRow
        objectName: "adaptiveFlex"
        x: 12
        y: 12
        width: 336
        height: 72
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

    Tk.Grid {
        objectName: "adaptiveGrid"
        x: 12
        y: 96
        width: 336
        height: 84
        columns: "repeat(auto-fill, minmax(144, 1fr))"
        rows: "auto"
        gap: Tk.Theme.space.xs
        layoutDirection: root.interfaceDirection

        Tk.Label {
            objectName: "adaptiveCardA"
            text: "Audio conforms to the selected delivery profile"
            wrapMode: Text.Wrap
            Tk.Grid.column: 0
        }
        Tk.Label {
            objectName: "adaptiveCardB"
            text: "Color space and frame rate follow sequence settings"
            wrapMode: Text.Wrap
            Tk.Grid.column: 1
        }
    }

    Tk.Stack {
        objectName: "adaptiveStack"
        x: 12
        y: 196
        width: 336
        height: 84
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
