// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned trim snapshot. The control only proposes exact decimal tick deltas;
// source bounds, linked clips and edit transactions stay in the sequence engine.
Item {
    id: root
    property var edit: null // {editId,leftClipId,rightClipId,leftResourceId,rightResourceId,leftHeadTicks,rightHeadTicks,leftTailTicks,rightTailTicks,legalHead,legalTail,missingLeft,missingRight}
    property var assetProvider: null
    property var document: null
    property string viewId: ""
    property string rateNumerator: "24"
    property string rateDenominator: "1"
    property string mode: "roll" // roll, ripple, slip, slide
    property string deltaTicks: "0"
    property string previewOperationId: ""
    property string previewStatus: "idle" // idle, pending, ready, rejected
    property string statusText: ""
    property string errorText: ""
    property string tooltip: qsTr("Precision trim editor")
    property string _serial: "0"
    property bool hovered: hover.hovered

    signal previewRequested(string requestId, string editId, string mode, string deltaTicks)
    signal commitRequested(string requestId, string operationId)
    signal cancelRequested(string requestId, string operationId)
    signal frameSeekRequested(string editId, string side, string timeTicks)

    readonly property bool snapshotValid: root.edit !== null && root.edit !== undefined
        && String(root.edit.editId || "").length > 0
        && String(root.edit.leftClipId || "").length > 0
        && String(root.edit.rightClipId || "").length > 0
    readonly property bool validDelta: /^-?[0-9]+$/.test(root.deltaTicks)
    readonly property bool handlesLegal: root.snapshotValid && root.edit.missingLeft !== true
        && root.edit.missingRight !== true && (root.mode === "slip" || root.mode === "slide"
        || (root.edit.legalHead !== false && root.edit.legalTail !== false))

    objectName: "trimView"
    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.thumbnailHeight * 2 + Tk.Theme.size.controlLg * 3
        + Tk.Theme.space.lg * 3
    Accessible.role: Accessible.Pane
    Accessible.name: root.tooltip
    Accessible.description: root.statusText.length ? root.statusText
        : !snapshotValid ? qsTr("No valid edit point is selected.")
        : !handlesLegal ? qsTr("The selected trim mode exceeds available source handles.")
        : qsTr("Exact tick delta. Sequence edits require host approval.")

    function requestId(kind) {
        root._serial = String(Number(root._serial) + 1)
        return (root.viewId.length ? root.viewId : "trim") + ":" + kind + ":" + root._serial
    }
    function requestPreview() {
        if (!enabled || !snapshotValid || !validDelta || !handlesLegal) return false
        previewRequested(requestId("preview"), String(edit.editId), mode, deltaTicks)
        return true
    }
    function commitPreview() {
        if (!enabled || previewStatus !== "ready" || previewOperationId.length === 0) return false
        commitRequested(requestId("commit"), previewOperationId)
        return true
    }
    function cancelPreview() {
        if (!enabled || previewOperationId.length === 0) return false
        cancelRequested(requestId("cancel"), previewOperationId)
        return true
    }
    function requestFrame(side, ticks) {
        if (!enabled || !snapshotValid || !/^-?[0-9]+$/.test(String(ticks))) return false
        frameSeekRequested(String(edit.editId), side, String(ticks))
        return true
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Overline { text: qsTr("TRIM VIEW") }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption {
                text: root.snapshotValid ? qsTr("%1 ⇄ %2").arg(root.edit.leftClipId).arg(root.edit.rightClipId)
                                         : qsTr("Select an edit point")
                color: Tk.Theme.color.textMuted
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { text: qsTr("OUTGOING · %1").arg(root.edit ? String(root.edit.leftClipId || "—") : "—") }
                Tk.AssetFilmstrip {
                    objectName: "trimOutgoingFrames"
                    provider: root.assetProvider
                    resourceId: root.edit ? String(root.edit.leftResourceId || "") : ""
                    headTimeTicks: root.edit ? String(root.edit.leftHeadTicks || "0") : "0"
                    tailTimeTicks: root.edit ? String(root.edit.leftTailTicks || "") : ""
                    rateNumerator: root.rateNumerator
                    rateDenominator: root.rateDenominator
                    document: root.document
                    viewId: root.viewId + ":trim-out"
                    active: root.enabled && root.snapshotValid
                    frameSize: ({width: Tk.Theme.size.thumbnail * 2, height: Tk.Theme.size.thumbnailHeight})
                    tooltip: qsTr("Outgoing clip head and final frame")
                    width: parent.width
                    height: Tk.Theme.size.thumbnailHeight * 2
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Caption {
                    visible: root.snapshotValid && root.edit.missingLeft === true
                    color: Tk.Theme.color.warning
                    text: qsTr("Outgoing media is missing; source handles cannot be verified.")
                    wrapMode: Text.WordWrap
                }
            }
            Tk.Flex {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { text: qsTr("INCOMING · %1").arg(root.edit ? String(root.edit.rightClipId || "—") : "—") }
                Tk.AssetFilmstrip {
                    objectName: "trimIncomingFrames"
                    provider: root.assetProvider
                    resourceId: root.edit ? String(root.edit.rightResourceId || "") : ""
                    headTimeTicks: root.edit ? String(root.edit.rightHeadTicks || "0") : "0"
                    tailTimeTicks: root.edit ? String(root.edit.rightTailTicks || "") : ""
                    rateNumerator: root.rateNumerator
                    rateDenominator: root.rateDenominator
                    document: root.document
                    viewId: root.viewId + ":trim-in"
                    active: root.enabled && root.snapshotValid
                    frameSize: ({width: Tk.Theme.size.thumbnail * 2, height: Tk.Theme.size.thumbnailHeight})
                    tooltip: qsTr("Incoming clip first and tail frame")
                    width: parent.width
                    height: Tk.Theme.size.thumbnailHeight * 2
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Caption {
                    visible: root.snapshotValid && root.edit.missingRight === true
                    color: Tk.Theme.color.warning
                    text: qsTr("Incoming media is missing; source handles cannot be verified.")
                    wrapMode: Text.WordWrap
                }
            }
        }
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Segmented {
                objectName: "trimMode"
                model: [{text: qsTr("Roll"), value: "roll"}, {text: qsTr("Ripple"), value: "ripple"},
                        {text: qsTr("Slip"), value: "slip"}, {text: qsTr("Slide"), value: "slide"}]
                currentIndex: Math.max(0, ["roll", "ripple", "slip", "slide"].indexOf(root.mode))
                tooltip: qsTr("Trim operation")
                onActivated: function(index) { root.mode = String(currentValue) }
            }
            Tk.Caption { text: qsTr("Delta · ticks") }
            Tk.TextField {
                id: deltaField
                objectName: "trimDeltaTicks"
                text: root.deltaTicks
                mono: true
                error: text.length > 0 && !/^-?[0-9]+$/.test(text)
                tooltip: qsTr("Signed exact time delta in timeline ticks")
                onTextEdited: root.deltaTicks = text
                width: Tk.Theme.size.fieldWidth
            }
            Tk.Button {
                objectName: "trimPreview"
                text: qsTr("Preview")
                iconName: "eye"
                enabled: root.snapshotValid && root.validDelta && root.handlesLegal
                tooltip: qsTr("Ask the sequence host to preview this trim")
                onClicked: root.requestPreview()
            }
            Tk.Button {
                objectName: "trimCommit"
                text: qsTr("Commit")
                variant: "accent"
                enabled: root.previewStatus === "ready" && root.previewOperationId.length > 0
                tooltip: qsTr("Commit the accepted preview through the host")
                onClicked: root.commitPreview()
            }
            Tk.Button {
                objectName: "trimCancel"
                text: qsTr("Cancel")
                enabled: root.previewOperationId.length > 0
                tooltip: qsTr("Cancel the host trim preview")
                onClicked: root.cancelPreview()
            }
        }
        Tk.Caption {
            objectName: "trimStatus"
            visible: root.statusText.length > 0 || !root.handlesLegal || root.errorText.length > 0
            color: root.errorText.length > 0 || !root.handlesLegal ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            text: root.errorText.length > 0 ? root.errorText
                : !root.handlesLegal ? qsTr("Insufficient source handles for this trim operation.") : root.statusText
            wrapMode: Text.WordWrap
        }
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && root.hovered }
}
