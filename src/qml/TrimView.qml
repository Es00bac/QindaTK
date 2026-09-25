// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: two modes (docs/controls.md "TrimView", docs/sequence-edits.md).
// Host mode (`editor` null): the host owns the trim snapshot `edit`; the view
// only emits exact decimal tick requests and shows the host's acknowledgment.
// Editor mode (`editor` set): a Tk.SequenceEditor supplies the snapshot and
// runs preview/commit/cancel as TimelineModel transactions; the frames shown
// follow the live preview, and the request signals are not emitted.
Item {
    id: root
    property var edit: null // {editId,leftClipId,rightClipId,leftResourceId,rightResourceId,leftHeadTicks,rightHeadTicks,leftTailTicks,rightTailTicks,legalHead,legalTail,missingLeft,missingRight}
    property Tk.SequenceEditor editor: null
    property var assetProvider: null
    property var document: null
    property string viewId: ""
    property string rateNumerator: "24"
    property string rateDenominator: "1"
    property string mode: "roll" // roll, ripple, slip, slide
    property string side: "left" // ripple/slip/slide: "left" outgoing clip, "right" incoming clip
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

    readonly property bool editorMode: root.editor !== null
    readonly property var effectiveEdit: root.editorMode ? root.editor.trimEdit : root.edit
    readonly property string effectiveOperationId: root.editorMode ? root.editor.trimOperationId : root.previewOperationId
    readonly property string effectiveStatus: root.editorMode ? root.editor.trimStatus : root.previewStatus
    readonly property string effectiveError: root.editorMode ? root.editor.trimError : root.errorText
    readonly property string effectiveStatusText: !root.editorMode ? root.statusText
        : root.editor.trimStatus === "ready" ? qsTr("Preview · %1 ticks applied").arg(root.editor.trimAppliedDelta)
        : root.editor.trimStatus === "committed" ? qsTr("Committed to the sequence") : ""
    readonly property string effectiveRateNumerator: root.editorMode && root.snapshotValid
        ? String(root.effectiveEdit.rateNumerator || root.rateNumerator) : root.rateNumerator
    readonly property string effectiveRateDenominator: root.editorMode && root.snapshotValid
        ? String(root.effectiveEdit.rateDenominator || root.rateDenominator) : root.rateDenominator
    // AGENT-GUARD: frame requests carry timeTicks at rateNumerator/Denominator
    // TICKS per second (docs/assets-jobs.md). The editor snapshot's times are
    // timeline ticks, so backend mode sends the timeline timebase, never the
    // frame rate above; with the frame rate every request fell outside the
    // media and the filmstrips stayed empty (examples/sequence-edit-live).
    readonly property string assetRateNumerator: root.editorMode && root.editor.timeline
        ? root.editor.timeline.timebaseNumerator : root.effectiveRateNumerator
    readonly property string assetRateDenominator: root.editorMode && root.editor.timeline
        ? root.editor.timeline.timebaseDenominator : root.effectiveRateDenominator
    readonly property bool snapshotValid: root.effectiveEdit !== null && root.effectiveEdit !== undefined
        && String(root.effectiveEdit.editId || "").length > 0
        && String(root.effectiveEdit.leftClipId || "").length > 0
        && String(root.effectiveEdit.rightClipId || "").length > 0
    readonly property bool validDelta: /^-?[0-9]+$/.test(root.deltaTicks)
    readonly property bool handlesLegal: root.editorMode ? root._editorHandlesLegal()
        : root.snapshotValid && root.effectiveEdit.missingLeft !== true
          && root.effectiveEdit.missingRight !== true && (root.mode === "slip" || root.mode === "slide"
          || (root.effectiveEdit.legalHead !== false && root.effectiveEdit.legalTail !== false))

    objectName: "trimView"
    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.thumbnailHeight * 2 + Tk.Theme.size.controlLg * 3
        + Tk.Theme.space.lg * 3
    Accessible.role: Accessible.Pane
    Accessible.name: root.tooltip
    Accessible.description: root.effectiveStatusText.length ? root.effectiveStatusText
        : !snapshotValid ? qsTr("No valid edit point is selected.")
        : !handlesLegal ? qsTr("The selected trim mode exceeds available source handles.")
        : qsTr("Exact tick delta. Sequence edits require host approval.")

    // Editor mode: a move needs source handle only in the direction it goes;
    // the planner clamps the rest. Offline media keeps its recorded range.
    function _editorHandlesLegal() {
        if (!root.snapshotValid || !root.validDelta) return false
        const negative = root.deltaTicks.charAt(0) === "-"
        const zero = /^-?0+$/.test(root.deltaTicks)
        if (zero || root.mode === "slip" || root.mode === "slide") return true
        const edit = root.effectiveEdit
        if (root.mode === "roll") return negative ? edit.legalHead !== false : edit.legalTail !== false
        if (root.side === "right") return !negative || edit.legalHead !== false
        return negative || edit.legalTail !== false
    }
    function requestId(kind) {
        root._serial = String(Number(root._serial) + 1)
        return (root.viewId.length ? root.viewId : "trim") + ":" + kind + ":" + root._serial
    }
    function requestPreview() {
        if (!enabled || !snapshotValid || !validDelta || !handlesLegal) return false
        if (root.editorMode) return root.editor.previewTrim(root.mode, root.side, root.deltaTicks).ok === true
        previewRequested(requestId("preview"), String(effectiveEdit.editId), mode, deltaTicks)
        return true
    }
    function commitPreview() {
        if (!enabled || effectiveStatus !== "ready" || effectiveOperationId.length === 0) return false
        if (root.editorMode) return root.editor.commitTrim().ok === true
        commitRequested(requestId("commit"), effectiveOperationId)
        return true
    }
    function cancelPreview() {
        if (!enabled || effectiveOperationId.length === 0) return false
        if (root.editorMode) return root.editor.cancelTrim()
        cancelRequested(requestId("cancel"), effectiveOperationId)
        return true
    }
    function requestFrame(side, ticks) {
        if (!enabled || !snapshotValid || !/^-?[0-9]+$/.test(String(ticks))) return false
        frameSeekRequested(String(effectiveEdit.editId), side, String(ticks))
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
                text: root.snapshotValid ? qsTr("%1 ⇄ %2").arg(root.effectiveEdit.leftClipId).arg(root.effectiveEdit.rightClipId)
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
                Tk.Overline { text: qsTr("OUTGOING · %1").arg(root.effectiveEdit ? String(root.effectiveEdit.leftClipId || "—") : "—") }
                Tk.AssetFilmstrip {
                    objectName: "trimOutgoingFrames"
                    provider: root.assetProvider
                    resourceId: root.effectiveEdit ? String(root.effectiveEdit.leftResourceId || "") : ""
                    headTimeTicks: root.effectiveEdit ? String(root.effectiveEdit.leftHeadTicks || "0") : "0"
                    tailTimeTicks: root.effectiveEdit ? String(root.effectiveEdit.leftTailTicks || "") : ""
                    rateNumerator: root.assetRateNumerator
                    rateDenominator: root.assetRateDenominator
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
                    objectName: "trimOutgoingMissing"
                    visible: root.snapshotValid && root.effectiveEdit.missingLeft === true
                    color: Tk.Theme.color.warning
                    text: root.editorMode ? qsTr("Outgoing media is offline; edits keep its recorded source range.")
                                          : qsTr("Outgoing media is missing; source handles cannot be verified.")
                    wrapMode: Text.WordWrap
                }
            }
            Tk.Flex {
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Overline { text: qsTr("INCOMING · %1").arg(root.effectiveEdit ? String(root.effectiveEdit.rightClipId || "—") : "—") }
                Tk.AssetFilmstrip {
                    objectName: "trimIncomingFrames"
                    provider: root.assetProvider
                    resourceId: root.effectiveEdit ? String(root.effectiveEdit.rightResourceId || "") : ""
                    headTimeTicks: root.effectiveEdit ? String(root.effectiveEdit.rightHeadTicks || "0") : "0"
                    tailTimeTicks: root.effectiveEdit ? String(root.effectiveEdit.rightTailTicks || "") : ""
                    rateNumerator: root.assetRateNumerator
                    rateDenominator: root.assetRateDenominator
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
                    objectName: "trimIncomingMissing"
                    visible: root.snapshotValid && root.effectiveEdit.missingRight === true
                    color: Tk.Theme.color.warning
                    text: root.editorMode ? qsTr("Incoming media is offline; edits keep its recorded source range.")
                                          : qsTr("Incoming media is missing; source handles cannot be verified.")
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
            Tk.Segmented {
                objectName: "trimSide"
                visible: root.mode !== "roll"
                model: [{text: qsTr("Outgoing"), value: "left"}, {text: qsTr("Incoming"), value: "right"}]
                currentIndex: root.side === "right" ? 1 : 0
                tooltip: qsTr("Clip the ripple, slip or slide changes")
                onActivated: function(index) { root.side = String(currentValue) }
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
                tooltip: qsTr("Preview this trim on the sequence")
                onClicked: root.requestPreview()
            }
            Tk.Button {
                objectName: "trimCommit"
                text: qsTr("Commit")
                variant: "accent"
                enabled: root.effectiveStatus === "ready" && root.effectiveOperationId.length > 0
                tooltip: qsTr("Commit the accepted preview as one undoable edit")
                onClicked: root.commitPreview()
            }
            Tk.Button {
                objectName: "trimCancel"
                text: qsTr("Cancel")
                enabled: root.effectiveOperationId.length > 0
                tooltip: qsTr("Cancel the trim preview")
                onClicked: root.cancelPreview()
            }
        }
        Tk.Caption {
            objectName: "trimStatus"
            visible: root.effectiveStatusText.length > 0 || !root.handlesLegal || root.effectiveError.length > 0
            color: root.effectiveError.length > 0 || !root.handlesLegal ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            text: root.effectiveError.length > 0 ? root.effectiveError
                : !root.handlesLegal ? qsTr("Insufficient source handles for this trim operation.") : root.effectiveStatusText
            wrapMode: Text.WordWrap
        }
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && root.hovered }
}
