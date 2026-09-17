// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a grid of named colour swatches.
// `colors` is a list of colour strings or of {name, color}; the entry
// equal to `current` shows an accent ring. Tapping (or Space/Enter on a
// focused swatch) emits selected(color); the optional "Custom…" button
// emits customRequested() for the host to open a full picker. Sized to
// its content: `columns` swatches per row, 16px swatches.
Item {
    id: swatches

    property var colors: []
    property color current: "transparent"
    property int columns: 8
    property bool customButton: true
    property real swatchSize: Tk.Theme.size.iconLg

    signal selected(color color)
    signal customRequested()

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Colours")

    function entry(index) {
        const raw = swatches.colors[index]
        if (typeof raw === "string") {
            return { "name": raw, "color": raw }
        }
        return { "name": raw.name !== undefined ? raw.name : String(raw.color),
                 "color": raw.color }
    }

    Tk.Flex {
        id: column
        direction: Tk.Flex.Column
        align: Tk.Flex.Start
        gap: Tk.Theme.space.sm

        Tk.Grid {
            id: grid
            objectName: "swatchGrid"
            columns: "repeat(" + Math.max(1, swatches.columns) + ", auto)"
            gap: Tk.Theme.space.xs

            Repeater {
                model: Array.isArray(swatches.colors) ? swatches.colors.length : 0

                Item {
                    id: cell
                    required property int index
                    readonly property var entry: swatches.entry(index)
                    readonly property color swatchColor: entry.color
                    readonly property bool isCurrent: Qt.colorEqual(swatchColor, swatches.current)
                    objectName: "swatch_" + index
                    implicitWidth: swatches.swatchSize + Tk.Theme.space.sm
                    implicitHeight: implicitWidth
                    activeFocusOnTab: true

                    Accessible.role: Accessible.Button
                    Accessible.name: entry.name
                    Accessible.checked: isCurrent

                    Rectangle {
                        anchors.centerIn: parent
                        width: swatches.swatchSize
                        height: swatches.swatchSize
                        radius: Tk.Theme.radius.xs
                        color: cell.swatchColor
                        border.width: cell.isCurrent ? Tk.Theme.size.focusRing : Tk.Theme.size.border
                        border.color: cell.isCurrent ? Tk.Theme.color.accent
                                    : hover.hovered ? Tk.Theme.color.controlHoverBorder
                                    : Tk.Theme.color.borderStrong
                        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: Tk.Theme.radius.sm
                        color: "transparent"
                        border.width: Tk.Theme.size.focusRing
                        border.color: Tk.Theme.color.focus
                        visible: cell.activeFocus
                    }
                    HoverHandler { id: hover }
                    TapHandler { onTapped: swatches.selected(cell.swatchColor) }
                    Keys.onSpacePressed: swatches.selected(cell.swatchColor)
                    Keys.onReturnPressed: swatches.selected(cell.swatchColor)
                    Tk.ToolTip {
                        text: cell.entry.name
                        visible: hover.hovered
                        delay: 600
                    }
                }
            }
        }
        Tk.Button {
            objectName: "swatchCustom"
            visible: swatches.customButton
            text: qsTr("Custom…")
            iconName: "pipette"
            small: true
            variant: "ghost"
            onClicked: swatches.customRequested()
        }
    }
}
