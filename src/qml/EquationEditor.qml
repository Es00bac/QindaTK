// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Visual and source editing of one equation (OFF-09). The visual view
// (Tk.EquationCanvas) edits the expression tree: a caret and selection in its
// slots, typing, ^ _ / for scripts and fractions, and palette templates. The
// source view edits the TeX-subset text. Both write `sourceDraft` as
// canonical source, so the two round-trip; the host validates the draft and
// applies it with sourceChangeRequested.
Item {
    id: control
    property var equation: null // {id,source,linearText,status,error,location,supportedSymbols:[symbol ids]}
    property var symbols: [] // [{id,label,source,tooltip}]
    property string sourceDraft: equation ? equation.source || "" : ""
    property string errorText: ""
    property string tooltip: qsTr("Edit the equation visually or as source and insert supported symbols")
    // Where palette symbols go: "visual" (the caret in the tree) or "source".
    property string activeView: "visual"
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
        if (!enabled || !available || !symbol || equation.supportedSymbols
                && equation.supportedSymbols.indexOf(symbol.id) < 0) return
        if (activeView === "visual") {
            visualView.insertSymbol(symbol.id) // the tree edit writes sourceDraft
            visualView.forceActiveFocus()
            return
        }
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
                    enabled: control.available && control.equation.supported !== false
                             && (!control.equation.supportedSymbols
                                 || control.equation.supportedSymbols.indexOf(modelData.id) >= 0)
                    onClicked: control.insertSymbol(modelData)
                }
            }
        }
        Rectangle {
            objectName: "equationVisualFrame"
            visible: control.available
            implicitHeight: Math.max(visualView.implicitHeight, Tk.Theme.size.control * 3)
            color: Tk.Theme.color.inputBg
            radius: Tk.Theme.radius.sm
            border.width: Tk.Theme.size.border
            border.color: visualView.activeFocus ? Tk.Theme.color.inputFocusBorder : Tk.Theme.color.inputBorder
            Tk.EquationCanvas {
                id: visualView
                objectName: "equationVisual"
                anchors.fill: parent
                source: control.sourceDraft
                readOnly: !control.enabled || !!control.equation && control.equation.supported === false
                fontSizePt: Tk.Theme.font.display * 0.75 // px to pt at 96 dpi
                padding: Tk.Theme.space.sm
                inkColor: Tk.Theme.color.text
                caretColor: Tk.Theme.color.accent
                selectionColor: Tk.Theme.color.accentSubtle
                placeholderColor: Tk.Theme.color.textMuted
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Equation, visual view")
                Accessible.description: linearText
                onActiveFocusChanged: if (activeFocus) control.activeView = "visual"
                onEdited: function(source) { control.sourceDraft = source }
                Tk.ToolTip {
                    text: qsTr("Type to edit: ^ superscript, _ subscript, / fraction; arrow keys move between slots")
                    visible: visualHover.hovered
                }
                HoverHandler { id: visualHover }
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
            onActiveFocusChanged: if (activeFocus) control.activeView = "source"
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
            objectName: "equationHelp"
            visible: control.available
            color: Tk.Theme.color.textMuted
            text: qsTr("Edit in the visual view (^ superscript, _ subscript, / fraction, arrows between slots) or in the source; Apply sends the source to the document.")
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
