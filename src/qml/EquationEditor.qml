// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// This is a semantic source editor surface, not an equation parser or layout
// engine. The host validates source and returns explicit capability/errors.
Item {
    id: control
    property var equation: null // {id,source,linearText,status,error,location,supportedSymbols:[symbol ids]}
    property var symbols: [] // [{id,label,source,tooltip}]
    property string sourceDraft: equation ? equation.source || "" : ""
    property string errorText: ""
    property string tooltip: qsTr("Edit equation source and insert supported symbols")
    readonly property bool available: equation !== null && equation.id !== undefined && !!equation.id
    readonly property bool canCommit: available && equation.supported !== false && errorText.length === 0
    signal sourceChangeRequested(string equationId, string source)
    signal symbolInsertRequested(string equationId, string symbolId, string source, int cursorPosition)
    signal navigateRequested(string equationId, var location)

    onEquationChanged: {
        sourceDraft = equation ? equation.source || "" : ""
        errorText = ""
    }

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function submitSource() {
        if (enabled && canCommit) sourceChangeRequested(equation.id, sourceDraft)
    }
    function insertSymbol(symbol) {
        if (!enabled || !canCommit || !symbol || equation.supportedSymbols
                && equation.supportedSymbols.indexOf(symbol.id) < 0) return
        symbolInsertRequested(equation.id, symbol.id, sourceDraft, sourceField.cursorPosition)
    }

    Tk.Flex {
        id: column
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("EQUATION"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Button {
                objectName: "equationNavigate"
                text: qsTr("Go to")
                iconName: "navigation"
                tooltip: qsTr("Navigate to this equation in the document")
                enabled: control.available && control.equation.status !== "unsupported"
                onClicked: control.navigateRequested(control.equation.id, control.equation.location || null)
            }
        }
        Tk.Caption {
            objectName: "equationError"
            visible: control.errorText.length > 0 || control.equation && (!!control.equation.error || control.equation.supported === false)
            color: Tk.Theme.color.danger
            text: control.errorText.length > 0 ? control.errorText
                  : control.equation ? control.equation.error || qsTr("This equation construct is unsupported by the active document backend.")
                  : qsTr("No equation is selected.")
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            visible: control.available
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: control.symbols
                delegate: Tk.Button {
                    required property var modelData
                    objectName: "equationSymbol_" + modelData.id
                    text: modelData.label
                    tooltip: modelData.tooltip || qsTr("Insert %1 symbol").arg(modelData.label)
                    enabled: control.canCommit && (!control.equation.supportedSymbols
                              || control.equation.supportedSymbols.indexOf(modelData.id) >= 0)
                    onClicked: control.insertSymbol(modelData)
                }
            }
        }
        Tk.TextArea {
            id: sourceField
            objectName: "equationSource"
            visible: control.available
            rows: 4
            mono: true
            text: control.sourceDraft
            placeholderText: qsTr("Equation source")
            tooltip: qsTr("Equation source submitted to the document parser")
            readOnly: control.equation && control.equation.supported === false
            onTextChanged: control.sourceDraft = text
        }
        Tk.Caption {
            objectName: "equationUnsupportedSymbols"
            visible: control.available && control.symbols.some(function(symbol) {
                return control.equation.supportedSymbols
                       && control.equation.supportedSymbols.indexOf(symbol.id) < 0
            })
            color: Tk.Theme.color.warning
            text: qsTr("Some symbol templates are unavailable in the active equation backend.")
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            objectName: "equationLinearAlternative"
            visible: control.available
            text: control.equation
                  ? qsTr("Linear alternative: %1").arg(control.equation.linearText || qsTr("not provided by the host")) : ""
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            color: Tk.Theme.color.textMuted
            text: qsTr("Visual equation layout is supplied by the document host. Source edits are requests and are validated there.")
            wrapMode: Text.WordWrap
        }
        Tk.Button {
            objectName: "equationApply"
            text: qsTr("Apply source")
            iconName: "check"
            variant: "accent"
            tooltip: qsTr("Submit equation source for host validation")
            enabled: control.canCommit
            onClicked: control.submitSource()
        }
    }
}
