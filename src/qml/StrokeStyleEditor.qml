// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Edits one canonical stroke descriptor. The host owns preview, persistence,
// unit conversion and the transaction represented by styleChangeRequested.
// Each request carries `changed`, the fields the user set, so a host can
// apply exactly those (a value equal to a mixed key's shown value included).
// `style.mixed` lists fields whose values differ across a selection: they
// show "Mixed" until the user sets them.
Item {
    id: editor
    required property var style
    property var capabilities: ({})
    property string tooltip: qsTr("Edit vector stroke style")

    signal styleChangeRequested(var style)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    readonly property var units: capabilities.units || ["px", "pt", "document"]
    readonly property var alignments: capabilities.alignments || ["center", "inside", "outside"]
    readonly property var caps: capabilities.caps || ["butt", "round", "square"]
    readonly property var joins: capabilities.joins || ["miter", "round", "bevel"]
    readonly property var dashModes: ["solid", "dash", "dot", "custom"]
    readonly property string dashText: (style.dash || []).join(" ")

    // AGENT-NOTE: the owner's descriptor is the truth. User edits assign the
    // fields' own values, which ends their bindings, so the fields are synced
    // from `style` whenever the owner changes it and after every request (an
    // owner may reject or later undo the edit).
    function syncFields() {
        widthField.value = editor.style.width === undefined ? 1 : editor.style.width
        widthField.mixed = editor.isMixed("width")
        miterField.value = editor.style.miterLimit === undefined ? 4 : editor.style.miterLimit
        miterField.mixed = editor.isMixed("miterLimit")
        colorField.color = editor.style.color || Tk.Theme.color.text
    }
    function isMixed(field) { return (editor.style.mixed || []).indexOf(field) >= 0 }
    function request(patch) {
        styleChangeRequested(Object.assign({}, style, patch, { changed: Object.keys(patch) }))
        syncFields()
    }
    onStyleChanged: syncFields()
    function indexOf(list, value) {
        const index = list.indexOf(value)
        return index < 0 ? -1 : index
    }
    function parseDash(text) {
        const values = text.trim().split(/[ ,]+/).filter(function(part) { return part.length > 0 }).map(Number)
        if (values.length < 2 || values.length % 2 !== 0 || values.some(function(value) { return !isFinite(value) || value <= 0 }))
            return false
        request({ dash: values, dashMode: "custom" })
        return true
    }

    Tk.Flex {
        id: column
        width: editor.width > 0 ? editor.width : implicitWidth
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Stretch

        Tk.Overline { title: qsTr("STROKE") }
        Tk.PropertyRow {
            label: qsTr("Width")
            Tk.NumberField {
                id: widthField
                objectName: "strokeWidth"
                value: editor.style.width === undefined ? 1 : editor.style.width
                from: 0
                to: 10000
                stepSize: 0.25
                decimals: 2
                mixed: editor.isMixed("width")
                tooltip: qsTr("Stroke width in %1").arg(editor.style.unit || "px")
                onValueModified: function(value) { editor.request({ width: value }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Units")
            Tk.ComboBox {
                objectName: "strokeUnits"
                model: editor.units
                currentIndex: editor.indexOf(model, editor.style.unit || "px")
                tooltip: qsTr("Stroke width units")
                onActivated: function(index) { editor.request({ unit: model[index] }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Color")
            Tk.ColorField {
                id: colorField
                objectName: "strokeColor"
                color: editor.style.color || Tk.Theme.color.text
                tooltip: qsTr("Stroke colour")
                onColorEdited: function(color) { editor.request({ color: color }) }
            }
        }
        Tk.Caption {
            objectName: "strokeColorMixed"
            visible: editor.isMixed("color")
            text: qsTr("Mixed colours: a colour you choose applies to every object.")
            wrapMode: Text.WordWrap
        }
        Tk.PropertyRow {
            label: qsTr("Alignment")
            Tk.ComboBox {
                objectName: "strokeAlignment"
                model: editor.alignments
                currentIndex: editor.isMixed("alignment") ? -1 : editor.indexOf(model, editor.style.alignment || "center")
                placeholderText: qsTr("Mixed")
                tooltip: qsTr("Stroke alignment")
                onActivated: function(index) { editor.request({ alignment: model[index] }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Cap")
            Tk.ComboBox {
                objectName: "strokeCap"
                model: editor.caps
                currentIndex: editor.isMixed("cap") ? -1 : editor.indexOf(model, editor.style.cap || "butt")
                placeholderText: qsTr("Mixed")
                tooltip: qsTr("Stroke end cap")
                onActivated: function(index) { editor.request({ cap: model[index] }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Join")
            Tk.ComboBox {
                objectName: "strokeJoin"
                model: editor.joins
                currentIndex: editor.isMixed("join") ? -1 : editor.indexOf(model, editor.style.join || "miter")
                placeholderText: qsTr("Mixed")
                tooltip: qsTr("Stroke join")
                onActivated: function(index) { editor.request({ join: model[index] }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Miter limit")
            Tk.NumberField {
                id: miterField
                objectName: "strokeMiterLimit"
                value: editor.style.miterLimit === undefined ? 4 : editor.style.miterLimit
                from: 1
                to: 100
                stepSize: 0.25
                decimals: 2
                mixed: editor.isMixed("miterLimit")
                tooltip: qsTr("Miter limit, in stroke widths")
                onValueModified: function(value) { editor.request({ miterLimit: value }) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Dash")
            Tk.ComboBox {
                objectName: "strokeDashMode"
                model: editor.dashModes
                currentIndex: editor.isMixed("dash") ? -1
                    : editor.indexOf(model, editor.style.dashMode || (editor.style.dash && editor.style.dash.length ? "custom" : "solid"))
                placeholderText: qsTr("Mixed")
                tooltip: qsTr("Stroke dash pattern")
                onActivated: function(index) {
                    const mode = model[index]
                    if (mode === "solid") editor.request({ dash: [], dashMode: mode })
                    else if (mode === "dash") editor.request({ dash: [6, 3], dashMode: mode })
                    else if (mode === "dot") editor.request({ dash: [1, 2], dashMode: mode })
                    else editor.request({ dashMode: mode })
                }
            }
        }
        Tk.TextField {
            objectName: "strokeDashPattern"
            visible: (editor.style.dashMode || "solid") === "custom"
            text: editor.dashText
            placeholderText: qsTr("e.g. 6 2 1 2")
            tooltip: qsTr("Even list of positive dash and gap lengths")
            onAccepted: error = !editor.parseDash(text)
        }
        Tk.Caption {
            visible: editor.style.unsupported === true || editor.capabilities.supported === false
            color: Tk.Theme.color.warning
            text: editor.style.unsupportedReason || qsTr("Some stroke settings are unsupported by the active renderer.")
            wrapMode: Text.WordWrap
        }
    }
}
