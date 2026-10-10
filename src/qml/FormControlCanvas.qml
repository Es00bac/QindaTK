// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "FormControls.js" as FormControls

// Internal part of FormDesigner (D-311): the form's positioned controls. In
// design mode it draws each as a FormControlShape and owns the gestures —
// click selects, drag moves, the eight handles resize (all snapped to
// `designer.gridSize`), arrows nudge, Shift+arrows size, Delete removes —
// and every finished gesture is ONE designer edit (one undo step with a
// design document). In preview and edit modes it hosts the live controls.
// A tab control shows the controls of its current page only; moving it
// moves them too (one edit).
Item {
    id: canvas
    // AGENT-NOTE: `var`, not the owner's type: a part typed with the control
    // that instantiates it is a cyclic type, which qmllint reports as
    // "cannot assign X to X" (the GridCellDelegate / GridFillHandle precedent).
    required property var designer
    readonly property bool designing: designer.mode === "design"
    readonly property var controls: designer.controls
    // Gesture preview {id, x, y, width, height}; never written to the design.
    property var preview: null
    // View state: the current page of each tab control (id -> index).
    property var pages: ({})
    readonly property real margin: Tk.Theme.space.xl
    readonly property rect extent: {
        let right = 0, bottom = 0
        for (let i = 0; i < controls.length; ++i) {
            const r = rectFor(controls[i])
            right = Math.max(right, r.x + r.width)
            bottom = Math.max(bottom, r.y + r.height)
        }
        return Qt.rect(0, 0, right, bottom)
    }

    objectName: "formCanvas"
    implicitWidth: Math.max(Tk.Theme.size.panelMinWidth, extent.width + margin)
    implicitHeight: Math.max(Tk.Theme.size.control * 4, extent.height + margin)
    activeFocusOnTab: designing
    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: designing ? qsTr("Form layout") : qsTr("Form")
    Accessible.description: designer.selectedControlId.length
        ? qsTr("Selected: %1. Arrows move, Shift and arrows size, Delete removes.").arg(String(designer.controlById(designer.selectedControlId).name || ""))
        : ""

    function rectFor(control) {
        return preview && control && preview.id === control.id ? preview : FormControls.rectOf(control || ({}))
    }
    function minimumSize(control) {
        return control.type === "line" ? 0 : Math.max(1, designer.gridSize)
    }
    function pageOf(tabId) { return pages[tabId] || 0 }
    function showPage(tabId, page) {
        const next = Object.assign({}, pages)
        next[tabId] = page
        pages = next
    }
    function shown(control) { return FormControls.onShownPage(control, pages) }
    // The topmost shown control under a canvas point, or null.
    function controlAt(point) {
        for (let i = controls.length - 1; i >= 0; --i) {
            const r = rectFor(controls[i])
            if (shown(controls[i]) && point.x >= r.x && point.x <= r.x + Math.max(r.width, Tk.Theme.size.grip)
                    && point.y >= r.y && point.y <= r.y + Math.max(r.height, Tk.Theme.size.grip)) return controls[i]
        }
        return null
    }
    // Commits a moved or resized rectangle; a tab control carries its pages' controls.
    function commit(control, rect) {
        preview = null
        const start = FormControls.rectOf(control)
        if (rect.x === start.x && rect.y === start.y && rect.width === start.width && rect.height === start.height) return true
        const dx = rect.x - start.x, dy = rect.y - start.y
        const children = controls.filter(function(c) { return c.parentId === control.id })
        if (control.type !== "tabControl" || children.length === 0 || (dx === 0 && dy === 0))
            return designer.updateControl(control.id, { x: rect.x, y: rect.y, width: rect.width, height: rect.height })
        return designer.replaceControls(controls.map(function(c) {
            if (c.id === control.id) return Object.assign({}, c, { x: rect.x, y: rect.y, width: rect.width, height: rect.height })
            if (c.parentId !== control.id) return c
            return Object.assign({}, c, { x: Math.max(0, (Number(c.x) || 0) + dx), y: Math.max(0, (Number(c.y) || 0) + dy) })
        }))
    }
    function nudge(dx, dy, size) {
        const control = designer.controlById(designer.selectedControlId)
        if (!control.id) return false
        const start = FormControls.rectOf(control)
        const step = Math.max(1, designer.gridSize)
        const rect = size ? FormControls.resized(start, "se", dx * step, dy * step, 0, minimumSize(control), minimumSize(control))
                          : FormControls.moved(start, dx * step, dy * step, 0)
        return commit(control, rect)
    }
    function cycle(forward) {
        const order = controls.filter(shown)
        if (order.length === 0) return false
        const at = order.findIndex(function(c) { return c.id === designer.selectedControlId })
        const next = at < 0 ? (forward ? 0 : order.length - 1) : at + (forward ? 1 : -1)
        if (next < 0 || next >= order.length) { designer.selectControl(""); return false }
        designer.selectControl(order[next].id)
        return true
    }

    Keys.onPressed: function(event) {
        if (!canvas.designing) return
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        let handled = true
        if (event.key === Qt.Key_Left) canvas.nudge(-1, 0, shift)
        else if (event.key === Qt.Key_Right) canvas.nudge(1, 0, shift)
        else if (event.key === Qt.Key_Up) canvas.nudge(0, -1, shift)
        else if (event.key === Qt.Key_Down) canvas.nudge(0, 1, shift)
        else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) handled = canvas.designer.removeControl(canvas.designer.selectedControlId)
        else if (event.key === Qt.Key_Escape && canvas.designer.selectedControlId.length) canvas.designer.selectControl("")
        else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) handled = canvas.cycle(event.key === Qt.Key_Tab && !shift)
        else handled = false
        event.accepted = handled
    }

    Rectangle {
        objectName: "formCanvasBackground"
        anchors.fill: parent
        color: canvas.designing ? Tk.Theme.color.canvas : "transparent"
        border.width: canvas.designing ? Tk.Theme.size.border : 0
        border.color: canvas.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.divider
    }
    TapHandler {
        enabled: canvas.designing
        onTapped: function(point) {
            if (!canvas.controlAt(point.position)) canvas.designer.selectControl("")
            canvas.forceActiveFocus()
        }
    }

    Repeater {
        model: canvas.controls
        delegate: Item {
            id: slot
            required property var modelData
            required property int index
            readonly property var rect: canvas.rectFor(modelData)
            readonly property bool selected: canvas.designer.selectedControlId === String(modelData.id)
            readonly property bool interactive: !canvas.designing
                && ["textBox", "comboBox", "listBox", "checkBox", "commandButton", "subform", "tabControl"].indexOf(modelData.type) >= 0
            objectName: "formControl_" + modelData.id
            visible: canvas.shown(modelData)
            x: rect.x
            y: rect.y
            width: Math.max(rect.width, modelData.type === "line" ? Tk.Theme.size.border : 0)
            height: Math.max(rect.height, modelData.type === "line" ? Tk.Theme.size.border : 0)
            z: modelData.parentId ? 1 : 0
            Accessible.role: Accessible.Graphic
            Accessible.name: String(modelData.name || modelData.type)
            Accessible.selected: selected

            Tk.FormControlShape {
                anchors.fill: parent
                visible: !slot.interactive
                control: slot.modelData
                live: !canvas.designing
                currentPage: canvas.pageOf(String(slot.modelData.id))
            }
            Loader {
                anchors.fill: parent
                active: slot.interactive
                sourceComponent: Tk.FormControlLive {
                    control: slot.modelData
                    designer: canvas.designer
                    currentPage: canvas.pageOf(String(slot.modelData.id))
                    onPageChosen: function(page) { canvas.showPage(String(slot.modelData.id), page) }
                }
            }
            // Design gestures: tap selects (on a tab control's strip it also
            // shows the page), drag moves.
            TapHandler {
                enabled: canvas.designing
                onTapped: function(point) {
                    canvas.designer.selectControl(String(slot.modelData.id))
                    canvas.forceActiveFocus()
                    if (slot.modelData.type === "tabControl" && point.position.y < Tk.Theme.size.tab) {
                        const pages = slot.modelData.pages || []
                        if (pages.length > 1) canvas.showPage(String(slot.modelData.id), (canvas.pageOf(String(slot.modelData.id)) + 1) % pages.length)
                    }
                }
            }
            DragHandler {
                id: mover
                enabled: canvas.designing
                target: null
                property var start: null
                onActiveChanged: {
                    if (active) {
                        canvas.designer.selectControl(String(slot.modelData.id))
                        start = FormControls.rectOf(slot.modelData)
                    } else if (start && canvas.preview) {
                        canvas.commit(slot.modelData, canvas.preview)
                        start = null
                    }
                }
                onTranslationChanged: {
                    if (!active || !start) return
                    const next = FormControls.moved(start, translation.x, translation.y, canvas.designer.gridSize)
                    next.id = String(slot.modelData.id)
                    canvas.preview = next
                }
            }
        }
    }

    // The selection frame and its eight resize handles, above every control.
    Item {
        id: selection
        objectName: "formSelection"
        readonly property var control: canvas.designer.controlById(canvas.designer.selectedControlId)
        readonly property var rect: canvas.rectFor(control)
        visible: canvas.designing && !!control.id && canvas.shown(control)
        x: rect.x
        y: rect.y
        width: rect.width
        height: rect.height
        z: 10
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Tk.Theme.size.border
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.accent
        }
        Repeater {
            model: FormControls.handles()
            delegate: Rectangle {
                id: handle
                required property var modelData
                readonly property string handleName: modelData[0]
                objectName: "formHandle_" + handleName
                width: Tk.Theme.size.grip
                height: width
                x: selection.width * modelData[1] - width / 2
                y: selection.height * modelData[2] - height / 2
                radius: Tk.Theme.radius.xs
                color: Tk.Theme.color.accent
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.accentContrast
                Accessible.role: Accessible.Grip
                Accessible.name: qsTr("Resize %1").arg(handleName)
                DragHandler {
                    target: null
                    property var start: null
                    onActiveChanged: {
                        if (active) start = FormControls.rectOf(selection.control)
                        else if (start && canvas.preview) { canvas.commit(selection.control, canvas.preview); start = null }
                    }
                    onTranslationChanged: {
                        if (!active || !start) return
                        const minimum = canvas.minimumSize(selection.control)
                        const next = FormControls.resized(start, handle.handleName, translation.x, translation.y,
                                                          canvas.designer.gridSize, minimum, minimum)
                        next.id = String(selection.control.id)
                        canvas.preview = next
                    }
                }
            }
        }
    }
}
