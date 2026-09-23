// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the inspector number field. 24px
// (small 20), mono digits, optional inline `label` ("W"), `prefix`/`suffix`
// readouts, ▲▼ step buttons on hover (always visible in Touch), and scrubbing:
// dragging horizontally
// on the label or the field changes the value by `scrubSensitivity` units
// per pixel (Shift ×10, Alt ×0.1). Arrow keys step (Shift ×10); Ctrl+wheel
// steps. `value` is always clamped to [from, to] and rounded to `decimals`.
// `defaultValue`/`reset()` provide explicit reset semantics; mixed state is
// resolved only by a valid user commit. Invalid drafts stay visible and
// `inputInvalid` reports them. `valueModified(value)` fires for user edits
// and reset, while a programmatic `value = x` is silent. objectNames:
// "numberInput", "numberLabel", "numberStepUp", "numberStepDown".
T.Control {
    id: control

    property real value: 0
    property real defaultValue: 0
    property bool mixed: false
    property real from: -1e9
    property real to: 1e9
    property real stepSize: 1
    property int decimals: 0
    property string prefix: ""
    property string suffix: ""
    property string label: ""
    property bool editable: true
    property bool scrub: true
    property real scrubSensitivity: stepSize
    property bool stepButtons: true
    property bool small: false
    property bool error: false
    property bool live: false
    property string tooltip: ""
    readonly property alias inputItem: input
    readonly property bool editing: input.activeFocus
    readonly property bool inputInvalid: _invalidDraft

    signal valueModified(real value)
    signal editingFinished()

    property bool _invalidDraft: false

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real edgePadding: Tk.Theme.space.sm + 2

    function clamp(v) {
        const factor = Math.pow(10, Math.max(0, control.decimals))
        const rounded = Math.round(v * factor) / factor
        return Math.max(control.from, Math.min(control.to, rounded))
    }
    function format(v) {
        return Number(v).toFixed(Math.max(0, control.decimals))
    }
    // User-driven change: clamps, assigns, emits.
    function commit(v) {
        if (!control.editable || !control.enabled) return
        const next = control.clamp(v)
        const wasMixed = control.mixed
        control.mixed = false
        control._invalidDraft = false
        if (next !== control.value || wasMixed) {
            control.value = next
            control.valueModified(next)
        }
        if (!control.editing) {
            control.syncText()
        }
    }
    function step(direction, modifiers) {
        let amount = control.stepSize
        if (modifiers & Qt.ShiftModifier) amount *= 10
        if (modifiers & Qt.AltModifier) amount /= 10
        control.commit(control.value + direction * amount)
        if (control.editing) {
            control.syncText()
            input.selectAll()
        }
    }
    function syncText() {
        input.text = control.mixed ? "" : control.format(control.value)
    }
    function applyInput() {
        if (!control.editable || !control.enabled) {
            control.syncText()
            return
        }
        const parsed = Number(input.text.replace(",", "."))
        if (input.text.trim().length === 0 || isNaN(parsed) || !isFinite(parsed)) {
            control._invalidDraft = true
            return
        }
        control.commit(parsed)
        control.syncText()
    }
    // Explicit user reset operation: unlike assigning `value`, this reports
    // a valueModified signal when it changes the effective value.
    function reset() {
        if (!control.editable || !control.enabled) return
        control._invalidDraft = false
        control.commit(control.defaultValue)
        control.syncText()
    }

    onValueChanged: {
        const clamped = control.clamp(control.value)
        if (clamped !== control.value) {
            control.value = clamped
            return
        }
        if (!control.editing || !control.live) {
            control.syncText()
        }
    }
    onMixedChanged: {
        if (mixed) _invalidDraft = false
        syncText()
    }
    onDecimalsChanged: syncText()
    Component.onCompleted: {
        control.value = control.clamp(control.value)
        control.syncText()
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: edgePadding
    rightPadding: stepColumn.visible ? stepColumn.width + Tk.Theme.space.xs : edgePadding
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.SpinBox
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : control.label
    Accessible.description: control.inputInvalid ? qsTr("Invalid value")
                             : control.mixed ? qsTr("Multiple values")
                             : control.error ? qsTr("Error") : ""
    Accessible.onIncreaseAction: control.step(1, Qt.NoModifier)
    Accessible.onDecreaseAction: control.step(-1, Qt.NoModifier)

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !scrubHandler.active
        delay: 600
    }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.fieldWidth
        implicitHeight: control.controlHeight
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: control.error || control.inputInvalid ? Tk.Theme.color.danger
                    : control.editing ? Tk.Theme.color.inputFocusBorder
                    : control.hovered || scrubHandler.active ? Tk.Theme.color.controlHoverBorder
                    : Tk.Theme.color.inputBorder
        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: control.error || control.inputInvalid ? Tk.Theme.alpha(Tk.Theme.color.danger, 0.4) : Tk.Theme.color.focus
            visible: control.editing
        }
    }

    // AGENT-GUARD: the step buttons are direct children (z 1), not part of
    // `background`: with pointer handlers on the control, Qt visits the
    // control before its z -1 background and a tap parented there is lost.
    Item {
        id: stepColumn
        readonly property bool touchTargets: Tk.Density.modeName === "touch"
        visible: control.stepButtons && control.enabled && control.editable
                 && (touchTargets || control.hovered || control.editing)
        width: touchTargets ? Tk.Theme.size.controlSm * 2
                            : Tk.Theme.size.iconSm + Tk.Theme.space.xs
        z: 1
        anchors.right: parent.right
        anchors.rightMargin: Tk.Theme.space.xs
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: touchTargets ? 0 : Tk.Theme.size.border
        anchors.bottomMargin: touchTargets ? 0 : Tk.Theme.size.border

        Repeater {
            model: 2
            delegate: Item {
                required property int index
                readonly property int direction: index === 0 ? 1 : -1
                objectName: index === 0 ? "numberStepUp" : "numberStepDown"
                width: stepColumn.touchTargets ? stepColumn.width / 2 : stepColumn.width
                height: stepColumn.touchTargets ? stepColumn.height : stepColumn.height / 2
                x: stepColumn.touchTargets ? index * width : 0
                y: stepColumn.touchTargets ? 0 : index * height
                Accessible.role: Accessible.Button
                Accessible.name: direction > 0 ? qsTr("Increase value") : qsTr("Decrease value")
                Accessible.onPressAction: control.step(direction, Qt.NoModifier)
                Rectangle {
                    anchors.fill: parent
                    radius: Tk.Theme.radius.xs
                    color: stepHover.hovered ? Tk.Theme.color.hover : "transparent"
                }
                Tk.Icon {
                    anchors.centerIn: parent
                    name: index === 0 ? "chevron-up" : "chevron-down"
                    size: Tk.Theme.size.iconSm
                    color: stepHover.hovered ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                }
                HoverHandler { id: stepHover }
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: control.step(direction, point.modifiers)
                }
            }
        }
    }

    contentItem: Item {
        implicitWidth: (labelText.visible ? labelMetrics.advanceWidth + Tk.Theme.space.sm : 0)
                       + prefixMetrics.advanceWidth + input.implicitWidth + suffixMetrics.advanceWidth
                       + Tk.Theme.space.xs * 2
        implicitHeight: input.implicitHeight

        // Text with elision needs a width before it can calculate its painted
        // width.  Measuring independently avoids a zero-width binding loop
        // for NumberField's inline label, prefix, and suffix.
        TextMetrics {
            id: labelMetrics
            font: labelText.font
            text: control.label
        }
        TextMetrics {
            id: prefixMetrics
            font: prefixText.font
            text: control.prefix
        }
        TextMetrics {
            id: suffixMetrics
            font: suffixText.font
            text: control.suffix
        }

        Tk.Caption {
            id: labelText
            objectName: "numberLabel"
            visible: control.label.length > 0
            text: control.label
            color: scrubHandler.active ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
            font.weight: Font.DemiBold
            height: parent.height
            width: visible ? Math.ceil(labelMetrics.advanceWidth) : 0
        }
        Tk.Caption {
            id: prefixText
            visible: control.prefix.length > 0
            text: control.prefix
            x: labelText.width + (labelText.visible ? Tk.Theme.space.sm : 0)
            width: visible ? Math.ceil(prefixMetrics.advanceWidth) + 1 : 0
            height: parent.height
        }
        TextInput {
            id: input
            objectName: "numberInput"
            x: prefixText.x + prefixText.width + (prefixText.visible ? Tk.Theme.space.xs : 0)
            width: Math.max(Tk.Theme.space.xl, parent.width - x - suffixText.width - (suffixText.visible ? Tk.Theme.space.xs : 0))
            height: parent.height
            verticalAlignment: TextInput.AlignVCenter
            color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
            selectionColor: Tk.Theme.color.selection
            selectedTextColor: Tk.Theme.color.text
            font.family: Tk.Theme.font.monoFamily
            font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
            renderType: Text.NativeRendering
            property bool draftEdited: false
            readOnly: !control.editable || !control.enabled
            selectByMouse: true
            activeFocusOnTab: control.editable
            clip: true
            onActiveFocusChanged: {
                if (activeFocus) {
                    draftEdited = false
                    selectAll()
                } else {
                    if (draftEdited) control.applyInput()
                    draftEdited = false
                    control.editingFinished()
                }
            }
            onAccepted: {
                control.applyInput()
                draftEdited = false
                selectAll()
            }
            onTextEdited: {
                draftEdited = true
                if (control.live) {
                    const trimmed = text.trim()
                    const parsed = Number(text.replace(",", "."))
                    if (trimmed.length > 0 && isFinite(parsed)) control.commit(parsed)
                    else control._invalidDraft = true
                }
            }
            Keys.onUpPressed: function(event) { if (control.editable && control.enabled) control.step(1, event.modifiers); event.accepted = true }
            Keys.onDownPressed: function(event) { if (control.editable && control.enabled) control.step(-1, event.modifiers); event.accepted = true }
            Keys.onEscapePressed: function(event) { draftEdited = false; control._invalidDraft = false; control.syncText(); selectAll(); event.accepted = true }
        }
        Text {
            objectName: "numberMixedPlaceholder"
            x: input.x
            y: input.y
            width: input.width
            height: input.height
            visible: control.mixed && input.text.length === 0
            text: qsTr("Mixed")
            color: Tk.Theme.color.textMuted
            font.family: Tk.Theme.font.monoFamily
            font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
            verticalAlignment: Text.AlignVCenter
            renderType: Text.NativeRendering
        }
        Tk.Caption {
            id: suffixText
            visible: control.suffix.length > 0
            text: control.suffix
            anchors.right: parent.right
            width: visible ? Math.ceil(suffixMetrics.advanceWidth) + 1 : 0
            height: parent.height
        }
    }

    // Scrubbing: a horizontal drag anywhere on the field that did not start
    // as a text selection. The label is the intended handle; the input area
    // scrubs too unless it is being edited.
    HoverHandler {
        id: scrubHover
        enabled: control.scrub && control.enabled && control.editable
        cursorShape: (labelText.visible && labelText.contains(labelText.mapFromItem(control, point.position)))
                     || !control.editing ? Qt.SizeHorCursor : Qt.IBeamCursor
    }
    DragHandler {
        id: scrubHandler
        target: null
        enabled: control.scrub && control.enabled && control.editable && !control.editing
        acceptedButtons: Qt.LeftButton
        xAxis.enabled: true
        yAxis.enabled: false
        dragThreshold: Tk.Theme.space.xs
        property real startValue: 0
        onActiveChanged: {
            if (active) {
                startValue = control.value
            } else {
                control.editingFinished()
            }
        }
        onTranslationChanged: {
            if (!active) return
            let perPixel = control.scrubSensitivity
            const modifiers = scrubHandler.centroid.modifiers
            if (modifiers & Qt.ShiftModifier) perPixel *= 10
            if (modifiers & Qt.AltModifier) perPixel /= 10
            control.commit(startValue + translation.x * perPixel)
        }
    }
    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        enabled: control.enabled && control.editable
        onWheel: function(event) {
            control.step(event.angleDelta.y > 0 ? 1 : -1, event.modifiers & ~Qt.ControlModifier)
        }
    }
}
