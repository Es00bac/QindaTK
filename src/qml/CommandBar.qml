// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Model-driven command strip. Items retain stable IDs across user ordering;
// registry projections provide live enabled/checked state.
Item {
    id: bar
    property Tk.CommandRegistry registry: null
    property Tk.ShortcutMap shortcutMap: null
    property var model: []
    property var defaultOrder: []
    property var customizedOrder: []
    property string tooltip: qsTr("Commands")
    property bool compact: false
    readonly property var orderedItems: orderedModel()
    signal orderChanged(var ids)

    implicitWidth: toolbar.implicitWidth
    implicitHeight: toolbar.implicitHeight
    Accessible.role: Accessible.ToolBar
    Accessible.name: tooltip
    function orderedModel() {
        const source = Array.isArray(model) ? model : []
        const ids = Array.isArray(customizedOrder) && customizedOrder.length
                    ? customizedOrder : defaultOrder
        if (!Array.isArray(ids) || ids.length === 0) return source.slice(0)
        const used = {}
        const result = []
        for (let n = 0; n < ids.length; ++n) {
            const wanted = String(ids[n])
            for (let i = 0; i < source.length; ++i) {
                if (String(source[i].id) === wanted && !used[wanted]) {
                    result.push(source[i]); used[wanted] = true; break
                }
            }
        }
        for (let i = 0; i < source.length; ++i)
            if (!used[String(source[i].id)]) result.push(source[i])
        return result
    }
    function setOrder(ids) {
        if (!Array.isArray(ids)) return false
        const known = {}
        const source = Array.isArray(model) ? model : []
        for (let i = 0; i < source.length; ++i) known[String(source[i].id)] = true
        const next = []
        for (let i = 0; i < ids.length; ++i)
            if (known[String(ids[i])] && next.indexOf(String(ids[i])) < 0) next.push(String(ids[i]))
        customizedOrder = next
        orderChanged(next)
        return true
    }
    function resetOrder() {
        customizedOrder = []
        orderChanged(defaultOrder.slice(0))
    }
    Tk.ToolBar {
        id: toolbar
        width: bar.width
        compact: bar.compact
        wrap: false
        Repeater {
            model: bar.orderedItems
            delegate: Item {
                required property int index
                required property var modelData
                objectName: "commandSlot_" + modelData.id
                implicitWidth: controlLoader.item ? controlLoader.item.implicitWidth : 0
                implicitHeight: controlLoader.item ? controlLoader.item.implicitHeight : Tk.Theme.size.control
                Loader {
                    id: controlLoader
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: modelData.kind === "separator" ? separatorComponent
                                     : modelData.kind === "property" ? propertyComponent
                                     : commandComponent
                }
                Component {
                    id: separatorComponent
                    Tk.ToolSeparator { }
                }
                Component {
                    id: commandComponent
                    Tk.CommandButton {
                        registry: bar.registry
                        shortcutMap: bar.shortcutMap
                        commandId: modelData.commandId || modelData.id
                        iconOnly: modelData.iconOnly === true
                        tooltip: modelData.tooltip || modelData.label || ""
                        small: bar.compact
                        Accessible.name: modelData.label || modelData.id
                    }
                }
                Component {
                    id: propertyComponent
                    Tk.Slider {
                        objectName: "propertyCommand_" + modelData.id
                        property Tk.PropertyBinding binding: modelData.binding || null
                        width: modelData.width || Tk.Theme.size.fieldWidth
                        from: modelData.from !== undefined ? modelData.from : 0
                        to: modelData.to !== undefined ? modelData.to : 1
                        stepSize: modelData.stepSize !== undefined ? modelData.stepSize : 0.01
                        value: binding ? Number(binding.value) : 0
                        tooltip: modelData.label || modelData.id
                        enabled: binding !== null && binding.writable
                        onValueModified: function(v) {
                            if (!binding) return
                            if (!binding.editing && !binding.begin()) return
                            if (!binding.update(v)) { binding.cancel(); return }
                            if (!pressed) binding.commit()
                        }
                        onPressedChanged: {
                            if (!binding) return
                            if (pressed) binding.begin()
                            else if (binding.editing) binding.commit()
                        }
                        Keys.onEscapePressed: if (binding) binding.cancel()
                    }
                }
            }
        }
    }
}
