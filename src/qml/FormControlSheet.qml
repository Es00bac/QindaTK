// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "FormControls.js" as FormControls

// Internal part of FormDesigner (D-311): the property sheet of the selected
// form control. Every committed editor value is one designer edit (one undo
// step with a design document): name, caption, bound field, calculated
// expression, items and row source, subform and its link fields, command
// action, tab pages, the tab page a control sits on, picture source, line
// slant and the geometry.
Tk.Flex {
    id: sheet
    // AGENT-NOTE: `var`, not the owner's type: a part typed with the control
    // that instantiates it is a cyclic type, which qmllint reports as
    // "cannot assign X to X" (the GridCellDelegate / GridFillHandle precedent).
    required property var designer
    readonly property var control: designer.controlById(designer.selectedControlId)
    readonly property string type: String(control.type || "")
    readonly property var fieldChoices: [qsTr("(none)")].concat(designer.fieldNames())
    // "(form)" and every page of every other tab control, as {label, parentId, page}.
    readonly property var placeChoices: {
        const out = [{ label: qsTr("(form)"), parentId: "", page: 0 }]
        const all = designer.controls
        for (let i = 0; i < all.length; ++i) {
            if (all[i].type !== "tabControl" || all[i].id === control.id) continue
            const pages = all[i].pages || []
            for (let p = 0; p < pages.length; ++p)
                out.push({ label: String(all[i].name || all[i].id) + " › " + String(pages[p]), parentId: String(all[i].id), page: p })
        }
        return out
    }
    function set(key, value) { return designer.updateControl(String(control.id), ({ [key]: value })) }
    function setText(key, text) {
        if (String(control[key] ?? "") === text) return true
        return set(key, text.length ? text : undefined)
    }

    objectName: "formPropertySheet"
    visible: !!control.id
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Properties of %1").arg(String(control.name || ""))

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Icon { name: FormControls.iconFor(sheet.type); size: Tk.Theme.size.icon; color: Tk.Theme.color.textMuted }
        Tk.Overline { title: qsTr("Properties"); Tk.Flex.grow: 1 }
        Tk.IconButton {
            objectName: "formControlDelete"
            iconName: "trash-2"
            small: true
            tooltip: qsTr("Delete %1").arg(String(sheet.control.name || ""))
            onClicked: sheet.designer.removeControl(String(sheet.control.id))
        }
    }
    Tk.PropertyRow {
        label: qsTr("Name")
        Tk.TextField { objectName: "formControlName"; small: true; text: String(sheet.control.name || ""); tooltip: qsTr("Control name"); onEditingFinished: sheet.setText("name", text) }
    }
    Tk.PropertyRow {
        visible: FormControls.hasCaption(sheet.type)
        label: qsTr("Caption")
        Tk.TextField { objectName: "formControlCaption"; small: true; text: String(sheet.control.caption || ""); tooltip: qsTr("Caption"); onEditingFinished: sheet.setText("caption", text) }
    }
    Tk.PropertyRow {
        visible: FormControls.isBound(sheet.type)
        label: qsTr("Field")
        Tk.ComboBox {
            objectName: "formControlField"
            small: true
            model: sheet.fieldChoices
            currentIndex: Math.max(0, sheet.fieldChoices.indexOf(String(sheet.control.fieldName || "")))
            tooltip: qsTr("Bound field")
            onActivated: function(index) { sheet.set("fieldName", index === 0 ? undefined : sheet.fieldChoices[index]) }
        }
    }
    Tk.PropertyRow {
        visible: sheet.type === "textBox"
        label: qsTr("Expression")
        hint: qsTr("A calculated control, e.g. =[Price]*[Quantity]")
        Tk.TextField { objectName: "formControlExpression"; small: true; mono: true; text: String(sheet.control.expression || ""); tooltip: qsTr("Calculated expression"); onEditingFinished: sheet.setText("expression", text) }
    }
    Tk.PropertyRow {
        visible: FormControls.hasItems(sheet.type)
        label: qsTr("Items")
        Tk.TextField {
            objectName: "formControlItems"; small: true; tooltip: qsTr("Values, separated by semicolons")
            text: FormControls.listText(sheet.control.items)
            onEditingFinished: if (text !== FormControls.listText(sheet.control.items)) sheet.set("items", FormControls.parseList(text))
        }
    }
    Tk.PropertyRow {
        visible: FormControls.hasItems(sheet.type)
        label: qsTr("Row source")
        Tk.TextField { objectName: "formControlRowSource"; small: true; text: String(sheet.control.rowSource || ""); tooltip: qsTr("Row source the host fills the list from"); onEditingFinished: sheet.setText("rowSource", text) }
    }
    Tk.PropertyRow {
        visible: sheet.type === "subform"
        label: qsTr("Form")
        Tk.TextField { objectName: "formControlFormName"; small: true; text: String(sheet.control.formName || ""); tooltip: qsTr("Child form name"); onEditingFinished: sheet.setText("formName", text) }
    }
    Tk.PropertyRow {
        visible: sheet.type === "subform"
        label: qsTr("Link master")
        Tk.TextField { objectName: "formControlLinkMaster"; small: true; text: String(sheet.control.linkMasterFields || ""); tooltip: qsTr("Link master fields"); onEditingFinished: sheet.setText("linkMasterFields", text) }
    }
    Tk.PropertyRow {
        visible: sheet.type === "subform"
        label: qsTr("Link child")
        Tk.TextField { objectName: "formControlLinkChild"; small: true; text: String(sheet.control.linkChildFields || ""); tooltip: qsTr("Link child fields"); onEditingFinished: sheet.setText("linkChildFields", text) }
    }
    Tk.PropertyRow {
        visible: sheet.type === "commandButton"
        label: qsTr("Action")
        Tk.TextField { objectName: "formControlAction"; small: true; mono: true; text: String(sheet.control.actionId || ""); tooltip: qsTr("Action id the button requests"); onEditingFinished: sheet.setText("actionId", text) }
    }
    Tk.PropertyRow {
        visible: sheet.type === "tabControl"
        label: qsTr("Pages")
        Tk.TextField {
            objectName: "formControlPages"; small: true; tooltip: qsTr("Page names, separated by semicolons")
            text: FormControls.listText(sheet.control.pages)
            onEditingFinished: {
                const pages = FormControls.parseList(text)
                if (pages.length && text !== FormControls.listText(sheet.control.pages)) sheet.set("pages", pages)
            }
        }
    }
    Tk.PropertyRow {
        visible: sheet.type === "image"
        label: qsTr("Picture")
        Tk.TextField { objectName: "formControlSource"; small: true; text: String(sheet.control.source || ""); tooltip: qsTr("Picture source"); onEditingFinished: sheet.setText("source", text) }
    }
    Tk.PropertyRow {
        visible: sheet.type === "line"
        label: qsTr("Slant")
        Tk.Segmented {
            objectName: "formControlSlant"
            small: true
            model: [qsTr("Down"), qsTr("Up")]
            currentIndex: sheet.control.slant === "up" ? 1 : 0
            tooltip: qsTr("Line slant")
            onActivated: function(index) { sheet.set("slant", index === 1 ? "up" : "down") }
        }
    }
    Tk.PropertyRow {
        visible: sheet.type !== "tabControl" && sheet.placeChoices.length > 1
        label: qsTr("On page")
        Tk.ComboBox {
            objectName: "formControlPlace"
            small: true
            model: sheet.placeChoices.map(function(choice) { return choice.label })
            currentIndex: Math.max(0, sheet.placeChoices.findIndex(function(choice) {
                return choice.parentId === String(sheet.control.parentId || "") && (!choice.parentId || choice.page === (Number(sheet.control.page) || 0))
            }))
            tooltip: qsTr("Form or tab page the control is on")
            onActivated: function(index) {
                const choice = sheet.placeChoices[index]
                sheet.designer.updateControl(String(sheet.control.id), choice.parentId ? { parentId: choice.parentId, page: choice.page }
                                                                                      : { parentId: undefined, page: undefined })
            }
        }
    }
    Tk.Grid {
        columns: "1fr 1fr"
        gap: Tk.Theme.space.xs
        Repeater {
            model: [["x", "X"], ["y", "Y"], ["width", "W"], ["height", "H"]]
            delegate: Tk.NumberField {
                required property var modelData
                objectName: "formControl_" + modelData[0]
                small: true
                label: modelData[1]
                from: 0
                to: 20000
                value: Number(sheet.control[modelData[0]]) || 0
                tooltip: qsTr("Control %1").arg(modelData[0])
                onValueModified: function(value) { sheet.set(modelData[0], Math.round(value)) }
            }
        }
    }
}
