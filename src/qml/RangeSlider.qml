// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Two handles on one track, for a band rather than a point.
//
// AGENT-CONTRACT (docs/controls-media.md): matches Slider's metrics — a 2px
// track, a 10px handle (8px small), in a 16px (14px) row — so a range and a
// value sit on the same inspector grid. `rangeModified(first, second)` fires
// on user moves from either handle.
//
// AGENT-NOTE: the handles are deliberately allowed to meet and cross-clamp
// rather than being blocked at each other. A user dragging the upper handle
// down past the lower one means "I want this band to start here", and a slider
// that simply refuses feels broken; T.RangeSlider's own clamping keeps first
// <= second, so the band collapses rather than inverting.
T.RangeSlider {
    id: control

    property bool showValues: false
    property int decimals: 0
    property string suffix: ""
    property bool small: false
    property string tooltip: ""
    property bool _keyboardFirstHandle: false

    // The range is one tab stop: arrows adjust its upper end, Shift+arrows
    // adjust its lower end. This keeps normal Tab traversal intact while both
    // values remain operable without a pointer.
    function _adjustKeyboardHandle(delta, lower) {
        if (!control.enabled) {
            return
        }
        const step = control.stepSize > 0 ? control.stepSize
                                          : Math.abs(control.to - control.from) / 100
        if (lower) {
            const value = Math.max(control.from,
                                   Math.min(control.second.value,
                                            control.first.value + delta * step))
            if (value !== control.first.value) {
                control.first.value = value
                control.rangeModified(control.first.value, control.second.value)
            }
        } else {
            const value = Math.max(control.first.value,
                                   Math.min(control.to,
                                            control.second.value + delta * step))
            if (value !== control.second.value) {
                control.second.value = value
                control.rangeModified(control.first.value, control.second.value)
            }
        }
    }

    signal rangeModified(real first, real second)

    readonly property real rowHeight: small ? Tk.Theme.size.icon : Tk.Theme.size.iconLg
    // AGENT-CONTRACT (D-022): a range slider's visible row stays compact while
    // its template input surface reaches the shared Touch control target height.
    readonly property real _targetHeight: Tk.Density.mode === Tk.Density.Touch
                                          ? Math.max(rowHeight, Tk.Theme.size.control)
                                          : rowHeight
    readonly property real handleSize: small ? Tk.Theme.space.md : Tk.Theme.size.grip
    readonly property real trackThickness: Tk.Theme.space.xs
    readonly property string readout:
        Number(control.first.value).toFixed(Math.max(0, control.decimals))
        + control.suffix + "–"
        + Number(control.second.value).toFixed(Math.max(0, control.decimals))
        + control.suffix

    objectName: "rangeSlider"
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            first.implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             first.implicitHandleHeight + topPadding + bottomPadding)
    padding: 0
    rightPadding: showValues ? valueText.implicitWidth + Tk.Theme.space.sm : 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    first.onMoved: {
        control._keyboardFirstHandle = true
        control.rangeModified(control.first.value, control.second.value)
    }
    second.onMoved: {
        control._keyboardFirstHandle = false
        control.rangeModified(control.first.value, control.second.value)
    }

    Accessible.role: Accessible.Slider
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Range slider")
    Accessible.description: qsTr("From %1 to %2. Left and Right change the upper value; Shift+Left and Shift+Right change the lower value.")
                             .arg(control.first.value).arg(control.second.value)
    Accessible.focusable: control.enabled
    Accessible.focused: control.activeFocus
    Accessible.onIncreaseAction: control._adjustKeyboardHandle(1, false)
    Accessible.onDecreaseAction: control._adjustKeyboardHandle(-1, false)

    Keys.onPressed: function(event) {
        if (!control.enabled) {
            return
        }
        switch (event.key) {
        case Qt.Key_Left:
            control._keyboardFirstHandle = (event.modifiers & Qt.ShiftModifier) !== 0
            control._adjustKeyboardHandle(-1, control._keyboardFirstHandle)
            event.accepted = true
            break
        case Qt.Key_Right:
            control._keyboardFirstHandle = (event.modifiers & Qt.ShiftModifier) !== 0
            control._adjustKeyboardHandle(1, control._keyboardFirstHandle)
            event.accepted = true
            break
        case Qt.Key_Home:
            if (event.modifiers & Qt.ShiftModifier) {
                control._keyboardFirstHandle = true
                if (control.first.value !== control.from) {
                    control.first.value = control.from
                    control.rangeModified(control.first.value, control.second.value)
                }
            } else {
                control._keyboardFirstHandle = false
                if (control.second.value !== control.first.value) {
                    control.second.value = control.first.value
                    control.rangeModified(control.first.value, control.second.value)
                }
            }
            event.accepted = true
            break
        case Qt.Key_End:
            if (event.modifiers & Qt.ShiftModifier) {
                control._keyboardFirstHandle = true
                if (control.first.value !== control.second.value) {
                    control.first.value = control.second.value
                    control.rangeModified(control.first.value, control.second.value)
                }
            } else {
                control._keyboardFirstHandle = false
                if (control.second.value !== control.to) {
                    control.second.value = control.to
                    control.rangeModified(control.first.value, control.second.value)
                }
            }
            event.accepted = true
            break
        }
    }

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
        delay: 600
    }

    background: Item {
        implicitWidth: Tk.Theme.size.fieldWidth
        implicitHeight: control._targetHeight

        Rectangle {
            objectName: "rangeSliderTrack"
            x: control.leftPadding
            y: control.topPadding + (control.availableHeight - height) / 2
            width: control.availableWidth
            height: control.trackThickness
            radius: height / 2
            color: Tk.Theme.color.borderStrong

            // Only the band between the handles is accented: the point of a
            // range control is to show what is included, not where it starts.
            Rectangle {
                objectName: "rangeSliderBand"
                x: control.first.position * parent.width
                width: (control.second.position - control.first.position) * parent.width
                height: parent.height
                radius: parent.radius
                color: Tk.Theme.color.accent
            }
        }
    }

    first.handle: Rectangle {
        objectName: "rangeSliderFirstHandle"
        x: control.leftPadding + control.first.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: control.handleSize
        implicitHeight: control.handleSize
        radius: width / 2
        color: control.first.pressed ? Tk.Theme.color.pressed : Tk.Theme.color.controlBg
        border.width: Tk.Theme.size.border
        border.color: control.first.pressed || control.first.hovered
                      ? Tk.Theme.color.accent : Tk.Theme.color.controlBorder

        Rectangle {
            objectName: "rangeSliderFirstFocus"
            anchors.fill: parent
            anchors.margins: -Tk.Theme.size.focusRing
            radius: width / 2
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.activeFocus && control._keyboardFirstHandle
        }
    }

    second.handle: Rectangle {
        objectName: "rangeSliderSecondHandle"
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: control.handleSize
        implicitHeight: control.handleSize
        radius: width / 2
        color: control.second.pressed ? Tk.Theme.color.pressed : Tk.Theme.color.controlBg
        border.width: Tk.Theme.size.border
        border.color: control.second.pressed || control.second.hovered
                      ? Tk.Theme.color.accent : Tk.Theme.color.controlBorder

        Rectangle {
            objectName: "rangeSliderSecondFocus"
            anchors.fill: parent
            anchors.margins: -Tk.Theme.size.focusRing
            radius: width / 2
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.activeFocus && !control._keyboardFirstHandle
        }
    }

    Tk.Mono {
        id: valueText
        objectName: "rangeSliderValue"
        visible: control.showValues
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: control.readout
        color: Tk.Theme.color.textMuted
    }
}
