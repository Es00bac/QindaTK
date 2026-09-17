// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the Ctrl+K palette. `commands` is a list of
// {id, label, shortcut, section, iconName, keywords}; typing filters by a
// case-insensitive substring over label and keywords, results are
// grouped by section (first-appearance order) under overline rows, and
// ↑/↓ + Enter (or a click) emit `activated(id)` and close. Top-centred
// at 12% of the window height, width min(560, window - 32).
T.Popup {
    id: palette

    property var commands: []
    property string placeholderText: qsTr("Type a command…")
    property int maxVisible: 10
    property alias filterText: input.text
    readonly property alias currentRow: list.currentIndex
    readonly property int resultCount: palette.rows.filter(function(r) { return !r.header }).length
    property var rows: []

    signal activated(string id)

    parent: T.Overlay.overlay
    modal: true
    dim: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round(parent.height * 0.12) : 0
    width: parent && parent.width > 0 ? Math.min(560, parent.width - Tk.Theme.space.xl * 2) : 560
    padding: 0
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding

    function rebuild() {
        const needle = input.text.trim().toLowerCase()
        const source = Array.isArray(palette.commands) ? palette.commands : []
        const sections = []
        const bySection = {}
        for (let i = 0; i < source.length; ++i) {
            const c = source[i]
            const label = (c.label !== undefined ? String(c.label) : "")
            const keywords = (c.keywords !== undefined ? String(c.keywords) : "")
            if (needle.length > 0 && label.toLowerCase().indexOf(needle) < 0
                    && keywords.toLowerCase().indexOf(needle) < 0) {
                continue
            }
            const section = c.section !== undefined ? String(c.section) : ""
            if (bySection[section] === undefined) {
                bySection[section] = []
                sections.push(section)
            }
            bySection[section].push(c)
        }
        const flat = []
        for (let s = 0; s < sections.length; ++s) {
            if (sections[s].length > 0) {
                flat.push({ "header": true, "label": sections[s] })
            }
            const group = bySection[sections[s]]
            for (let k = 0; k < group.length; ++k) {
                const c = group[k]
                flat.push({
                    "header": false,
                    "id": c.id !== undefined ? String(c.id) : "",
                    "label": c.label !== undefined ? String(c.label) : "",
                    "shortcut": c.shortcut !== undefined ? String(c.shortcut) : "",
                    "iconName": c.iconName !== undefined ? String(c.iconName) : ""
                })
            }
        }
        palette.rows = flat
        list.currentIndex = palette.firstSelectable(0, 1)
    }

    // Next selectable (non-header) row from `start` walking by `step`.
    function firstSelectable(start, step) {
        for (let i = start; i >= 0 && i < palette.rows.length; i += step) {
            if (!palette.rows[i].header) {
                return i
            }
        }
        return -1
    }
    function move(step) {
        const next = palette.firstSelectable(list.currentIndex + step, step)
        if (next >= 0) {
            list.currentIndex = next
            list.positionViewAtIndex(next, ListView.Contain)
        }
    }
    function activateCurrent() {
        const row = palette.rows[list.currentIndex]
        if (row === undefined || row.header) {
            return
        }
        palette.activated(row.id)
        palette.close()
    }

    onCommandsChanged: rebuild()
    onAboutToShow: {
        input.text = ""
        rebuild()
    }
    onOpened: input.forceActiveFocus(Qt.PopupFocusReason)

    // AGENT-NOTE: a Popup is not an Item; Accessible attaches to the content.
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Command palette")

        Tk.Box {
            padding: Tk.Theme.space.sm
            borderBottom: 1
            borderColor: Tk.Theme.color.divider
            Tk.Flex.shrink: 0

            Tk.TextField {
                id: input
                objectName: "paletteInput"
                iconName: "search"
                placeholderText: palette.placeholderText
                onTextChanged: palette.rebuild()
                Keys.onUpPressed: palette.move(-1)
                Keys.onDownPressed: palette.move(1)
                Keys.onReturnPressed: palette.activateCurrent()
                Keys.onEnterPressed: palette.activateCurrent()
                Keys.onEscapePressed: palette.close()
            }
        }

        ListView {
            id: list
            objectName: "paletteList"
            model: palette.rows
            clip: true
            implicitHeight: Math.min(contentHeight, palette.maxVisible * Tk.Theme.size.menuItem + Tk.Theme.space.xs * 2)
            Tk.Flex.shrink: 0
            topMargin: Tk.Theme.space.xs
            bottomMargin: Tk.Theme.space.xs
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            highlightFollowsCurrentItem: false
            T.ScrollBar.vertical: Tk.ScrollBar { }

            delegate: Item {
                id: row
                required property int index
                required property var modelData
                readonly property bool current: list.currentIndex === index
                width: ListView.view.width
                height: Tk.Theme.size.menuItem

                Tk.Overline {
                    visible: row.modelData.header
                    title: row.modelData.label
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Tk.Theme.space.md
                    anchors.rightMargin: Tk.Theme.space.md
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    visible: !row.modelData.header
                    anchors.fill: parent
                    anchors.leftMargin: Tk.Theme.space.xs
                    anchors.rightMargin: Tk.Theme.space.xs
                    radius: Tk.Theme.radius.xs
                    color: row.current ? Tk.Theme.color.selection
                         : rowHover.hovered ? Tk.Theme.color.hover : "transparent"

                    Tk.Flex {
                        anchors.fill: parent
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        paddingLeft: Tk.Theme.space.sm
                        paddingRight: Tk.Theme.space.sm

                        Tk.Icon {
                            visible: row.modelData.iconName !== undefined && row.modelData.iconName.length > 0
                            name: row.modelData.iconName !== undefined && row.modelData.iconName.length > 0 ? row.modelData.iconName : "circle"
                            size: Tk.Theme.size.icon
                            color: row.current ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                            Tk.Flex.shrink: 0
                        }
                        Tk.Label {
                            text: row.modelData.label !== undefined ? row.modelData.label : ""
                            font.pixelSize: Tk.Theme.font.small
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                        }
                        Tk.Caption {
                            visible: row.modelData.shortcut !== undefined && row.modelData.shortcut.length > 0
                            text: row.modelData.shortcut !== undefined ? row.modelData.shortcut : ""
                            Tk.Flex.shrink: 0
                        }
                    }
                    HoverHandler { id: rowHover }
                    TapHandler {
                        onTapped: {
                            list.currentIndex = row.index
                            palette.activateCurrent()
                        }
                    }
                }
            }

            Tk.EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: palette.resultCount === 0
                text: qsTr("No command matches \"%1\"").arg(input.text)
            }
        }
    }

    background: Rectangle {
        color: Tk.Theme.color.popoverBg
        border.width: 1
        border.color: Tk.Theme.color.popoverBorder
        radius: Tk.Theme.radius.lg
    }

    T.Overlay.modal: Rectangle {
        color: Tk.Theme.color.overlay
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Tk.Theme.motion.fast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Tk.Theme.motion.fast }
    }
}
