// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-041 working fixture: handles, keyboard steps and the numeric inspector
// drive one Tk.SpatialTransform; guides are document content created by
// dragging from a ruler, snapped to, undone and saved with the document.
Rectangle {
    id: root
    width: 1000
    height: 620
    color: Tk.Theme.color.bg
    property var firstId: 0
    property var secondId: 0

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 600, 400)
        Component.onCompleted: {
            root.firstId = addRect(70, 60, 170, 120, "#4a81a1")
            root.secondId = addRect(290, 170, 180, 130, "#a98655")
            addGuide("vertical", 290)
            addGuide("horizontal", 60)
        }
    }
    Tk.ToolController { id: tools; document: document }
    Tk.CanvasViewState { id: pose }
    Tk.SnapModel {
        id: snap
        document: document
        viewState: pose
        guides: document.guides
    }
    Tk.SpatialTransform {
        id: groupTransform
        document: document
        tools: tools
        snapModel: snap
        viewState: pose
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row

        Tk.Flex {
            Tk.Flex.flex: 1
            direction: Tk.Flex.Column
            Tk.Flex {
                direction: Tk.Flex.Row
                Item {
                    implicitWidth: Tk.Theme.size.row
                    implicitHeight: Tk.Theme.size.row
                }
                Tk.Ruler {
                    id: topRuler
                    objectName: "inspectorTopRuler"
                    Tk.Flex.flex: 1
                    origin: view.width / 2 - view.centerX * view.zoom
                    pixelsPerUnit: view.zoom
                    majorEvery: 100
                    minorEvery: 10
                    // Dragging from the top ruler previews a horizontal guide.
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        onPositionChanged: function(mouse) {
                            const p = mapToItem(guideLayer, mouse.x, mouse.y)
                            guideLayer.previewAdd("horizontal", p.x, p.y)
                        }
                        onReleased: function(mouse) {
                            const p = mapToItem(guideLayer, mouse.x, mouse.y)
                            if (guideLayer.contains(p)) guideLayer.finishAdd("horizontal", p.x, p.y)
                            else guideLayer.cancelAdd()
                        }
                        onCanceled: guideLayer.cancelAdd()
                    }
                }
            }
            Tk.Flex {
                Tk.Flex.flex: 1
                direction: Tk.Flex.Row
                Tk.Ruler {
                    objectName: "inspectorLeftRuler"
                    orientation: Qt.Vertical
                    origin: view.height / 2 - view.centerY * view.zoom
                    pixelsPerUnit: view.zoom
                    majorEvery: 100
                    minorEvery: 10
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        onPositionChanged: function(mouse) {
                            const p = mapToItem(guideLayer, mouse.x, mouse.y)
                            guideLayer.previewAdd("vertical", p.x, p.y)
                        }
                        onReleased: function(mouse) {
                            const p = mapToItem(guideLayer, mouse.x, mouse.y)
                            if (guideLayer.contains(p)) guideLayer.finishAdd("vertical", p.x, p.y)
                            else guideLayer.cancelAdd()
                        }
                        onCanceled: guideLayer.cancelAdd()
                    }
                }
                Tk.CanvasView {
                    id: view
                    objectName: "inspectorFixtureCanvas"
                    Tk.Flex.flex: 1
                    document: document
                    toolController: tools
                    viewState: pose
                    backend: "software"
                    fitMode: "page"
                    accessibleName: qsTr("Transform inspector fixture")
                    Tk.GuideLayer {
                        id: guideLayer
                        objectName: "inspectorGuides"
                        anchors.fill: parent
                        surface: view
                        guideData: document.guides
                        onGuideAddRequested: function(orientation, position) {
                            document.addGuide(orientation, position)
                        }
                        onGuideMoveFinished: function(guideId, position) {
                            document.moveGuide(guideId, position)
                        }
                        onGuideDeleteRequested: function(guideId) { document.removeGuide(guideId) }
                    }
                    Tk.SelectionOverlay {
                        objectName: "inspectorHandles"
                        anchors.fill: parent
                        surface: view
                        selectionBounds: groupTransform.previewBounds
                        selectionCorners: groupTransform.previewCorners
                        onTransformRequested: function(handle, start, current, modifiers) {
                            groupTransform.update(handle, start, current, modifiers)
                        }
                        onTransformFinished: function(handle) { groupTransform.finish(handle) }
                        onTransformCanceled: function(handle) { groupTransform.cancel(handle) }
                        onTransformNudgeRequested: function(handle, dx, dy, modifiers) {
                            groupTransform.nudge(handle, dx, dy, modifiers)
                        }
                        onRotateStepRequested: function(degrees, modifiers) {
                            groupTransform.rotateBy(degrees, modifiers)
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "inspectorPanel"
            implicitWidth: Tk.Theme.size.labelWidth * 4
            Tk.Flex.shrink: 0
            color: Tk.Theme.color.panel
            Tk.Flex {
                anchors.fill: parent
                anchors.margins: Tk.Theme.space.md
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.SectionHeader { title: qsTr("Transform") }
                Tk.SpatialTransformEditor {
                    objectName: "inspectorEditor"
                    controller: groupTransform
                }
            }
        }
    }

    Component.onCompleted: Qt.callLater(function() {
        tools.selection = [root.firstId, root.secondId]
    })
}
