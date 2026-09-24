// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/canvas.md#synchronized-views, D-094): mainView owns
// the authoritative navigation pose; this overview owns only its fitted
// preview. Pointer, wheel and keyboard navigation write mainView's pose
// (one atomic centre change per step), never document data, selection or
// tool state. The preview is a second CanvasView on the same document and
// render providers, fitted to the content at its own low level of detail.
//
// AGENT-GUARD: the preview keeps its own private CanvasViewState and fits to
// content only. Sharing mainView's state, or fitting to the main viewport,
// would feed every navigation back into the overview transform (the box
// would move under the pointer while it is dragged).
Item {
    id: navigator
    objectName: "navigatorView"

    property Tk.CanvasView mainView
    property string tooltip: qsTr("Canvas navigator")
    // Fraction of the main view's visible extent one arrow key moves it;
    // plus/minus zoom by 1 + keyboardStep.
    property real keyboardStep: 0.1
    readonly property alias preview: previewView
    readonly property alias viewportOutline: outline

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("Drag to move the main canvas view; wheel or plus and minus to zoom it")
    Accessible.focusable: true
    Accessible.focused: activeFocus
    Accessible.onIncreaseAction: navigator.zoomBy(1 + keyboardStep)
    Accessible.onDecreaseAction: navigator.zoomBy(1 / (1 + keyboardStep))

    function fitPreview() {
        if (previewView.document && previewView.width > 0 && previewView.height > 0)
            previewView.fitDocument()
    }
    Component.onCompleted: Qt.callLater(fitPreview)
    onMainViewChanged: outline.requestPaint()

    // Centres the main view on the document point under `point` (preview
    // coordinates) in one pose change.
    function navigateAt(point) {
        if (!mainView || !mainView.viewState) return false
        const documentPoint = previewView.mapToDocument(point)
        mainView.viewState.setCenter(documentPoint.x, documentPoint.y)
        outline.requestPaint()
        return true
    }
    function panBy(fractionX, fractionY) {
        if (!mainView || !mainView.viewState) return false
        const visible = mainView.visibleDocumentRect
        mainView.viewState.setCenter(mainView.centerX + visible.width * fractionX,
                                     mainView.centerY + visible.height * fractionY)
        return true
    }
    function zoomBy(factor) {
        if (!mainView || !(factor > 0)) return false
        mainView.zoomAbout(mainView.width / 2, mainView.height / 2, factor)
        return true
    }

    Keys.onPressed: function(event) {
        const step = navigator.keyboardStep
        if (event.key === Qt.Key_Left) navigator.panBy(-step, 0)
        else if (event.key === Qt.Key_Right) navigator.panBy(step, 0)
        else if (event.key === Qt.Key_Up) navigator.panBy(0, -step)
        else if (event.key === Qt.Key_Down) navigator.panBy(0, step)
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) navigator.zoomBy(1 + step)
        else if (event.key === Qt.Key_Minus) navigator.zoomBy(1 / (1 + step))
        else return
        event.accepted = true
    }

    Tk.CanvasView {
        id: previewView
        objectName: "navigatorPreview"
        anchors.fill: parent
        document: navigator.mainView ? navigator.mainView.document : null
        fitMode: "content"
        interactive: false
        activeFocusOnTab: false
        canvasColor: Tk.Theme.color.canvas
        Accessible.ignored: true
        onDocumentChanged: Qt.callLater(navigator.fitPreview)
        onWidthChanged: Qt.callLater(navigator.fitPreview)
        onHeightChanged: Qt.callLater(navigator.fitPreview)
    }

    Canvas {
        id: outline
        objectName: "navigatorViewportOutline"
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const main = navigator.mainView
            if (!main || !main.viewState) return
            const points = [main.mapToDocument(Qt.point(0, 0)),
                            main.mapToDocument(Qt.point(main.width, 0)),
                            main.mapToDocument(Qt.point(main.width, main.height)),
                            main.mapToDocument(Qt.point(0, main.height))]
            const mapped = points.map(point => previewView.mapFromDocument(point))
            ctx.beginPath()
            ctx.moveTo(mapped[0].x, mapped[0].y)
            for (let i = 1; i < mapped.length; ++i)
                ctx.lineTo(mapped[i].x, mapped[i].y)
            ctx.closePath()
            ctx.strokeStyle = Tk.Theme.color.accent
            ctx.lineWidth = Tk.Theme.size.focusRing
            ctx.stroke()
        }
    }

    MouseArea {
        objectName: "navigatorPointerArea"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        Accessible.ignored: true
        onPressed: mouse => {
            navigator.forceActiveFocus()
            navigator.navigateAt(Qt.point(mouse.x, mouse.y))
        }
        onPositionChanged: mouse => {
            if (pressed) navigator.navigateAt(Qt.point(mouse.x, mouse.y))
        }
        onWheel: wheel => {
            const main = navigator.mainView
            if (!main || wheel.angleDelta.y === 0) return
            navigator.zoomBy(wheel.angleDelta.y > 0 ? main.wheelZoomStep : 1 / main.wheelZoomStep)
        }
    }

    Rectangle {
        objectName: "navigatorFocusRing"
        anchors.fill: parent
        color: "transparent"
        border.color: Tk.Theme.color.focus
        border.width: Tk.Theme.size.focusRing
        visible: navigator.activeFocus
    }

    Connections {
        target: navigator.mainView
        function onViewChanged() { outline.requestPaint() }
        function onWidthChanged() { outline.requestPaint() }
        function onHeightChanged() { outline.requestPaint() }
    }
    Connections {
        target: previewView
        function onViewChanged() { outline.requestPaint() }
        function onWidthChanged() { outline.requestPaint() }
        function onHeightChanged() { outline.requestPaint() }
    }
}
