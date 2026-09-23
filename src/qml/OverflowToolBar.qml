// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Deterministic measured overflow. Each command has one visible presentation
// at a time; hidden measurement rows never take focus or receive input.
Item {
    id: bar
    property Tk.CommandRegistry registry: null
    property Tk.ShortcutMap shortcutMap: null
    property var model: []
    property real gap: Tk.Theme.space.sm
    property string tooltip: qsTr("More commands")
    property bool compact: false
    readonly property var visibleIndices: _visibleIndices
    readonly property var overflowIndices: _overflowIndices
    property var _visibleIndices: []
    property var _overflowIndices: []
    property string _focusedCommandId: ""
    property bool _fitting: false
    signal overflowChanged(var commandIds)

    implicitHeight: Tk.Theme.size.toolbar
    Accessible.role: Accessible.ToolBar
    Accessible.name: tooltip
    function entries() { return Array.isArray(model) ? model : [] }
    function measureAt(index) {
        const measured = measurements.itemAt(index)
        if (measured) return measured.implicitWidth
        // Delegates are created asynchronously; fit runs again when their
        // measured widths become available.
        return Tk.Theme.size.control
    }
    function groupKey(entry, index) { return entry.groupId ? "g:" + entry.groupId : "i:" + index }
    function fit() {
        if (_fitting) return
        _fitting = true
        const source = entries()
        const all = []
        for (let i = 0; i < source.length; ++i) if (source[i].visible !== false) all.push(i)
        const avail = Math.max(0, width - Tk.Theme.space.sm * 2)
        const sum = function(indices) {
            let value = 0
            for (let n = 0; n < indices.length; ++n) value += measureAt(indices[n])
            return value + Math.max(0, indices.length - 1) * gap
        }
        let keep = all.slice(0)
        let spill = []
        const menuWidth = Tk.Theme.size.control
        while (keep.length > 0 && sum(keep) + (spill.length ? gap + menuWidth : 0) > avail) {
            let candidate = null
            for (let n = 0; n < keep.length; ++n) {
                const e = source[keep[n]]
                const key = groupKey(e, keep[n])
                const priority = Number(e.priority === undefined ? 0 : e.priority)
                if (candidate === null || priority < candidate.priority)
                    candidate = { key: key, priority: priority, order: n }
            }
            const removed = []
            keep = keep.filter(function(index) {
                if (groupKey(source[index], index) === candidate.key) { removed.push(index); return false }
                return true
            })
            spill = spill.concat(removed)
        }
        spill.sort(function(a, b) { return a - b })
        _visibleIndices = keep
        _overflowIndices = spill
        const moved = _focusedCommandId.length && spill.some(function(i) { return String(source[i].id) === _focusedCommandId })
        if (moved) {
            const targetIndex = spill.findIndex(function(i) { return String(source[i].id) === _focusedCommandId })
            Qt.callLater(function() {
                overflowButton.forceActiveFocus(Qt.TabFocusReason)
                if (!choices.visible) choices.popup(overflowButton, Qt.point(0, overflowButton.height))
                choices.currentIndex = targetIndex
            })
        }
        const ids = []
        for (let n = 0; n < spill.length; ++n) ids.push(String(source[spill[n]].id))
        overflowChanged(ids)
        _fitting = false
    }
    onWidthChanged: fit()
    onModelChanged: Qt.callLater(fit)
    onGapChanged: fit()
    Component.onCompleted: Qt.callLater(fit)
    Connections {
        target: bar.registry
        function onChanged() { Qt.callLater(bar.fit) }
    }
    Repeater {
        id: measurements
        model: bar.entries()
        delegate: Item {
            id: measure
            required property var modelData
            objectName: "measure_" + modelData.id
            visible: false
            readonly property string effectiveLabel: measureBinding.label.length
                    ? measureBinding.label : modelData.label || modelData.id || ""
            readonly property real iconWidth: modelData.iconName || measureBinding.iconName
                    ? (bar.compact ? Tk.Theme.size.iconSm : Tk.Theme.size.icon) : 0
            implicitWidth: Math.max(Tk.Theme.size.control,
                (modelData.iconOnly === true ? 0 : labelMetrics.width)
                + iconWidth + (iconWidth > 0 && effectiveLabel.length > 0
                    ? Tk.Theme.space.xs + Tk.Theme.size.focusRing : 0)
                + (bar.compact ? (Tk.Theme.space.sm + Tk.Theme.size.focusRing) * 2
                               : Tk.Theme.space.md * 2))
            implicitHeight: bar.compact ? Tk.Theme.size.controlSm : Tk.Theme.size.control
            TextMetrics {
                id: labelMetrics
                text: modelData.iconOnly === true ? "" : measure.effectiveLabel
                font.pixelSize: bar.compact ? Tk.Theme.font.small : Tk.Theme.font.body
            }
            onImplicitWidthChanged: Qt.callLater(bar.fit)
            Tk.CommandBinding {
                id: measureBinding
                registry: bar.registry
                shortcutMap: bar.shortcutMap
                commandId: modelData.commandId || modelData.id
            }
        }
    }
    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.surface
        borderColor: Tk.Theme.color.divider
        borderBottom: Tk.Theme.size.border
        paddingLeft: Tk.Theme.space.sm
        paddingRight: Tk.Theme.space.sm
        Tk.Flex {
            id: row
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: bar.gap
            Repeater {
                model: bar.visibleIndices
                delegate: Tk.CommandButton {
                    required property int modelData
                    readonly property var entry: bar.entries()[modelData]
                    objectName: "toolbarCommand_" + entry.id
                    registry: bar.registry
                    shortcutMap: bar.shortcutMap
                    commandId: entry.commandId || entry.id
                    iconOnly: entry.iconOnly === true
                    small: bar.compact
                    tooltip: entry.tooltip || entry.label || entry.id
                    Accessible.name: entry.label || entry.id
                    onActiveFocusChanged: if (activeFocus) bar._focusedCommandId = String(entry.id)
                }
            }
            T.Button {
                id: overflowButton
                objectName: "toolbarOverflowButton"
                visible: bar.overflowIndices.length > 0
                implicitWidth: Tk.Theme.size.control
                implicitHeight: bar.compact ? Tk.Theme.size.controlSm : Tk.Theme.size.control
                text: ""
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: bar.tooltip
                Accessible.description: qsTr("%1 additional commands").arg(bar.overflowIndices.length)
                Accessible.focusable: visible && enabled
                Accessible.onPressAction: if (visible) openOverflow()
                onClicked: openOverflow()
                Keys.onDownPressed: openOverflow()
                contentItem: Tk.Icon { name: "more-horizontal"; size: Tk.Theme.size.icon; color: Tk.Theme.color.textMuted }
                background: Rectangle {
                    radius: Tk.Theme.radius.sm
                    color: overflowButton.hovered || overflowButton.down ? Tk.Theme.color.controlHoverBg
                          : Tk.Theme.alpha(Tk.Theme.color.surface, Tk.Theme.opacity.hidden)
                    border.width: overflowButton.visualFocus ? Tk.Theme.size.focusRing : 0
                    border.color: Tk.Theme.color.focus
                }
                Tk.ToolTip { text: bar.tooltip; visible: overflowButton.hovered }
                function openOverflow() {
                    if (!visible) return
                    choices.popup(overflowButton, Qt.point(0, overflowButton.height))
                }
            }
        }
    }
    Tk.Menu {
        id: choices
        title: bar.tooltip
        Repeater {
            model: bar.overflowIndices
            delegate: Tk.MenuItem {
                required property int modelData
                readonly property var entry: bar.entries()[modelData]
                objectName: "overflowCommand_" + entry.id
                text: entry.label || entry.id
                iconName: entry.iconName || ""
                shortcut: entry.shortcut || ""
                tooltip: entry.tooltip || entry.label || entry.id
                commandRegistry: bar.registry
                shortcutMap: bar.shortcutMap
                commandId: entry.commandId || entry.id
                visible: entry.visible !== false
            }
        }
        onClosed: if (overflowButton.visible) overflowButton.forceActiveFocus(Qt.PopupFocusReason)
    }
}
