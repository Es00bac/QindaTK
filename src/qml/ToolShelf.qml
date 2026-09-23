// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Tool selector for canvas and editing contexts. Ownership of the effective
// tool stays with the host; temporary tools are an explicit push/pop request.
Item {
    id: shelf
    property var tools: []
    property string activeToolId: ""
    property string orientation: "vertical"
    property string tooltip: qsTr("Tools")
    property string temporaryToolId: ""
    readonly property string effectiveToolId: temporaryToolId.length ? temporaryToolId : activeToolId
    property real gap: Tk.Theme.space.xs
    signal toolRequested(string toolId)
    signal temporaryToolRequested(string toolId)
    signal temporaryToolReleased(string toolId)

    readonly property bool vertical: orientation !== "horizontal"
    implicitWidth: vertical ? Tk.Theme.size.action : row.implicitWidth
    implicitHeight: vertical ? row.implicitHeight : Tk.Theme.size.action
    Accessible.role: Accessible.ToolBar
    Accessible.name: tooltip
    function beginTemporaryTool(toolId) {
        if (!toolId.length || temporaryToolId === toolId) return
        temporaryToolId = toolId
        temporaryToolRequested(toolId)
    }
    function endTemporaryTool() {
        if (!temporaryToolId.length) return
        const old = temporaryToolId
        temporaryToolId = ""
        temporaryToolReleased(old)
    }
    function stepTool(index, direction) {
        if (!Array.isArray(tools) || tools.length < 1) return
        for (let n = 1; n <= tools.length; ++n) {
            const next = (index + direction * n + tools.length) % tools.length
            const tool = tools[next]
            if (tool.enabled !== false) {
                const item = buttons.itemAt(next)
                if (item) item.forceActiveFocus(Qt.TabFocusReason)
                if (item) item.click()
                return
            }
        }
    }
    Tk.Box {
        anchors.fill: parent
        padding: Tk.Theme.space.xs
        color: Tk.Theme.color.panelAlt
        borderColor: Tk.Theme.color.controlBorder
        radius: Tk.Theme.radius.sm
        Tk.Flex {
            id: row
            direction: shelf.vertical ? Tk.Flex.Column : Tk.Flex.Row
            gap: shelf.gap
            align: Tk.Flex.Center
            Repeater {
                id: buttons
                model: Array.isArray(shelf.tools) ? shelf.tools : []
                delegate: T.AbstractButton {
                    id: toolButton
                    required property int index
                    required property var modelData
                    readonly property bool selected: modelData.id === shelf.effectiveToolId
                    objectName: "tool_" + modelData.id
                    text: modelData.label || modelData.id || ""
                    enabled: modelData.enabled !== false
                    checkable: false
                    checked: selected
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    implicitWidth: Tk.Theme.size.controlLg
                    implicitHeight: implicitWidth
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.label || modelData.id || ""
                    Accessible.description: modelData.tooltip || ""
                    Accessible.checked: selected
                    Accessible.focusable: enabled
                    Accessible.onPressAction: if (enabled) click()
                    onClicked: {
                        if (modelData.commandId && shelf.registry) {
                            if (!shelf.registry.trigger(modelData.commandId)) return
                        }
                        shelf.toolRequested(modelData.id)
                    }
                    Keys.onLeftPressed: if (!shelf.vertical) shelf.stepTool(index, -1)
                    Keys.onRightPressed: if (!shelf.vertical) shelf.stepTool(index, 1)
                    Keys.onUpPressed: if (shelf.vertical) shelf.stepTool(index, -1)
                    Keys.onDownPressed: {
                        if (modelData.subtools && modelData.subtools.length) {
                            subtoolMenu.popup(toolButton, Qt.point(0, toolButton.height))
                        } else if (shelf.vertical) shelf.stepTool(index, 1)
                    }
                    Keys.onMenuPressed: if (modelData.subtools && modelData.subtools.length) subtoolMenu.popup()
                    background: Rectangle {
                        radius: Tk.Theme.radius.sm
                        color: toolButton.selected ? Tk.Theme.color.controlActiveBg
                             : toolButton.hovered ? Tk.Theme.color.controlHoverBg
                                                   : Tk.Theme.alpha(Tk.Theme.color.panelAlt, Tk.Theme.opacity.hidden)
                        border.width: toolButton.activeFocus ? Tk.Theme.size.focusRing : Tk.Theme.size.border
                        border.color: toolButton.activeFocus ? Tk.Theme.color.focus
                                    : toolButton.selected ? Tk.Theme.color.controlActiveBorder
                                                          : Tk.Theme.alpha(Tk.Theme.color.panelAlt, Tk.Theme.opacity.hidden)
                    }
                    contentItem: Tk.Flex {
                        direction: shelf.vertical ? Tk.Flex.Column : Tk.Flex.Row
                        align: Tk.Flex.Center
                        justify: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        Tk.Icon {
                            name: toolButton.modelData.iconName || "cursor"
                            size: Tk.Theme.size.icon
                            color: toolButton.selected ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
                        }
                        Tk.Caption {
                            visible: toolButton.modelData.showLabel === true
                            text: toolButton.modelData.label || ""
                            color: Tk.Theme.color.textMuted
                        }
                    }
                    Tk.ToolTip {
                        text: toolButton.modelData.tooltip || toolButton.modelData.label || ""
                        visible: text.length > 0 && toolButton.hovered
                    }
                    Tk.Menu {
                        id: subtoolMenu
                        title: toolButton.modelData.label || ""
                        Repeater {
                            model: toolButton.modelData.subtools || []
                            delegate: Tk.MenuItem {
                                required property var modelData
                                text: modelData.label || modelData.id || ""
                                iconName: modelData.iconName || ""
                                onTriggered: shelf.toolRequested(modelData.id)
                            }
                        }
                        onClosed: toolButton.forceActiveFocus(Qt.PopupFocusReason)
                    }
                }
            }
        }
    }
    // Registry is optional; a tool model can instead be driven directly by ToolController.
    property Tk.CommandRegistry registry: null
}
