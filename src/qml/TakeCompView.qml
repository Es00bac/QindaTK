// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Host-owned takes and comp snapshots. Gesture coordinates choose among the
// supplied decimal-string boundaries; sample values are never Number-coerced.
// AGENT-CONTRACT (WP-065, docs/audio-comping.md): AudioTakeCompModel.viewState
// supplies takes/compSegments/sampleBoundaries; a take's sourceOffsetSample is
// a signed decimal (a pass can start before the comp origin).
T.Control {
    id: root
    property var takes: [] // {takeId, sourceId, sourceOffsetSample, peaks, available}
    property var compSegments: [] // {segmentId, takeId, startSample, endSample, sourceOffsetSample, fadeInEndSample, fadeOutStartSample}
    property var sampleBoundaries: [] // exact, ordered decimal strings spanning the visible timeline
    property string selectionStartSample: ""
    property string selectionEndSample: ""
    property string referenceTakeId: ""
    // The take the host is auditioning now ("" when none): its lane button
    // and Space end the audition, as does Escape outside a swipe.
    property string auditionTakeId: ""
    property var proposal: null // host response descriptor; status "pending" exposes accept/reject requests
    property string statusText: ""
    property string tooltip: ""
    property bool editingEnabled: true
    // Rate of the sample strings (AudioTakeCompModel.viewState's sampleRate):
    // lane offsets read in seconds. 0 shows raw samples.
    property int sampleRate: 0

    property string _gestureTakeId: ""
    property string _gestureId: ""
    property int _gestureSerial: 0
    property int _startBoundary: -1
    property int _endBoundary: -1
    // Keyboard selection: a cursor cell and an anchor cell of the boundary
    // table, and the take lane the keys act on.
    property int keyboardTakeIndex: 0
    property int _cursorCell: -1
    property int _anchorCell: -1
    readonly property string keyboardTakeId: takeCount > 0
        ? String(takes[Math.max(0, Math.min(takeCount - 1, keyboardTakeIndex))].takeId) : ""

    signal compProposalPreviewRequested(string proposalId, string takeId, string startSample, string endSample, string sourceOffsetSample)
    // A swipe ended without a proposal (Escape, disabled): drop its preview.
    signal compProposalPreviewCanceled(string proposalId)
    signal compProposalRequested(string proposalId, string segmentId, string takeId, string startSample, string endSample, string sourceId, string sourceOffsetSample)
    signal proposalDecisionRequested(string proposalId, bool accept)
    signal auditionRequested(string takeId, string sourceId, string startSample, string endSample, string sourceOffsetSample)
    signal auditionStopRequested(string takeId)
    signal compareRequested(string takeId, string referenceTakeId, string startSample, string endSample, string takeOffsetSample, string referenceOffsetSample)
    // Keyboard selection request; the host owns selectionStart/EndSample.
    signal selectionRequested(string startSample, string endSample)

    readonly property bool snapshotValid: root.validateSnapshot()
    readonly property int takeCount: root.takes ? root.takes.length : 0
    readonly property int segmentCount: root.compSegments ? root.compSegments.length : 0
    readonly property int takeLaneHeight: Tk.Theme.size.controlLg * 2
    readonly property bool hasPendingProposal: root.proposal
        && String(root.proposal.status || "pending") === "pending"

    objectName: "takeCompView"
    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: takeLaneHeight * takeCount + Tk.Theme.size.controlLg
        + Tk.Theme.size.row + Tk.Theme.space.md * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Take comp editor")
    Accessible.description: root.statusText.length ? root.statusText
        : (snapshotValid ? qsTr("%1 takes, %2 host comp segments. Left and Right select cells, Shift extends, Up and Down choose a take, Enter uses it, Space auditions or stops, Escape cancels.").arg(takeCount).arg(segmentCount)
                         : qsTr("Take comp snapshot is invalid or incomplete."))
    activeFocusOnTab: true
    hoverEnabled: true

    function isDecimalString(value) { return typeof value === "string" && /^[0-9]+$/.test(value) }
    function isSignedDecimalString(value) { return typeof value === "string" && /^-?[0-9]+$/.test(value) }
    function decimalCompare(a, b) {
        const left = String(a), right = String(b)
        if (left.length !== right.length) return left.length < right.length ? -1 : 1
        return left === right ? 0 : (left < right ? -1 : 1)
    }
    function indexForBoundary(sample) {
        const wanted = String(sample)
        for (let i = 0; i < root.sampleBoundaries.length; ++i)
            if (String(root.sampleBoundaries[i]) === wanted) return i
        return -1
    }
    function takeById(takeId) {
        for (let i = 0; i < root.takes.length; ++i)
            if (String(root.takes[i].takeId) === String(takeId)) return root.takes[i]
        return null
    }
    function validateSnapshot() {
        if (!root.sampleBoundaries || root.sampleBoundaries.length < 2 || !root.takes || !root.compSegments)
            return false
        for (let i = 0; i < root.sampleBoundaries.length; ++i) {
            if (!root.isDecimalString(root.sampleBoundaries[i])) return false
            if (i && root.decimalCompare(root.sampleBoundaries[i - 1], root.sampleBoundaries[i]) >= 0) return false
        }
        const ids = Object.create(null)
        for (let i = 0; i < root.takes.length; ++i) {
            const take = root.takes[i]
            const id = String(take.takeId || "")
            if (!id.length || ids[id] || !String(take.sourceId || "").length
                || !root.isSignedDecimalString(String(take.sourceOffsetSample))) return false
            ids[id] = true
        }
        const segmentIds = Object.create(null)
        for (let i = 0; i < root.compSegments.length; ++i) {
            const segment = root.compSegments[i]
            const id = String(segment.segmentId || "")
            if (!id.length || segmentIds[id] || !ids[String(segment.takeId)]
                || !root.isSignedDecimalString(String(segment.sourceOffsetSample))) return false
            segmentIds[id] = true
            const start = root.indexForBoundary(segment.startSample)
            const end = root.indexForBoundary(segment.endSample)
            if (start < 0 || end <= start) return false
            for (const edge of [segment.fadeInEndSample, segment.fadeOutStartSample]) { // optional, inside the segment
                const at = edge === undefined ? start : root.indexForBoundary(edge)
                if (at < start || at > end) return false
            }
        }
        return true
    }
    function boundaryIndexAt(x, laneWidth) {
        const cells = root.sampleBoundaries.length - 1
        if (cells < 1) return -1
        const fraction = Math.max(0, Math.min(0.999999, Number(x) / Math.max(1, Number(laneWidth))))
        return Math.min(cells - 1, Math.floor(fraction * cells))
    }
    function swipeRange() {
        if (root._startBoundary < 0 || root._endBoundary < 0) return null
        const start = Math.min(root._startBoundary, root._endBoundary)
        const last = Math.max(root._startBoundary, root._endBoundary)
        return { startSample: String(root.sampleBoundaries[start]),
                 endSample: String(root.sampleBoundaries[Math.min(last + 1, root.sampleBoundaries.length - 1)]) }
    }
    function segmentForRange(startSample, endSample) {
        const from = root.indexForBoundary(startSample), to = root.indexForBoundary(endSample)
        for (let i = 0; i < root.compSegments.length; ++i) {
            const segment = root.compSegments[i]
            const a = root.indexForBoundary(segment.startSample), b = root.indexForBoundary(segment.endSample)
            if (a <= from && b >= to) return String(segment.segmentId)
        }
        return ""
    }
    function validRange(startSample, endSample) {
        const start = root.indexForBoundary(startSample), end = root.indexForBoundary(endSample)
        return start >= 0 && end > start
    }
    function beginSwipe(takeId, x, laneWidth) {
        const take = root.takeById(takeId)
        if (!root.enabled || !root.editingEnabled || !root.snapshotValid || !take || take.available === false
            || root._gestureId.length) return false
        const boundary = root.boundaryIndexAt(x, laneWidth)
        root._gestureId = "take-comp-" + (++root._gestureSerial).toString()
        root._gestureTakeId = String(takeId)
        root._startBoundary = boundary
        root._endBoundary = boundary
        root.previewSwipe()
        return true
    }
    function updateSwipe(x, laneWidth) {
        if (!root._gestureId.length) return false
        root._endBoundary = root.boundaryIndexAt(x, laneWidth)
        root.previewSwipe()
        return true
    }
    function previewSwipe() {
        const range = root.swipeRange(), take = root.takeById(root._gestureTakeId)
        if (!range || !take) return false
        root.compProposalPreviewRequested(root._gestureId, root._gestureTakeId,
                                          range.startSample, range.endSample,
                                          String(take.sourceOffsetSample))
        compPaint.requestPaint()
        return true
    }
    function finishSwipe() {
        if (!root._gestureId.length) return false
        const id = root._gestureId, take = root.takeById(root._gestureTakeId)
        const range = root.swipeRange()
        root._gestureId = ""
        root._gestureTakeId = ""
        if (!take || !range) return false
        root.compProposalRequested(id, root.segmentForRange(range.startSample, range.endSample),
                                   String(take.takeId), range.startSample, range.endSample,
                                   String(take.sourceId), String(take.sourceOffsetSample))
        compPaint.requestPaint()
        return true
    }
    function cancelSwipe() {
        if (!root._gestureId.length) return false
        const id = root._gestureId
        root._gestureId = ""
        root._gestureTakeId = ""
        root.compProposalPreviewCanceled(id)
        compPaint.requestPaint()
        return true
    }
    function requestProposalForRange(takeId) {
        const take = root.takeById(takeId)
        const start = String(root.selectionStartSample), end = String(root.selectionEndSample)
        if (!root.editingEnabled || !root.snapshotValid || !take || take.available === false
            || root.indexForBoundary(start) < 0 || root.indexForBoundary(end) <= root.indexForBoundary(start)) return false
        const id = "take-comp-" + (++root._gestureSerial).toString()
        root.compProposalRequested(id, root.segmentForRange(start, end), String(take.takeId),
                                   start, end, String(take.sourceId), String(take.sourceOffsetSample))
        return true
    }
    function decideProposal(accept) {
        if (!root.enabled || !root.hasPendingProposal) return false
        const id = String(root.proposal.proposalId || "")
        if (!id.length) return false
        root.proposalDecisionRequested(id, Boolean(accept))
        return true
    }
    function stopAudition() {
        if (!root.auditionTakeId.length) return false
        root.auditionStopRequested(root.auditionTakeId)
        return true
    }
    function requestAudition(takeId) {
        if (root.auditionTakeId.length && String(takeId) === root.auditionTakeId) return root.stopAudition()
        const take = root.takeById(takeId)
        if (!take || take.available === false
            || !root.validRange(root.selectionStartSample, root.selectionEndSample)) return false
        root.auditionRequested(String(take.takeId), String(take.sourceId),
                               String(root.selectionStartSample), String(root.selectionEndSample),
                               String(take.sourceOffsetSample))
        return true
    }
    function requestCompare(takeId) {
        const take = root.takeById(takeId), reference = root.takeById(root.referenceTakeId)
        if (!take || !reference || take.available === false || reference.available === false
            || !root.validRange(root.selectionStartSample, root.selectionEndSample)) return false
        root.compareRequested(String(take.takeId), String(reference.takeId),
                              String(root.selectionStartSample), String(root.selectionEndSample),
                              String(take.sourceOffsetSample), String(reference.sourceOffsetSample))
        return true
    }
    // Keyboard: move the cursor cell by `delta` (clamped); `extend` keeps the
    // anchor, otherwise the selection is the cursor cell alone.
    function moveCursor(delta, extend) {
        const cells = root.sampleBoundaries.length - 1
        if (!root.snapshotValid || cells < 1) return false
        const from = root._cursorCell < 0 ? (delta < 0 ? cells : -1) : root._cursorCell
        root._cursorCell = Math.max(0, Math.min(cells - 1, from + delta))
        if (!extend || root._anchorCell < 0) root._anchorCell = root._cursorCell
        root._anchorCell = Math.min(root._anchorCell, cells - 1) // the table may have shrunk
        const first = Math.min(root._anchorCell, root._cursorCell)
        const last = Math.max(root._anchorCell, root._cursorCell)
        root.selectionRequested(String(root.sampleBoundaries[first]), String(root.sampleBoundaries[last + 1]))
        return true
    }
    function focusTake(delta) {
        if (root.takeCount < 1) return false
        root.keyboardTakeIndex = Math.max(0, Math.min(root.takeCount - 1, root.keyboardTakeIndex + delta))
        return true
    }
    // Display only (the exact string stays the value): a take's source
    // offset as seconds when the rate is known.
    function offsetText(sample) {
        const exact = String(sample)
        if (root.sampleRate <= 0) return qsTr("offset %1").arg(exact)
        const seconds = Number(exact) / root.sampleRate
        return qsTr("offset %1%2 s").arg(seconds < 0 ? "−" : "").arg(Math.abs(seconds).toFixed(3))
    }
    function fractionForSample(sample) {
        const index = root.indexForBoundary(sample)
        return index < 0 ? -1 : index / (root.sampleBoundaries.length - 1)
    }
    function colorCss(color) {
        return "rgba(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + ","
               + Math.round(color.b * 255) + "," + color.a + ")"
    }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.sm
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.Overline { text: qsTr("ACTIVE COMP") }
                Tk.Spacer { Tk.Flex.grow: 1 }
                Tk.Button {
                    objectName: "proposalAccept"
                    visible: root.hasPendingProposal
                    text: qsTr("Accept")
                    tooltip: qsTr("Ask the host to accept this comp proposal")
                    onClicked: root.decideProposal(true)
                }
                Tk.Button {
                    objectName: "proposalReject"
                    visible: root.hasPendingProposal
                    text: qsTr("Reject")
                    tooltip: qsTr("Ask the host to reject this comp proposal")
                    onClicked: root.decideProposal(false)
                }
            }
            Tk.Caption { text: root.statusText; color: Tk.Theme.color.textMuted }
        }
        Item {
            objectName: "activeCompLane"
            width: parent.width
            height: Tk.Theme.size.controlLg
            implicitHeight: Tk.Theme.size.controlLg
            Tk.Flex.basis: Tk.Theme.size.controlLg
            Tk.Flex.shrink: 0
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.inputBg; radius: Tk.Theme.radius.sm }
            Canvas {
                id: compPaint
                objectName: "compSegmentCanvas"
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset()
                    const w = width, h = height, segments = root.compSegments || []
                    for (let i = 0; i < segments.length; ++i) {
                        const segment = segments[i]
                        const x0 = root.fractionForSample(segment.startSample) * w
                        const x1 = root.fractionForSample(segment.endSample) * w
                        if (x0 < 0 || x1 <= x0) continue
                        ctx.fillStyle = root.colorCss(Tk.Theme.color.selection)
                        ctx.globalAlpha = Tk.Theme.opacity.muted
                        ctx.fillRect(x0, 0, x1 - x0, h)
                        ctx.globalAlpha = Tk.Theme.opacity.full
                        ctx.strokeStyle = root.colorCss(Tk.Theme.color.accent)
                        ctx.lineWidth = Tk.Theme.size.border
                        ctx.strokeRect(x0, 0, x1 - x0, h)
                        const fi = root.fractionForSample(segment.fadeInEndSample)
                        const fo = root.fractionForSample(segment.fadeOutStartSample)
                        if (fi > x0 / w && fi > 0) {
                            const edge = fi * w
                            ctx.fillStyle = root.colorCss(Tk.Theme.color.info)
                            ctx.globalAlpha = Tk.Theme.opacity.muted
                            ctx.fillRect(x0, 0, edge - x0, h)
                            ctx.globalAlpha = Tk.Theme.opacity.full
                            ctx.strokeStyle = root.colorCss(Tk.Theme.color.text)
                            ctx.beginPath(); ctx.moveTo(x0, h); ctx.lineTo(edge, 0); ctx.stroke()
                        }
                        if (fo >= 0 && fo < x1 / w) {
                            const edge = fo * w
                            ctx.fillStyle = root.colorCss(Tk.Theme.color.warning)
                            ctx.globalAlpha = Tk.Theme.opacity.muted
                            ctx.fillRect(edge, 0, x1 - edge, h)
                            ctx.globalAlpha = Tk.Theme.opacity.full
                            ctx.strokeStyle = root.colorCss(Tk.Theme.color.text)
                            ctx.beginPath(); ctx.moveTo(edge, 0); ctx.lineTo(x1, h); ctx.stroke()
                        }
                        // The take's name, clipped to its segment (IDs stay in the model).
                        const owner = root.takeById(segment.takeId)
                        ctx.save(); ctx.beginPath(); ctx.rect(x0, 0, x1 - x0, h); ctx.clip()
                        ctx.fillStyle = root.colorCss(Tk.Theme.color.text); ctx.textBaseline = "top"
                        ctx.font = Tk.Theme.font.micro + "px \"" + Tk.Theme.font.family + "\""
                        ctx.fillText(String(owner && owner.label ? owner.label : segment.takeId),
                                     x0 + Tk.Theme.space.xs, Tk.Theme.space.xs)
                        ctx.restore()
                    }
                    if (root._gestureId.length) {
                        const range = root.swipeRange()
                        if (range) {
                            const x0 = root.fractionForSample(range.startSample) * w
                            const x1 = root.fractionForSample(range.endSample) * w
                            ctx.fillStyle = root.colorCss(Tk.Theme.color.info)
                            ctx.globalAlpha = Tk.Theme.opacity.muted
                            ctx.fillRect(x0, 0, Math.max(Tk.Theme.size.border, x1 - x0), h)
                            ctx.globalAlpha = Tk.Theme.opacity.full
                        }
                    }
                }
            }
        }
        Repeater {
            model: root.takes || []
            delegate: Tk.Flex {
                id: takeRow
                required property var modelData
                objectName: "takeLane_" + String(modelData.takeId)
                width: parent.width
                height: root.takeLaneHeight + Tk.Theme.space.xs
                Tk.Flex.basis: root.takeLaneHeight + Tk.Theme.space.xs
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                // One fixed column for every lane, so the swipe surfaces line
                // up: the take name over its source offset, both elided.
                Item {
                    objectName: "takeLabel_" + String(modelData.takeId)
                    width: Tk.Theme.size.fieldWidth
                    implicitWidth: Tk.Theme.size.fieldWidth
                    height: root.takeLaneHeight
                    implicitHeight: root.takeLaneHeight
                    Tk.Flex.shrink: 0
                    clip: true
                    Tk.Label {
                        anchors { left: parent.left; right: parent.right; bottom: parent.verticalCenter }
                        text: String(modelData.label || modelData.takeId) // Label elides on the right
                        color: root.activeFocus && String(modelData.takeId) === root.keyboardTakeId
                            ? Tk.Theme.color.accent : Tk.Theme.color.text
                        Accessible.ignored: true
                    }
                    Tk.Caption {
                        objectName: "takeOffset_" + String(modelData.takeId)
                        anchors { left: parent.left; right: parent.right; top: parent.verticalCenter }
                        text: root.offsetText(modelData.sourceOffsetSample)
                        Accessible.ignored: true
                    }
                }
                Item {
                    id: swipeSurface
                    objectName: "takeSwipeSurface_" + String(modelData.takeId)
                    Tk.Flex.grow: 1
                    height: root.takeLaneHeight
                    implicitHeight: root.takeLaneHeight
                    property var peaks: modelData.peaks || []
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Take lane %1").arg(String(modelData.takeId))
                    Accessible.description: qsTr("Source %1, offset sample %2. Use the range button for keyboard selection.")
                        .arg(String(modelData.sourceId)).arg(String(modelData.sourceOffsetSample))
                    Rectangle { anchors.fill: parent; color: Tk.Theme.color.inputBg; radius: Tk.Theme.radius.sm }
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset()
                            const data = swipeSurface.peaks || [], mid = height / 2
                            if (!data.length) return
                            const step = Math.max(1, Math.ceil(data.length / Math.max(1, width)))
                            ctx.strokeStyle = root.colorCss(Tk.Theme.color.info)
                            ctx.globalAlpha = Tk.Theme.opacity.muted
                            ctx.lineWidth = Tk.Theme.size.border
                            for (let i = 0; i < data.length; i += step) {
                                const x = (i / Math.max(1, data.length - 1)) * width
                                const peak = Math.max(0, Math.min(1, Number(data[i]) || 0))
                                ctx.beginPath(); ctx.moveTo(x, mid - peak * mid * 0.9)
                                ctx.lineTo(x, mid + peak * mid * 0.9); ctx.stroke()
                            }
                            ctx.globalAlpha = Tk.Theme.opacity.full
                        }
                    }
                    DragHandler {
                        target: null
                        enabled: root.enabled && root.editingEnabled && root.snapshotValid && modelData.available !== false
                        onActiveChanged: {
                            if (active) root.beginSwipe(modelData.takeId, centroid.position.x, swipeSurface.width)
                            else root.finishSwipe()
                        }
                        onCentroidChanged: if (active) root.updateSwipe(centroid.position.x, swipeSurface.width)
                    }
                }
                Tk.Button {
                    text: qsTr("Use range")
                    enabled: root.editingEnabled && root.snapshotValid && modelData.available !== false
                    tooltip: qsTr("Propose this take for the host-selected sample range")
                    onClicked: root.requestProposalForRange(modelData.takeId)
                }
                Tk.Button {
                    readonly property bool auditioning: String(modelData.takeId) === root.auditionTakeId
                    objectName: "takeAudition_" + String(modelData.takeId)
                    text: auditioning ? qsTr("Stop") : qsTr("Audition")
                    enabled: auditioning || modelData.available !== false
                    tooltip: auditioning ? qsTr("Ask the host to end this audition") : qsTr("Request isolated audition of the selected range from the host")
                    onClicked: root.requestAudition(modelData.takeId)
                    // Stop keeps the Audition width, and Compare shows on every lane (disabled
                    // on the reference): every swipe surface has one width and time scale.
                    Tk.Flex.basis: auditionWidth.implicitWidth
                    Tk.Button { id: auditionWidth; opacity: 0; enabled: false; activeFocusOnTab: false; Accessible.ignored: true; text: qsTr("Audition") }
                }
                Tk.Button {
                    visible: root.referenceTakeId.length > 0
                    enabled: modelData.available !== false && String(modelData.takeId) !== root.referenceTakeId
                    text: qsTr("Compare")
                    tooltip: String(modelData.takeId) === root.referenceTakeId ? qsTr("This is the reference take") : qsTr("Request host comparison with the reference take")
                    onClicked: root.requestCompare(modelData.takeId)
                }
            }
        }
    }

    function refreshPaint() { compPaint.requestPaint() }
    onCompSegmentsChanged: compPaint.requestPaint(); onSampleBoundariesChanged: compPaint.requestPaint()
    onWidthChanged: compPaint.requestPaint(); onHeightChanged: compPaint.requestPaint()
    onEnabledChanged: if (!enabled) cancelSwipe()
    Keys.onEscapePressed: cancelSwipe() || stopAudition()
    Keys.onPressed: function(event) {
        const extend = (event.modifiers & Qt.ShiftModifier) !== 0
        let handled = true
        if (event.key === Qt.Key_Left) handled = root.moveCursor(-1, extend)
        else if (event.key === Qt.Key_Right) handled = root.moveCursor(1, extend)
        else if (event.key === Qt.Key_Home) handled = root.moveCursor(-root.sampleBoundaries.length, extend)
        else if (event.key === Qt.Key_End) handled = root.moveCursor(root.sampleBoundaries.length, extend)
        else if (event.key === Qt.Key_Up) handled = root.focusTake(-1)
        else if (event.key === Qt.Key_Down) handled = root.focusTake(1)
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            handled = root.requestProposalForRange(root.keyboardTakeId)
        else if (event.key === Qt.Key_Space) handled = root.requestAudition(root.keyboardTakeId)
        else if (event.key === Qt.Key_C) handled = root.requestCompare(root.keyboardTakeId)
        else handled = false
        event.accepted = handled
    }
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && root.hovered }
}
