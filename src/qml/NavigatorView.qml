// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/canvas.md#synchronized-views): mainView owns the
// authoritative navigation pose; this overview owns only its fitted preview.
// Pointer navigation changes mainView.viewState center, never document data.
Item {
    id: navigator
    objectName: "navigatorView"

    property Tk.CanvasView mainView
    property string tooltip: qsTr("Canvas navigator")
    readonly property alias preview: previewView
    readonly property alias viewportOutline: outline

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("Drag to move the main canvas view")
    Accessible.focusable: true

    function fitPreview() {
        if (previewView.document && previewView.width > 0 && previewView.height > 0)
            previewView.fitDocument()
    }
    Component.onCompleted: Qt.callLater(fitPreview)
    onMainViewChanged: outline.requestPaint()

    Tk.CanvasView {
        id: previewView
        objectName: "navigatorPreview"
        anchors.fill: parent
        document: navigator.mainView ? navigator.mainView.document : null
        fitMode: "content"
        interactive: false
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

    function navigateAt(point) {
        if (!mainView || !mainView.viewState) return
        const documentPoint = previewView.mapToDocument(point)
        mainView.viewState.centerX = documentPoint.x
        mainView.viewState.centerY = documentPoint.y
        outline.requestPaint()
    }

    MouseArea {
        objectName: "navigatorPointerArea"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        Accessible.ignored: true
        onPressed: mouse => navigator.navigateAt(Qt.point(mouse.x, mouse.y))
        onPositionChanged: mouse => {
            if (pressed) navigator.navigateAt(Qt.point(mouse.x, mouse.y))
        }
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
