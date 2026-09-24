// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    objectName: "rasterStrokeInput"
    property var document: null
    property string layerId: ""
    property bool paintMask: false
    property color color: Tk.Theme.color.accent
    property var brushPreset: ({})
    property Item canvasTarget: root
    // Optional function(localPoint) -> raster pixel point, e.g. through
    // CanvasView.mapToDocument and TiledRasterDocument.canvasToRaster.
    property var pointMapper: null
    property real mousePressure: 0.6
    property bool inputEnabled: true
    property bool strokeActive: false
    property string lastError: ""
    property var strokeDocument: null
    property string strokeLayerId: ""
    signal strokeCommitted()
    signal strokeCanceled()
    signal strokeFailed(string message)

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Raster painting canvas")
    focus: true
    activeFocusOnTab: true

    function mappedPosition(sample) {
        if (typeof root.pointMapper === "function") return root.pointMapper(sample.position)
        const target = root.canvasTarget
        if (!target || target.width <= 0 || target.height <= 0) return Qt.point(0, 0)
        return Qt.point(sample.position.x * root.document.width / target.width,
                        sample.position.y * root.document.height / target.height)
    }

    function cancelStroke() {
        if (!root.strokeActive) return
        const activeDocument = root.strokeDocument
        root.strokeActive = false
        root.strokeDocument = null
        root.strokeLayerId = ""
        if (activeDocument) activeDocument.cancelStroke()
        root.strokeCanceled()
    }

    function failStroke(message) {
        root.lastError = message
        const activeDocument = root.strokeDocument
        root.strokeActive = false
        root.strokeDocument = null
        root.strokeLayerId = ""
        if (activeDocument) activeDocument.cancelStroke()
        root.strokeFailed(message)
    }

    function begin(sample) {
        if (!root.inputEnabled || !root.document || !root.canvasTarget || root.layerId.length === 0)
            return
        const activeDocument = root.document
        const activeLayer = root.layerId
        if (!activeDocument.beginStroke(activeLayer, root.paintMask, root.color, root.brushPreset)) {
            root.failStroke(activeDocument.error)
            return
        }
        root.strokeDocument = activeDocument
        root.strokeLayerId = activeLayer
        root.strokeActive = true
        root.lastError = ""
        const position = root.mappedPosition(sample)
        if (!activeDocument.sampleStroke(position.x, position.y,
                                         sample.pressure, sample.tiltX, sample.tiltY))
            root.failStroke(activeDocument.error)
        root.forceActiveFocus()
    }

    function addSample(sample) {
        if (!root.strokeActive) return
        if (root.strokeDocument !== root.document || root.strokeLayerId !== root.layerId) {
            root.cancelStroke()
            return
        }
        const position = root.mappedPosition(sample)
        if (!root.strokeDocument.sampleStroke(position.x, position.y, sample.pressure,
                                              sample.tiltX, sample.tiltY))
            root.failStroke(root.strokeDocument.error)
    }

    function finish(sample) {
        if (!root.strokeActive) return
        root.addSample(sample)
        if (!root.strokeActive) return
        const activeDocument = root.strokeDocument
        root.strokeActive = false
        root.strokeDocument = null
        root.strokeLayerId = ""
        if (activeDocument.endStroke()) {
            root.lastError = ""
            root.strokeCommitted()
        } else {
            root.lastError = activeDocument.error
            root.strokeFailed(root.lastError)
        }
    }

    onDocumentChanged: cancelStroke()
    onLayerIdChanged: cancelStroke()
    onPaintMaskChanged: cancelStroke()
    onColorChanged: cancelStroke()
    // AGENT-GUARD (CAN-15): no brushPreset handler. The document captured the
    // preset at beginStroke, so a preset switch applies to the next stroke.
    onCanvasTargetChanged: cancelStroke()
    onPointMapperChanged: cancelStroke()
    onInputEnabledChanged: if (!inputEnabled) cancelStroke()
    onEnabledChanged: if (!enabled) cancelStroke()

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && root.strokeActive) {
            root.cancelStroke()
            event.accepted = true
        }
    }

    Tk.StylusHandler {
        objectName: "rasterStylusHandler"
        target: root.canvasTarget
        enabled: root.enabled && root.inputEnabled
        mousePressure: root.mousePressure
        acceptedButtons: Qt.LeftButton
        onBegan: function(sample) { root.begin(sample) }
        onMoved: function(sample) { root.addSample(sample) }
        onEnded: function(sample) { root.finish(sample) }
        onCanceled: root.cancelStroke()
        onActiveChanged: if (!active) root.cancelStroke()
    }

    Tk.ToolTip { text: qsTr("Paint with a pen or left mouse button · Escape cancels a stroke"); visible: hover.hovered }
    HoverHandler { id: hover; objectName: "rasterStrokeHover" }
}
