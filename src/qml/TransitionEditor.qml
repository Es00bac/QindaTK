// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: two modes (docs/controls.md "TransitionEditor",
// docs/sequence-edits.md "Transitions"). Host mode: a host-owned descriptor;
// edits are requests, never local mutation. Editor mode (`editor` set): a
// Tk.SequenceEditor supplies the descriptor of its selected incoming clip,
// applies each edit as one TimelineModel transaction and previews by looping
// its player over the transition window; the request signals are not emitted.
Item {
    id: root
    property var transition: null // {transitionId,leftClipId,rightClipId,type,durationTicks,alignment,headHandleTicks,tailHandleTicks,parameters}
    property Tk.SequenceEditor editor: null
    property string durationTicks: "0"
    property string type: "dissolve"
    property string alignment: "center" // start, center, end
    property string previewState: "idle" // idle, pending, ready, failed
    property string statusText: ""
    property string errorText: ""
    property string tooltip: qsTr("Transition settings")
    property string _serial: "0"
    property bool hovered: hover.hovered

    signal parameterEditRequested(string requestId, string transitionId, string parameter, string value)
    signal previewRequested(string requestId, string transitionId)
    signal cancelPreviewRequested(string requestId, string transitionId)

    readonly property bool editorMode: root.editor !== null
    readonly property var effectiveTransition: root.editorMode ? root.editor.transition : root.transition
    readonly property string effectivePreviewState: root.editorMode ? root.editor.transitionPreviewState : root.previewState
    readonly property string effectiveError: root.editorMode ? root.editor.transitionError : root.errorText
    readonly property bool hasTransition: root.validSnapshot && root.effectiveTransition.hasTransition !== false
    readonly property bool validSnapshot: root.effectiveTransition !== null && root.effectiveTransition !== undefined
        && String(root.effectiveTransition.transitionId || "").length > 0
        && String(root.effectiveTransition.leftClipId || "").length > 0
        && String(root.effectiveTransition.rightClipId || "").length > 0
    readonly property bool validDuration: /^[0-9]+$/.test(root.durationTicks) && root.durationTicks !== "0"
    // Editor mode: offline media keeps its recorded handles (D-118), so only
    // the handle arithmetic gates a preview; host mode also needs the media.
    readonly property bool handlesAvailable: root.validSnapshot
        && (root.editorMode || root.effectiveTransition.missingMedia !== true) && root.durationFitsHandles()
    readonly property var parameterDescriptors: root.validSnapshot && root.effectiveTransition.parameters
        ? root.effectiveTransition.parameters : []

    objectName: "transitionEditor"
    implicitWidth: Tk.Theme.size.panelMinWidth * 1.7
    implicitHeight: Tk.Theme.size.controlLg * 5 + Tk.Theme.space.lg * 3
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip
    Accessible.description: root.effectiveError.length ? root.effectiveError
        : !validSnapshot ? qsTr("No host transition is selected.")
        : !handlesAvailable ? qsTr("The requested transition exceeds available clip handles.")
        : qsTr("Transition settings use exact timeline ticks and host validation.")

    function compareDecimal(a, b) {
        let left = String(a), right = String(b)
        const leftNegative = left.charAt(0) === "-", rightNegative = right.charAt(0) === "-"
        if (leftNegative !== rightNegative) return leftNegative ? -1 : 1
        if (leftNegative) { left = left.slice(1); right = right.slice(1) }
        left = left.replace(/^0+(?=\d)/, ""); right = right.replace(/^0+(?=\d)/, "")
        const cmp = left.length !== right.length ? (left.length < right.length ? -1 : 1)
            : left === right ? 0 : left < right ? -1 : 1
        return leftNegative ? -cmp : cmp
    }
    function durationFitsHandles() {
        if (!root.validSnapshot || !root.validDuration) return false
        if (!root.editorMode && root.effectiveTransition.missingMedia === true) return false
        const head = String(root.effectiveTransition.headHandleTicks || "0")
        const tail = String(root.effectiveTransition.tailHandleTicks || "0")
        if (!/^[0-9]+$/.test(head) || !/^[0-9]+$/.test(tail)) return false
        const needHead = root.alignment === "start" ? "0" : root.alignment === "center" ? halfUp(root.durationTicks) : root.durationTicks
        const needTail = root.alignment === "end" ? "0" : root.alignment === "center" ? halfDown(root.durationTicks) : root.durationTicks
        return compareDecimal(head, needHead) >= 0 && compareDecimal(tail, needTail) >= 0
    }
    // Decimal-string half split without coercing a potentially long tick count.
    function halfParts(value) {
        const digits = String(value).replace(/^0+(?=\d)/, "")
        let out = "", carry = 0
        for (let i = 0; i < digits.length; ++i) {
            const part = carry * 10 + Number(digits.charAt(i))
            out += String(Math.floor(part / 2)); carry = part % 2
        }
        out = out.replace(/^0+(?=\d)/, "")
        return {floor: out, ceil: carry ? incrementDecimal(out) : out}
    }
    function incrementDecimal(value) {
        const chars = String(value).split("")
        for (let i = chars.length - 1; i >= 0; --i) {
            if (chars[i] !== "9") { chars[i] = String(Number(chars[i]) + 1); return chars.join("") }
            chars[i] = "0"
        }
        return "1" + chars.join("")
    }
    function halfUp(value) { return halfParts(value).ceil }
    function halfDown(value) { return halfParts(value).floor }
    function requestId(kind) {
        root._serial = String(Number(root._serial) + 1)
        return "transition:" + kind + ":" + root._serial
    }
    function edit(parameter, value) {
        if (!enabled || !validSnapshot || String(value).length === 0) return false
        if (root.editorMode) return root.editor.editTransition(parameter, String(value)).ok === true
        parameterEditRequested(requestId("edit"), String(effectiveTransition.transitionId), parameter, String(value))
        return true
    }
    function requestPreview() {
        if (!enabled || !validSnapshot || !handlesAvailable) return false
        if (root.editorMode) return root.hasTransition && root.editor.previewTransition().ok === true
        previewRequested(requestId("preview"), String(effectiveTransition.transitionId))
        return true
    }
    function cancelPreview() {
        if (!enabled || !validSnapshot || effectivePreviewState !== "pending") return false
        if (root.editorMode) return root.editor.stopTransitionPreview()
        cancelPreviewRequested(requestId("cancel"), String(effectiveTransition.transitionId))
        return true
    }
    function removeTransition() {
        if (!enabled || !root.editorMode || !root.hasTransition) return false
        return root.editor.removeTransition(String(effectiveTransition.transitionId)).ok === true
    }
    // Editor mode: the fields show the committed (frame-exact) values.
    function _syncFromEditor() {
        if (!root.editorMode || !root.validSnapshot || !root.hasTransition) return
        root.type = String(root.effectiveTransition.type || "dissolve")
        root.durationTicks = String(root.effectiveTransition.durationTicks || "0")
        root.alignment = String(root.effectiveTransition.alignment || "center")
    }
    Connections {
        target: root.editor
        function onTransitionChanged() { root._syncFromEditor() }
    }
    onEditorChanged: root._syncFromEditor()

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Overline { text: qsTr("TRANSITION") }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { text: root.effectiveTransition ? String(root.effectiveTransition.transitionId || "—") : qsTr("No selection"); color: Tk.Theme.color.textMuted }
        }
        Tk.Flex {
            width: parent.width
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Between") }
            Tk.Label {
                text: root.validSnapshot ? String(root.effectiveTransition.leftClipId) + "  →  " + String(root.effectiveTransition.rightClipId) : "—"
                color: Tk.Theme.color.text
                elide: Text.ElideMiddle
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
        }
        Tk.Grid {
            width: parent.width
            columns: Tk.Theme.size.fieldWidth + "px 1fr"
            columnGap: Tk.Theme.space.md
            rowGap: Tk.Theme.space.sm
            alignItems: Tk.Grid.Center
            Tk.Caption { text: qsTr("Type") }
            Tk.Segmented {
                objectName: "transitionType"
                model: [{text: qsTr("Dissolve"), value: "dissolve"}, {text: qsTr("Wipe"), value: "wipe"},
                        {text: qsTr("Dip"), value: "dip"}]
                currentIndex: Math.max(0, ["dissolve", "wipe", "dip"].indexOf(root.type))
                tooltip: qsTr("Transition type")
                onActivated: function(index) { root.type = String(currentValue); root.edit("type", root.type) }
            }
            Tk.Caption { text: qsTr("Duration") }
            Tk.TextField {
                id: durationField
                objectName: "transitionDurationTicks"
                text: root.durationTicks
                mono: true
                error: !/^[0-9]+$/.test(text) || text === "0"
                tooltip: qsTr("Positive exact duration in timeline ticks")
                onTextEdited: root.durationTicks = text
                onAccepted: if (root.validDuration) root.edit("durationTicks", root.durationTicks)
            }
            Tk.Caption { text: qsTr("Alignment") }
            Tk.Segmented {
                objectName: "transitionAlignment"
                model: [{text: qsTr("Start"), value: "start"}, {text: qsTr("Center"), value: "center"}, {text: qsTr("End"), value: "end"}]
                currentIndex: Math.max(0, ["start", "center", "end"].indexOf(root.alignment))
                tooltip: qsTr("Transition alignment")
                onActivated: function(index) { root.alignment = String(currentValue); root.edit("alignment", root.alignment) }
            }
        }
        Repeater {
            model: root.parameterDescriptors
            delegate: Tk.Flex {
                id: parameterRow
                required property var modelData
                width: parent.width
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Caption {
                    text: String(modelData.label || modelData.id || qsTr("Parameter"))
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.TextField {
                    id: parameterValueField
                    objectName: "transitionParameter_" + String(modelData.id || "unknown")
                    text: String(modelData.valueText !== undefined ? modelData.valueText : modelData.value || "")
                    mono: true
                    enabled: modelData.editable !== false
                    tooltip: qsTr("Host parameter %1").arg(String(modelData.label || modelData.id || ""))
                    Accessible.name: String(modelData.label || modelData.id || qsTr("Transition parameter"))
                    width: Tk.Theme.size.fieldWidth
                    onAccepted: root.edit("parameter:" + String(modelData.id), text)
                }
                Tk.Button {
                    text: qsTr("Set")
                    small: true
                    enabled: modelData.editable !== false
                    tooltip: qsTr("Request host update for this parameter")
                    onClicked: root.edit("parameter:" + String(modelData.id), parameterValueField.text)
                }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Badge {
                text: root.handlesAvailable ? qsTr("Handles available") : qsTr("Insufficient handles")
                variant: root.handlesAvailable ? "success" : "warning"
                tooltip: qsTr("Head handle %1 ticks · tail handle %2 ticks")
                    .arg(root.effectiveTransition ? String(root.effectiveTransition.headHandleTicks || "0") : "0")
                    .arg(root.effectiveTransition ? String(root.effectiveTransition.tailHandleTicks || "0") : "0")
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button {
                objectName: "transitionRemove"
                visible: root.editorMode
                text: qsTr("Remove")
                enabled: root.hasTransition
                tooltip: qsTr("Remove the transition; the cut stays")
                onClicked: root.removeTransition()
            }
            Tk.Button {
                objectName: "transitionPreview"
                text: qsTr("Preview")
                iconName: "play"
                enabled: root.validSnapshot && root.handlesAvailable && (!root.editorMode || root.hasTransition)
                tooltip: qsTr("Play the transition window from the rendered program")
                onClicked: root.requestPreview()
            }
            Tk.Button {
                objectName: "transitionCancelPreview"
                text: qsTr("Cancel")
                enabled: root.effectivePreviewState === "pending"
                tooltip: qsTr("Stop the transition preview")
                onClicked: root.cancelPreview()
            }
        }
        Tk.Caption {
            objectName: "transitionStatus"
            visible: root.statusText.length > 0 || root.effectiveError.length > 0 || !root.handlesAvailable
                || (root.validSnapshot && root.effectiveTransition.missingMedia === true)
            color: root.effectiveError.length > 0 || !root.handlesAvailable ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            text: root.effectiveError.length > 0 ? root.effectiveError
                : !root.validSnapshot ? qsTr("Transition data is unavailable.")
                : root.effectiveTransition.missingMedia === true && root.editorMode
                    ? qsTr("Source media is offline; the transition keeps its recorded handles.")
                : root.effectiveTransition.missingMedia === true ? qsTr("Source media is missing; transition handles cannot be verified.")
                : !root.handlesAvailable ? qsTr("The selected duration and alignment exceed available handles.")
                : root.statusText
            wrapMode: Text.WordWrap
        }
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && root.hovered }
}
