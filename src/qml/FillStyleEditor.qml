// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The fill descriptor is host-owned. `settings` retains one record per kind,
// so changing kind and returning restores the prior colour/gradient/pattern.
Item {
    id: editor
    required property var fill
    property var capabilities: ({})
    property var patterns: []
    property string tooltip: qsTr("Edit vector fill style")

    signal fillChangeRequested(var fill)
    signal gradientEditRequested(var gradient)

    readonly property var kinds: ["none", "solid", "gradient", "pattern"]
    readonly property var settings: fill.settings || ({})
    readonly property var currentSettings: settings[fill.kind] || defaultSettings(fill.kind)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function defaultSettings(kind) {
        if (kind === "solid") return { color: Tk.Theme.color.accent }
        if (kind === "gradient") return { gradient: null }
        if (kind === "pattern") return { patternId: patterns.length ? patterns[0].id : "" }
        return {}
    }
    function request(nextKind, patch) {
        const oldSettings = Object.assign({}, settings)
        const merged = Object.assign({}, oldSettings[nextKind] || defaultSettings(nextKind), patch || {})
        oldSettings[nextKind] = merged
        fillChangeRequested(Object.assign({}, fill, { kind: nextKind, settings: oldSettings }))
    }
    function patternIndex(id) {
        for (let i = 0; i < patterns.length; ++i) if (patterns[i].id === id) return i
        return -1
    }

    Tk.Flex {
        id: column
        width: editor.width > 0 ? editor.width : implicitWidth
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Stretch

        Tk.Overline { title: qsTr("FILL") }
        Tk.PropertyRow {
            label: qsTr("Kind")
            Tk.ComboBox {
                objectName: "fillKind"
                model: editor.kinds
                currentIndex: editor.kinds.indexOf(editor.fill.kind || "solid")
                tooltip: qsTr("Fill kind")
                onActivated: function(index) { editor.request(model[index], {}) }
            }
        }
        Tk.PropertyRow {
            visible: editor.fill.kind === "solid"
            label: qsTr("Color")
            Tk.ColorField {
                objectName: "fillSolidColor"
                color: editor.currentSettings.color || Tk.Theme.color.accent
                tooltip: qsTr("Solid fill colour")
                onColorEdited: function(color) { editor.request("solid", { color: color }) }
            }
        }
        Tk.PropertyRow {
            visible: editor.fill.kind === "gradient"
            label: qsTr("Gradient")
            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Caption {
                    text: editor.currentSettings.gradient
                          ? qsTr("%1 stops · %2").arg((editor.currentSettings.gradient.stops || []).length)
                                .arg(editor.currentSettings.gradient.type || qsTr("linear"))
                          : qsTr("No gradient assigned")
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Button {
                    objectName: "fillEditGradient"
                    text: qsTr("Edit…")
                    enabled: editor.capabilities.gradient !== false
                    tooltip: qsTr("Edit this fill's gradient")
                    onClicked: editor.gradientEditRequested(editor.currentSettings.gradient)
                }
            }
        }
        Tk.PropertyRow {
            visible: editor.fill.kind === "pattern"
            label: qsTr("Pattern")
            Tk.ComboBox {
                objectName: "fillPattern"
                model: editor.patterns
                textRole: "label"
                currentIndex: editor.patternIndex(editor.currentSettings.patternId || "")
                placeholderText: qsTr("No pattern available")
                tooltip: qsTr("Pattern fill source")
                enabled: editor.capabilities.pattern !== false && editor.patterns.length > 0
                onActivated: function(index) { editor.request("pattern", { patternId: model[index].id }) }
            }
        }
        Tk.Caption {
            visible: editor.capabilities[editor.fill.kind] === false
            color: Tk.Theme.color.warning
            text: qsTr("The active renderer cannot display this fill kind. Its settings are retained.")
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            visible: editor.fill.unsupported === true
            color: Tk.Theme.color.warning
            text: editor.fill.unsupportedReason || qsTr("This fill contains unsupported settings.")
            wrapMode: Text.WordWrap
        }
    }
}
