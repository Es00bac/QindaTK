// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Internal part of FormDesigner (D-311): one interactive form control of a
// previewed or edited record — a text, combo, list or check box bound to
// `fieldName` (values change through FormDesigner.setFieldValue, so only
// edit mode writes and every rule there applies), a calculated text box
// (`expression`, shown through FormDesigner.expressionEvaluator), a command
// button (controlCommandRequested), a subform (the host's subformComponent)
// or a tab control's page strip. Static types are FormControlShape's.
Item {
    id: live
    required property var control
    // AGENT-NOTE: `var`, not the owner's type: a part typed with the control
    // that instantiates it is a cyclic type, which qmllint reports as
    // "cannot assign X to X" (the GridCellDelegate / GridFillHandle precedent).
    required property var designer
    property int currentPage: 0
    signal pageChosen(int page)
    readonly property string type: String(control.type || "")
    readonly property string field: String(control.fieldName || "")
    readonly property bool calculated: String(control.expression || "").length > 0
    readonly property var value: field.length ? designer.workingRecord[field] : undefined
    readonly property bool writable: designer.mode === "edit" && field.length > 0 && !calculated
    readonly property string caption: String(control.caption || control.name || "")
    readonly property var items: control.items || []

    function numericField() {
        const fields = designer.schemaSnapshot.fields || []
        for (let i = 0; i < fields.length; ++i)
            if (String(fields[i].name) === field) return /int|real|num|dec|float|double/i.test(String(fields[i].type || ""))
        return false
    }
    function display() {
        if (calculated) {
            const evaluate = designer.expressionEvaluator
            return typeof evaluate === "function" ? String(evaluate(String(control.expression), designer.workingRecord) ?? "")
                                                  : String(control.expression)
        }
        return value === undefined || value === null ? "" : String(value)
    }

    Loader {
        anchors.fill: parent
        sourceComponent: live.type === "textBox" ? textBox : live.type === "comboBox" ? comboBox
            : live.type === "listBox" ? listBox : live.type === "checkBox" ? checkBox
            : live.type === "commandButton" ? commandButton : live.type === "subform" ? subform
            : live.type === "tabControl" ? tabControl : null
    }

    Component {
        id: textBox
        Tk.TextField {
            objectName: "formLive_" + live.control.id
            text: live.display()
            readOnly: !live.writable
            mono: live.calculated && typeof live.designer.expressionEvaluator !== "function"
            tooltip: live.caption
            onEditingFinished: if (live.writable && text !== live.display()) live.designer.setFieldValue(live.field, text)
        }
    }
    Component {
        id: comboBox
        Tk.ComboBox {
            objectName: "formLive_" + live.control.id
            model: live.items
            currentIndex: live.items.indexOf(live.display())
            enabled: live.writable
            tooltip: live.caption
            onActivated: function(index) { live.designer.setFieldValue(live.field, live.items[index]) }
        }
    }
    Component {
        id: listBox
        Rectangle {
            objectName: "formLive_" + live.control.id
            color: Tk.Theme.color.inputBg
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.inputBorder
            clip: true
            Accessible.role: Accessible.List
            Accessible.name: live.caption
            ListView {
                anchors.fill: parent
                anchors.margins: Tk.Theme.size.border
                model: live.items
                boundsBehavior: Flickable.StopAtBounds
                delegate: Tk.ListRow {
                    required property var modelData
                    required property int index
                    objectName: "formLiveItem_" + index
                    width: ListView.view.width
                    small: true
                    text: String(modelData)
                    selected: String(modelData) === live.display()
                    enabled: live.writable
                    onClicked: live.designer.setFieldValue(live.field, String(modelData))
                }
            }
        }
    }
    Component {
        id: checkBox
        Tk.CheckBox {
            objectName: "formLive_" + live.control.id
            text: String(live.control.caption || "")
            checked: !!live.value
            enabled: live.writable
            tooltip: live.caption
            // SQLite has no boolean: a number column stores 1 or 0.
            onToggled: live.designer.setFieldValue(live.field, live.numericField() ? (checked ? 1 : 0) : checked)
        }
    }
    Component {
        id: commandButton
        Tk.Button {
            objectName: "formLive_" + live.control.id
            text: String(live.control.caption || "")
            tooltip: String(live.control.actionId || live.caption)
            onClicked: live.designer.controlCommandRequested(String(live.control.id), String(live.control.actionId || ""))
        }
    }
    // AGENT-CONTRACT (docs/forms-reports.md#form-controls): the host's
    // subformComponent is created in the frame; of formName, controlId,
    // linkMasterFields, linkChildFields and masterRecord it receives those
    // its root declares. Without one the frame names the child form.
    Component {
        id: subform
        Rectangle {
            objectName: "formLive_" + live.control.id
            color: Tk.Theme.color.panel
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.divider
            Loader {
                id: childForm
                anchors.fill: parent
                anchors.margins: Tk.Theme.size.border
                sourceComponent: live.designer.subformComponent
                onLoaded: {
                    const values = { formName: String(live.control.formName || ""), controlId: String(live.control.id),
                                     linkMasterFields: String(live.control.linkMasterFields || ""),
                                     linkChildFields: String(live.control.linkChildFields || ""),
                                     masterRecord: live.designer.workingRecord }
                    for (const name in values) if (name in item) item[name] = values[name]
                }
            }
            Tk.Caption {
                visible: !childForm.item
                anchors.centerIn: parent
                text: qsTr("Subform: %1").arg(String(live.control.formName || qsTr("none")))
            }
        }
    }
    Component {
        id: tabControl
        Item {
            objectName: "formLive_" + live.control.id
            Tk.TabStrip {
                objectName: "formLiveTabs"
                width: parent.width
                model: live.control.pages || []
                currentIndex: live.currentPage
                small: true
                onTabActivated: function(index) { live.pageChosen(index) }
            }
            Rectangle {
                y: Tk.Theme.size.tab
                width: parent.width
                height: Math.max(0, parent.height - y)
                color: "transparent"
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.divider
            }
        }
    }
}
