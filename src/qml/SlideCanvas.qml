// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#slide-authoring-and-presentation, docs/slides.md#canvas): the
// editable slide surface on the WP-042 canvas stack. A Tk.SlideCanvasDocument
// adapts `deck` + `slideId` to a Tk.CanvasView; Tk.ToolController owns the
// selection and the select/move/marquee gestures, Tk.SpatialTransform the
// resize/rotate/nudge gestures with Tk.SnapModel (objects, slide edges and
// centre guides). Every change reaches the deck as ONE SlideDeck undo step
// (the adapter's commit, insertObject, removeObjects, setObjectsFill,
// applyMaster). Text edits go through deck.beginTextEdit and a
// Tk.RichTextEditor, so formatting survives (never setObjectText).
// AGENT-NOTE: the canvas pixels are Slides::renderObjectImage, the output
// painter's own object code; only placeholder prompts are canvas-only.
Item {
    id: control
    property Tk.SlideDeck deck: null
    property string slideId: ""
    property var capabilities: ({ canEdit: true, canInsertText: true, canInsertShape: true, canApplyMaster: true })
    property bool snapping: true
    // Document colours (deck data, not theme roles): the deck's own palette.
    property var fillColors: deck && deck.revision.length ? deck.documentColors() : []
    property string tooltip: qsTr("Edit the selected slide objects")
    readonly property alias canvasDocument: slideDocument
    readonly property alias toolController: tools
    readonly property alias spatialTransform: transform
    readonly property alias view: canvas
    readonly property alias snapModel: snap
    readonly property alias textEditor: editor
    readonly property var selectedObjectIds: slideDocument.revision > 0 ? slideDocument.slideObjectIds(tools.selection) : []
    readonly property bool textEditing: !!control.deck && control.deck.textEditing && editor.document !== null
    readonly property var currentSlide: control.deck && control.deck.revision.length ? control.deck.slide(control.slideId) : ({})
    readonly property var masterChoices: control.deck && control.deck.revision.length ? control.deck.masterChoices() : []
    readonly property var selectedObject: findObject(selectedObjectIds.length === 1 ? selectedObjectIds[0] : "")
    readonly property bool canEditNow: enabled && !!deck && capabilities.canEdit && !textEditing

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: qsTr("Slide %1, %2 objects, %3 selected").arg(slideId).arg(slideDocument.objectCount)
                               .arg(selectedObjectIds.length)

    function findObject(id) {
        if (!deck || !id.length) return null
        const objects = deck.revision.length ? deck.objects(slideId) : []
        for (let i = 0; i < objects.length; ++i) if (objects[i].id === id) return objects[i]
        return null
    }
    function select(ids) { tools.selection = slideDocument.canvasObjectIds(ids) }
    function insert(kind) {
        if (!canEditNow || !(kind === "text" ? capabilities.canInsertText : capabilities.canInsertShape)) return ""
        const size = deck.slideSize
        const box = { x: size.width * 0.35, y: size.height * 0.35,
                      width: size.width * (kind === "text" ? 0.3 : 0.2), height: size.height * (kind === "text" ? 0.12 : 0.2) }
        const id = deck.insertObject(slideId, kind, box, "")
        if (id.length) select([id])
        return id
    }
    function deleteSelection() { if (canEditNow && selectedObjectIds.length) deck.removeObjects(selectedObjectIds) }
    function setFill(color) { if (canEditNow && selectedObjectIds.length) deck.setObjectsFill(selectedObjectIds, color) }
    function beginTextEdit() {
        const object = selectedObject
        if (!canEditNow || !object || object.kind !== "text" || object.locked) return false
        const document = deck.beginTextEdit(object.id)
        if (!document) return false
        editor.document = document
        editor.forceActiveFocus()
        return true
    }
    function commitTextEdit() {
        const ok = deck ? deck.commitTextEdit() : false
        editor.document = null
        canvas.forceActiveFocus()
        return ok
    }
    function cancelTextEdit() {
        if (deck) deck.cancelTextEdit()
        editor.document = null
        canvas.forceActiveFocus()
    }
    // View rectangle of a document rectangle (axis-aligned; text boxes edit unrotated).
    function viewRect(bounds) {
        const a = canvas.mapFromDocument(Qt.point(bounds.x, bounds.y))
        const b = canvas.mapFromDocument(Qt.point(bounds.x + bounds.width, bounds.y + bounds.height))
        return Qt.rect(Math.min(a.x, b.x), Math.min(a.y, b.y), Math.abs(b.x - a.x), Math.abs(b.y - a.y))
    }

    onSlideIdChanged: { if (textEditing) cancelTextEdit(); tools.selection = [] }
    Component.onDestruction: if (textEditing) deck.cancelTextEdit()

    Tk.SlideCanvasDocument { id: slideDocument; deck: control.deck; slideId: control.slideId }
    Tk.ToolController { id: tools; document: slideDocument; activeTool: "select" }
    // AGENT-GUARD: SnapModel measures its tolerance in view pixels and
    // snaps nothing without a view pose; the view, snapping and keyboard
    // nudges share this one pose (tst_slide_authoring::test_drag_move_snaps_to_the_centre_guide).
    Tk.CanvasViewState { id: pose }
    Tk.SnapModel { id: snap; document: slideDocument; viewState: pose; enabled: control.snapping; guides: slideDocument.guides }
    Tk.SpatialTransform { id: transform; objectName: "slideTransform"; document: slideDocument; tools: tools; snapModel: snap; viewState: pose }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            objectName: "slideCanvasToolbar"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "slideInsertText"
                text: qsTr("Text")
                iconName: "type"
                tooltip: qsTr("Insert a text box on this slide")
                enabled: control.canEditNow && control.capabilities.canInsertText
                onClicked: control.insert("text")
            }
            Tk.Button {
                objectName: "slideInsertShape"
                text: qsTr("Shape")
                iconName: "shapes"
                tooltip: qsTr("Insert a rectangle shape")
                enabled: control.canEditNow && control.capabilities.canInsertShape
                onClicked: control.insert("shape")
            }
            Tk.ToolSeparator {}
            Tk.Button {
                objectName: "slideEditText"
                text: control.textEditing ? qsTr("Done") : qsTr("Edit text")
                iconName: control.textEditing ? "check" : "text-cursor-input"
                tooltip: control.textEditing ? qsTr("Store the text edit as one undo step")
                                             : qsTr("Edit the selected text box with formatting")
                enabled: control.textEditing || (control.canEditNow && !!control.selectedObject
                                                 && control.selectedObject.kind === "text")
                onClicked: control.textEditing ? control.commitTextEdit() : control.beginTextEdit()
            }
            Tk.IconButton {
                objectName: "slideCancelText"
                visible: control.textEditing
                iconName: "x"
                tooltip: qsTr("Discard the text edit")
                onClicked: control.cancelTextEdit()
            }
            Tk.ColorButton {
                objectName: "slideFill"
                color: control.selectedObject && control.selectedObject.fill.length ? control.selectedObject.fill : Tk.Theme.color.accent
                colors: control.fillColors
                tooltip: qsTr("Fill the selected objects")
                enabled: control.canEditNow && control.selectedObjectIds.length > 0
                onColorSelected: function(color) { control.setFill(String(color)) }
            }
            Tk.IconButton {
                objectName: "slideDelete"
                iconName: "trash-2"
                tooltip: qsTr("Delete the selected objects")
                enabled: control.canEditNow && control.selectedObjectIds.length > 0
                onClicked: control.deleteSelection()
            }
            Tk.IconButton {
                objectName: "slideSnap"
                iconName: "magnet"
                checkable: true
                checked: control.snapping
                tooltip: qsTr("Snap to objects, slide edges and centre guides")
                onToggled: control.snapping = checked
            }
            Tk.ToolSeparator {}
            Tk.ComboBox {
                id: masterPicker
                objectName: "slideMasterPicker"
                model: control.masterChoices
                textRole: "label"
                currentIndex: {
                    for (let i = 0; i < control.masterChoices.length; ++i)
                        if (control.masterChoices[i].id === control.currentSlide.masterId) return i
                    return -1
                }
                tooltip: qsTr("Choose a slide master")
                enabled: control.canEditNow && control.capabilities.canApplyMaster && control.masterChoices.length > 0
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "slideApplyMaster"
                text: qsTr("Apply master")
                tooltip: qsTr("Apply the chosen master to this slide, keeping its deliberate overrides")
                enabled: control.canEditNow && masterPicker.currentIndex >= 0 && control.capabilities.canApplyMaster
                onClicked: control.deck.applyMaster(control.masterChoices[masterPicker.currentIndex].id, [control.slideId], "")
            }
            Tk.IconButton {
                objectName: "slideFit"
                iconName: "scan"
                tooltip: qsTr("Fit the slide in the view")
                onClicked: canvas.fitDocument()
            }
            Tk.Caption { objectName: "slideZoom"; text: qsTr("%1%").arg(Math.round(canvas.zoom * 100)); mono: true }
        }

        Tk.CanvasView {
            id: canvas
            objectName: "slideCanvasView"
            document: slideDocument
            toolController: tools
            viewState: pose
            fitMode: "page"
            accessibleName: control.tooltip
            focus: true
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Keys.onPressed: function(event) {
                if (control.textEditing) return
                if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    control.deleteSelection(); event.accepted = true
                } else if (event.key === Qt.Key_F2 || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    event.accepted = control.beginTextEdit()
                }
            }

            Tk.GuideLayer {
                objectName: "slideGuides"
                anchors.fill: parent
                surface: canvas
                visible: control.snapping
                guideData: slideDocument.guides
                guideColor: Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.muted)
                geometryRevision: slideDocument.revision
                enabled: false // centre guides are fixed; they only guide and snap
            }
            // Dragging inside the selection moves it through SpatialTransform,
            // so the move snaps; a click without a drag picks what is under it.
            MouseArea {
                id: moveArea
                objectName: "slideMoveArea"
                readonly property rect bounds: control.viewRect(transform.selectionBounds)
                visible: control.canEditNow && tools.selection.length > 0 && !transform.active
                         || moveArea.pressed
                x: bounds.x
                y: bounds.y
                width: bounds.width
                height: bounds.height
                property point startDocument
                property bool moved: false
                onPressed: function(mouse) {
                    canvas.forceActiveFocus()
                    moved = false
                    startDocument = canvas.mapToDocument(mapToItem(canvas, mouse.x, mouse.y))
                }
                onPositionChanged: function(mouse) {
                    const current = canvas.mapToDocument(mapToItem(canvas, mouse.x, mouse.y))
                    moved = moved || Math.hypot(current.x - startDocument.x, current.y - startDocument.y) * canvas.zoom >= 2
                    if (moved) transform.update("move", startDocument, current, mouse.modifiers)
                }
                onReleased: function(mouse) {
                    if (moved) { transform.finish("move"); return }
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const hits = canvas.hitTest(point.x, point.y)
                    if (hits.length) tools.selection = [hits[0].objectId]
                }
                onCanceled: transform.cancel("move")
                onDoubleClicked: control.beginTextEdit()
            }
            Tk.SelectionOverlay {
                objectName: "slideSelectionHandles"
                anchors.fill: parent
                surface: canvas
                visible: tools.selection.length > 0 && !control.textEditing
                handlesEnabled: control.canEditNow
                selectionBounds: transform.previewBounds
                selectionCorners: transform.previewCorners
                geometryRevision: slideDocument.revision
                onTransformRequested: function(handle, start, current, modifiers) { transform.update(handle, start, current, modifiers) }
                onTransformFinished: function(handle) { transform.finish(handle) }
                onTransformCanceled: function(handle) { transform.cancel(handle) }
                onTransformNudgeRequested: function(handle, dx, dy, modifiers) { transform.nudge(handle, dx, dy, modifiers) }
                onRotateStepRequested: function(degrees, modifiers) { transform.rotateBy(degrees, modifiers) }
            }
            Tk.RichTextEditor {
                id: editor
                objectName: "slideTextEditor"
                readonly property rect box: control.selectedObject ? control.viewRect(control.selectedObject.bounds) : Qt.rect(0, 0, 0, 0)
                visible: control.textEditing
                x: box.x
                y: box.y
                width: Math.max(Tk.Theme.size.fieldWidth, box.width)
                rows: 2
                tooltip: qsTr("Rich text of the selected text box")
                Keys.onEscapePressed: control.cancelTextEdit()
            }
        }
        Tk.Caption {
            objectName: "slideCanvasStatus"
            text: control.deck && control.deck.lastError.length ? control.deck.lastError
                : control.textEditing ? qsTr("Editing text · Done stores one undo step, Escape discards")
                : control.selectedObjectIds.length
                  ? qsTr("%1 selected · drag to move, handles resize and rotate, Delete removes").arg(control.selectedObjectIds.length)
                  : qsTr("Click an object to select it; Shift adds to the selection")
            elide: Text.ElideRight
        }
    }
}
