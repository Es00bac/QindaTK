// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/canvas.md#artboards): the host owns board records and
// commits; this layer only maps, paints, and reports document-space gestures.
Item {
    id: root
    objectName: "artboardView"

    property Item surface
    // Records: { id, rect, name, background }. IDs must remain stable.
    property var boardData: []
    property string activeBoardId: ""
    property string tooltip: qsTr("Artboards")
    property color frameColor: Tk.Theme.color.controlBorder
    property color activeFrameColor: Tk.Theme.color.accent
    property real lineWidth: Tk.Theme.size.border
    property real handleSize: Tk.Theme.size.controlSm
    property int geometryRevision: 0
    readonly property var boardPolygons: mappedBoards()

    signal boardActivateRequested(string boardId)
    signal boardArrangePreview(string boardId, point startDocumentPoint,
                               point currentDocumentPoint)
    signal boardArrangeFinished(string boardId, point currentDocumentPoint)
    signal boardArrangeCanceled(string boardId, point originalDocumentPoint)
    signal boardResizePreview(string boardId, point startDocumentPoint,
                              point currentDocumentPoint)
    signal boardResizeFinished(string boardId, point currentDocumentPoint)
    signal boardResizeCanceled(string boardId, point originalDocumentPoint)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: qsTr("Document artboards")
    activeFocusOnTab: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && gesture.active) {
            cancelGesture()
            event.accepted = true
        }
    }

    function validRect(rect) {
        return rect && Number.isFinite(Number(rect.x)) && Number.isFinite(Number(rect.y))
                && Number(rect.width) > 0 && Number(rect.height) > 0
    }

    function documentCorners(rect) {
        return [Qt.point(rect.x, rect.y), Qt.point(rect.x + rect.width, rect.y),
                Qt.point(rect.x + rect.width, rect.y + rect.height),
                Qt.point(rect.x, rect.y + rect.height)]
    }

    function mappedBoards() {
        const revision = geometryRevision
        if (!surface) return []
        const result = []
        for (let i = 0; i < boardData.length; ++i) {
            const board = boardData[i]
            if (!board || !board.id || !validRect(board.rect)) continue
            const points = documentCorners(board.rect).map(point => {
                const inSurface = surface.mapFromDocument(point)
                return root.mapFromItem(surface, inSurface.x, inSurface.y)
            })
            result.push({ id: String(board.id), name: String(board.name || board.id),
                          background: board.background || Tk.Theme.color.canvas,
                          rect: board.rect, points: points })
        }
        return result
    }

    function documentPoint(localPoint) {
        if (!surface) return Qt.point(0, 0)
        const inSurface = root.mapToItem(surface, localPoint.x, localPoint.y)
        return surface.mapToDocument(inSurface)
    }

    function contains(point, rect) {
        return point.x >= rect.x && point.x <= rect.x + rect.width
                && point.y >= rect.y && point.y <= rect.y + rect.height
    }

    function boardAt(localPoint) {
        const point = documentPoint(localPoint)
        for (let i = boardData.length - 1; i >= 0; --i) {
            const board = boardData[i]
            if (board && board.id && validRect(board.rect)
                    && (contains(point, board.rect)
                        || (String(board.id) === activeBoardId && resizeHit(board, localPoint))))
                return board
        }
        return null
    }

    function resizeHit(board, localPoint) {
        const corners = documentCorners(board.rect)
        const corner = corners[2]
        const inSurface = surface.mapFromDocument(corner)
        const mapped = root.mapFromItem(surface, inSurface.x, inSurface.y)
        return Math.hypot(localPoint.x - mapped.x, localPoint.y - mapped.y)
                <= handleSize * 0.75
    }

    function cancelGesture() {
        if (!gesture.active || gesture.canceled) return
        gesture.canceled = true
        if (gesture.dragging) {
            if (gesture.kind === "resize")
                boardResizeCanceled(gesture.boardId, gesture.startDocumentPoint)
            else
                boardArrangeCanceled(gesture.boardId, gesture.startDocumentPoint)
        }
    }

    function finishGesture(point) {
        if (!gesture.active) return
        if (!gesture.canceled && gesture.dragging) {
            if (gesture.kind === "resize")
                boardResizeFinished(gesture.boardId, point)
            else
                boardArrangeFinished(gesture.boardId, point)
        } else if (!gesture.canceled && gesture.boardId.length) {
            boardActivateRequested(gesture.boardId)
        }
        gesture.active = false
    }

    Connections {
        target: root.surface
        function onViewChanged() { root.geometryRevision += 1; boardFrame.requestPaint() }
    }
    onBoardDataChanged: boardFrame.requestPaint()
    onActiveBoardIdChanged: boardFrame.requestPaint()
    onGeometryRevisionChanged: boardFrame.requestPaint()
    onFrameColorChanged: boardFrame.requestPaint()
    onActiveFrameColorChanged: boardFrame.requestPaint()
    onLineWidthChanged: boardFrame.requestPaint()

    Canvas {
        id: boardFrame
        objectName: "artboardFrames"
        anchors.fill: parent
        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            const boards = root.boardPolygons
            for (let i = 0; i < boards.length; ++i) {
                const board = boards[i]
                const points = board.points
                context.beginPath()
                context.moveTo(points[0].x, points[0].y)
                for (let j = 1; j < points.length; ++j)
                    context.lineTo(points[j].x, points[j].y)
                context.closePath()
                context.fillStyle = board.background
                context.fill()
                context.strokeStyle = board.id === root.activeBoardId
                        ? root.activeFrameColor : root.frameColor
                context.lineWidth = root.lineWidth
                context.stroke()
                if (board.id === root.activeBoardId) {
                    const corner = points[2]
                    const half = root.handleSize / 2
                    context.fillStyle = root.activeFrameColor
                    context.fillRect(corner.x - half, corner.y - half,
                                     root.handleSize, root.handleSize)
                }
            }
        }
    }

    Repeater {
        model: root.boardData
        delegate: Item {
            required property var modelData
            required property int index
            objectName: "artboardLabel_" + String(modelData.id || index)
            Accessible.role: Accessible.Button
            Accessible.name: String(modelData.name || modelData.id || qsTr("Artboard"))
            Accessible.description: qsTr("Artboard")
            Accessible.focusable: true
            Accessible.onPressAction: root.boardActivateRequested(String(modelData.id || ""))
            activeFocusOnTab: true
            property var geometry: {
                const boards = root.boardPolygons
                for (let i = 0; i < boards.length; ++i)
                    if (boards[i].id === String(modelData.id || "")) return boards[i]
                return null
            }
            property var xs: geometry ? geometry.points.map(point => point.x) : []
            property var ys: geometry ? geometry.points.map(point => point.y) : []
            x: xs.length ? Math.min.apply(null, xs) : 0
            y: ys.length ? Math.min.apply(null, ys) : 0
            width: xs.length ? Math.max.apply(null, xs) - x : 0
            height: ys.length ? Math.max.apply(null, ys) - y : 0
            visible: geometry !== null
            Tk.Label {
                objectName: "artboardName"
                text: String(modelData.name || modelData.id || qsTr("Artboard"))
                x: parent.geometry ? parent.geometry.points[0].x - parent.x : 0
                y: parent.geometry ? parent.geometry.points[0].y - parent.y
                                     - height - Tk.Theme.space.xs : 0
                color: Tk.Theme.color.accent
            }
            Rectangle {
                objectName: "artboardFocusRing_" + String(modelData.id || index)
                anchors.fill: parent
                color: "transparent"
                border.color: Tk.Theme.color.focus
                border.width: Tk.Theme.size.focusRing
                visible: parent.activeFocus
            }
        }
    }

    QtObject {
        id: gesture
        property bool active: false
        property bool canceled: false
        property bool dragging: false
        property string boardId: ""
        property string kind: "arrange"
        property point startViewPoint: Qt.point(0, 0)
        property point startDocumentPoint: Qt.point(0, 0)
    }

    MouseArea {
        id: pointer
        objectName: "artboardGestureSurface"
        anchors.fill: parent
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        onPressed: function(mouse) {
            const board = root.boardAt(Qt.point(mouse.x, mouse.y))
            gesture.active = true
            gesture.canceled = false
            gesture.dragging = false
            gesture.boardId = board ? String(board.id) : ""
            gesture.kind = board && String(board.id) === root.activeBoardId
                    && root.resizeHit(board, Qt.point(mouse.x, mouse.y))
                    ? "resize" : "arrange"
            gesture.startViewPoint = Qt.point(mouse.x, mouse.y)
            gesture.startDocumentPoint = root.documentPoint(gesture.startViewPoint)
            root.forceActiveFocus()
            mouse.accepted = true
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !gesture.active || gesture.canceled || !gesture.boardId.length)
                return
            const point = root.documentPoint(Qt.point(mouse.x, mouse.y))
            if (!gesture.dragging
                    && Math.hypot(mouse.x - gesture.startViewPoint.x,
                                  mouse.y - gesture.startViewPoint.y) < Tk.Theme.size.controlSm / 2)
                return
            gesture.dragging = true
            if (gesture.kind === "resize")
                root.boardResizePreview(gesture.boardId, gesture.startDocumentPoint, point)
            else
                root.boardArrangePreview(gesture.boardId, gesture.startDocumentPoint, point)
        }
        onReleased: function(mouse) {
            root.finishGesture(root.documentPoint(Qt.point(mouse.x, mouse.y)))
        }
        onCanceled: {
            root.cancelGesture()
            gesture.active = false
        }
    }

    Tk.ToolTip { text: root.tooltip; visible: pointer.containsMouse }
}
