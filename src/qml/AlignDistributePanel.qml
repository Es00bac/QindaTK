// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (WP-042/CAN-21): the host supplies stable object IDs and
// owns canonical bounds, geometry previews, commit and undo. Requests carry
// string IDs so 64-bit CanvasObjectIds remain exact across QML/JS. The
// reference choices are domain data (D-095): bind `referenceOptions` to
// CanvasAlignmentModel.referenceOptions to offer its artboard reference.
Item {
    id: control
    objectName: "alignDistributePanel"

    property var selectedIds: []
    property string referenceMode: "selection"
    property string keyObjectId: ""
    property real spacing: 0
    property real spacingStep: 1
    property string tooltip: qsTr("Align and distribute selected objects")
    // [{value, text, available}] in menu order; "page" and "artboard" are
    // fixed references that one selected object can align to.
    property var referenceOptions: [
        { value: "selection", text: qsTr("Selection"), available: true },
        { value: "keyObject", text: qsTr("Key object"), available: true },
        { value: "page", text: qsTr("Page"), available: true }
    ]

    readonly property bool hasSelection: selectedIds && selectedIds.length > 0
    readonly property int referenceIndex: {
        const options = control.referenceOptions || []
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === control.referenceMode) return i
        return -1
    }
    readonly property bool referenceAvailable: referenceIndex >= 0
                                                && referenceOptions[referenceIndex].available !== false
                                                && (referenceMode !== "keyObject" || keyObjectId.length > 0)
    readonly property bool actionAvailable: hasSelection && referenceAvailable
                                            && (referenceMode === "page" || referenceMode === "artboard"
                                                || selectedIds.length > 1)

    signal operationRequested(string operation, var selectedIds,
                              string referenceMode, string keyObjectId,
                              real spacing)

    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: panelContent.implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: qsTr("Requests alignment and spacing operations for the selected objects")

    function requestOperation(operation) {
        if (!control.actionAvailable) return
        const ids = Array.from(control.selectedIds, id => String(id))
        control.operationRequested(operation, ids, control.referenceMode,
                                   control.keyObjectId, control.spacing)
    }

    Tk.Flex {
        id: panelContent
        width: parent.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Overline {
            objectName: "alignDistributeTitle"
            title: qsTr("Align & distribute")
            wide: true
        }

        Tk.ComboBox {
            id: referencePicker
            objectName: "alignReferenceMode"
            width: parent.width
            tooltip: qsTr("Alignment reference")
            textRole: "text"
            model: control.referenceOptions
            currentIndex: Math.max(0, control.referenceIndex)
            onActivated: function(index) {
                control.referenceMode = control.referenceOptions[index].value
            }
        }

        Tk.Overline {
            objectName: "alignSectionTitle"
            title: qsTr("Align")
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            width: parent.width
            Tk.Button {
                objectName: "alignLeftButton"
                text: qsTr("Left")
                tooltip: qsTr("Align left edges")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("align.left")
            }
            Tk.Button {
                objectName: "alignHorizontalCenterButton"
                text: qsTr("Center")
                tooltip: qsTr("Align horizontal centers")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("align.horizontalCenter")
            }
            Tk.Button {
                objectName: "alignRightButton"
                text: qsTr("Right")
                tooltip: qsTr("Align right edges")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("align.right")
            }
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            width: parent.width
            Tk.Button {
                objectName: "alignTopButton"
                text: qsTr("Top")
                tooltip: qsTr("Align top edges")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("align.top")
            }
            Tk.Button {
                objectName: "alignVerticalCenterButton"
                text: qsTr("Middle")
                tooltip: qsTr("Align vertical centers")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("align.verticalCenter")
            }
            Tk.Button {
                objectName: "alignBottomButton"
                text: qsTr("Bottom")
                tooltip: qsTr("Align bottom edges")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("align.bottom")
            }
        }

        Tk.Overline {
            objectName: "distributeSectionTitle"
            title: qsTr("Distribute")
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            width: parent.width
            Tk.Button {
                objectName: "distributeHorizontalButton"
                text: qsTr("Horizontal")
                tooltip: qsTr("Distribute object centers horizontally")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("distribute.horizontal")
            }
            Tk.Button {
                objectName: "distributeVerticalButton"
                text: qsTr("Vertical")
                tooltip: qsTr("Distribute object centers vertically")
                small: true
                available: control.actionAvailable
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onClicked: control.requestOperation("distribute.vertical")
            }
        }

        Tk.NumberField {
            id: spacingField
            objectName: "distributeSpacing"
            width: parent.width
            label: qsTr("Gap")
            tooltip: qsTr("Requested spacing in document units")
            from: 0
            stepSize: control.spacingStep
            decimals: 1
            Binding {
                target: spacingField
                property: "value"
                value: control.spacing
            }
            onValueModified: control.spacing = value
        }
    }

    onSpacingChanged: {
        if (!Number.isFinite(spacing) || spacing < 0)
            spacing = 0
    }
}
