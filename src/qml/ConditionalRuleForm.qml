// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#conditionalformateditor): the draft-rule
// form of Tk.ConditionalFormatEditor. Every field reads and writes the
// editor's draft properties; it keeps no state of its own. Field objectNames
// (cfRanges, cfType, cfOperator, cfValue1, cfValue2, cfText, cfBold,
// cfItalic, cfUnderline, cfStrikeout, cfTextColor, cfFillColor,
// cfClearColors, cfMinColor, cfMidColor, cfMaxColor, cfThreeColor,
// cfStopIfTrue) are part of the editor contract.
Tk.Grid {
    id: form

    // AGENT-NOTE: var, not Tk.ConditionalFormatEditor: a property typed with the
    // type that instantiates this one is a cycle that crashes compiled lookups.
    property var editor: null

    columns: "auto 1fr"
    columnGap: Tk.Theme.space.sm
    rowGap: Tk.Theme.space.xs

    Tk.Label { text: qsTr("Applies to"); color: Tk.Theme.color.textMuted }
    Tk.TextField {
        objectName: "cfRanges"
        small: true
        text: form.editor ? form.editor.ranges : ""
        placeholderText: qsTr("A1:B10 C3")
        tooltip: qsTr("Ranges the rule formats, separated by spaces")
        onTextEdited: form.editor.ranges = text
    }
    Tk.Label { text: qsTr("Format when"); color: Tk.Theme.color.textMuted }
    Tk.ComboBox {
        objectName: "cfType"
        small: true
        model: form.editor ? form.editor.typeModel : []
        textRole: "text"
        valueRole: "value"
        currentIndex: form.editor ? form.editor.indexOfValue(form.editor.typeModel, form.editor.type) : 0
        tooltip: qsTr("Kind of condition")
        onActivated: form.editor.type = currentValue
    }
    Tk.Label { visible: !!form.editor && form.editor.compares; text: qsTr("Value is"); color: Tk.Theme.color.textMuted }
    Tk.ComboBox {
        objectName: "cfOperator"
        visible: !!form.editor && form.editor.compares
        small: true
        model: form.editor ? form.editor.operatorModel : []
        textRole: "text"
        valueRole: "value"
        currentIndex: form.editor ? form.editor.indexOfValue(form.editor.operatorModel, form.editor.operator) : 0
        tooltip: qsTr("Comparison")
        onActivated: form.editor.operator = currentValue
    }
    Tk.Label {
        visible: !!form.editor && (form.editor.compares || form.editor.type === "expression")
        text: form.editor && form.editor.type === "expression" ? qsTr("Formula") : qsTr("Value")
        color: Tk.Theme.color.textMuted
    }
    Tk.TextField {
        objectName: "cfValue1"
        visible: !!form.editor && (form.editor.compares || form.editor.type === "expression")
        small: true
        mono: true
        text: form.editor ? form.editor.value1 : ""
        placeholderText: form.editor && form.editor.type === "expression" ? "=A1>0" : qsTr("Value or =formula")
        tooltip: qsTr("Value or formula, relative to the first cell of the first range")
        onTextEdited: form.editor.value1 = text
    }
    Tk.Label { visible: !!form.editor && form.editor.twoValues; text: qsTr("and"); color: Tk.Theme.color.textMuted }
    Tk.TextField {
        objectName: "cfValue2"
        visible: !!form.editor && form.editor.twoValues
        small: true
        mono: true
        text: form.editor ? form.editor.value2 : ""
        tooltip: qsTr("Second value of the range")
        onTextEdited: form.editor.value2 = text
    }
    Tk.Label { visible: !!form.editor && form.editor.type === "containsText"; text: qsTr("Text"); color: Tk.Theme.color.textMuted }
    Tk.TextField {
        objectName: "cfText"
        visible: !!form.editor && form.editor.type === "containsText"
        small: true
        text: form.editor ? form.editor.text : ""
        tooltip: qsTr("Text to look for (any case)")
        onTextEdited: form.editor.text = text
    }
    Tk.Label { visible: !!form.editor && form.editor.styled; text: qsTr("Style"); color: Tk.Theme.color.textMuted }
    Tk.Flex {
        visible: !!form.editor && form.editor.styled
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        Repeater {
            model: [{ key: "bold", icon: "bold", tip: qsTr("Bold") }, { key: "italic", icon: "italic", tip: qsTr("Italic") },
                    { key: "underline", icon: "underline", tip: qsTr("Underline") },
                    { key: "strikeout", icon: "strikethrough", tip: qsTr("Strikethrough") }]
            delegate: Tk.IconButton {
                required property var modelData
                objectName: "cf" + modelData.key.charAt(0).toUpperCase() + modelData.key.slice(1)
                small: true
                iconName: modelData.icon
                tooltip: modelData.tip
                checked: !!form.editor && form.editor.style[modelData.key] === true
                Accessible.checkable: true
                onClicked: form.editor.setStyle(modelData.key, !checked)
            }
        }
        Tk.Button {
            objectName: "cfClearColors"
            small: true
            variant: "ghost"
            text: qsTr("No colors")
            onClicked: {
                form.editor.setStyle("textColor", "")
                form.editor.setStyle("fillColor", "")
            }
        }
    }
    Tk.Label { visible: !!form.editor && form.editor.styled; text: qsTr("Text color"); color: Tk.Theme.color.textMuted }
    Tk.ColorSwatches {
        objectName: "cfTextColor"
        visible: !!form.editor && form.editor.styled
        colors: form.editor ? form.editor.documentColors : []
        customButton: false
        current: form.editor && form.editor.style.textColor ? form.editor.style.textColor : "transparent"
        onSelected: function(color) { form.editor.setStyle("textColor", color.toString()) }
    }
    Tk.Label { visible: !!form.editor && form.editor.styled; text: qsTr("Fill"); color: Tk.Theme.color.textMuted }
    Tk.ColorSwatches {
        objectName: "cfFillColor"
        visible: !!form.editor && form.editor.styled
        colors: form.editor ? form.editor.documentColors : []
        customButton: false
        current: form.editor && form.editor.style.fillColor ? form.editor.style.fillColor : "transparent"
        onSelected: function(color) { form.editor.setStyle("fillColor", color.toString()) }
    }
    Tk.Label { visible: !!form.editor && form.editor.type === "colorScale"; text: qsTr("Lowest"); color: Tk.Theme.color.textMuted }
    Tk.ColorSwatches {
        objectName: "cfMinColor"
        visible: !!form.editor && form.editor.type === "colorScale"
        colors: form.editor ? form.editor.documentColors : []
        customButton: false
        current: form.editor && form.editor.minColor ? form.editor.minColor : "transparent"
        onSelected: function(color) { form.editor.minColor = color.toString() }
    }
    Tk.Label { visible: !!form.editor && form.editor.type === "colorScale"; text: qsTr("Midpoint"); color: Tk.Theme.color.textMuted }
    Tk.Flex {
        visible: !!form.editor && form.editor.type === "colorScale"
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.CheckBox {
            objectName: "cfThreeColor"
            small: true
            text: qsTr("Three colors")
            checked: !!form.editor && form.editor.midColor.length > 0
            onToggled: form.editor.midColor = checked ? form.editor.documentColors[2].color : ""
        }
        Tk.ColorSwatches {
            objectName: "cfMidColor"
            visible: !!form.editor && form.editor.midColor.length > 0
            colors: form.editor ? form.editor.documentColors : []
            customButton: false
            current: form.editor && form.editor.midColor ? form.editor.midColor : "transparent"
            onSelected: function(color) { form.editor.midColor = color.toString() }
        }
    }
    Tk.Label {
        visible: !!form.editor && (form.editor.type === "colorScale" || form.editor.type === "dataBar")
        text: form.editor && form.editor.type === "dataBar" ? qsTr("Bar") : qsTr("Highest")
        color: Tk.Theme.color.textMuted
    }
    Tk.ColorSwatches {
        objectName: "cfMaxColor"
        visible: !!form.editor && (form.editor.type === "colorScale" || form.editor.type === "dataBar")
        colors: form.editor ? form.editor.documentColors : []
        customButton: false
        current: form.editor && form.editor.maxColor ? form.editor.maxColor : "transparent"
        onSelected: function(color) { form.editor.maxColor = color.toString() }
    }
    Tk.Label { text: ""; visible: stopCheck.visible }
    Tk.CheckBox {
        id: stopCheck
        objectName: "cfStopIfTrue"
        visible: !!form.editor && form.editor.styled
        small: true
        text: qsTr("Stop evaluating later rules when this one matches")
        checked: !!form.editor && form.editor.stopIfTrue
        onToggled: form.editor.stopIfTrue = checked
    }
}
