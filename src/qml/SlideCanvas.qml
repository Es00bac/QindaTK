// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host snapshots own geometry and text. Gestures preview locally, then report
// one request; the surface resumes from the host snapshot until it is acked.
// AGENT-CONTRACT (docs/slides.md#output, D-160): with `deck` set, the page is
// painted by Tk.SlideFrameView (the audience/export painter, every build
// complete) and object delegates draw only placeholder and selection chrome
// on top; without it the delegates draw a schematic of the host `objects`.
Item {
    id: control
    property Tk.SlideDeck deck: null
    readonly property bool rendered: !!control.deck && control.slideId.length > 0
    property string documentId: ""
    property string slideId: ""
    property string masterId: ""
    property var slideSize: ({ width: 960, height: 540 })
    property var objects: [] // [{id,kind,text,bounds,fill,locked,visible}]
    property var selectedObjectIds: []
    property var masterChoices: [] // [{id,label}]
    property var capabilities: ({ canEdit: true, canInsertText: true, canInsertShape: true, canApplyMaster: true })
    property real zoom: 1
    property string tooltip: qsTr("Edit the selected slide objects")
    readonly property string selectedText: selectedTextObject ? String(selectedTextObject.text || "") : ""
    readonly property var selectedTextObject: findObject(selectedObjectIds.length ? selectedObjectIds[0] : "")
    readonly property real pageScale: Math.max(0.1, Math.min((width - Tk.Theme.space.xl * 2) / Math.max(1, slideSize.width),
                                                            (height - Tk.Theme.size.control * 3 - Tk.Theme.space.xl * 2) / Math.max(1, slideSize.height)) * zoom)

    signal selectionRequested(string objectId)
    signal objectMoveRequested(string objectId, var bounds)
    signal objectTextEditRequested(string objectId, string text)
    signal objectInsertRequested(string kind, var bounds, string requestId)
    signal masterApplyRequested(string masterId, string slideId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: qsTr("Slide %1, %2 objects, %3 percent zoom").arg(slideId).arg(objects.length).arg(Math.round(zoom * 100))

    function findObject(id) {
        for (let i = 0; i < objects.length; ++i) if (objects[i].id === id) return objects[i]
        return null
    }
    function requestInsert(kind) {
        if (!enabled || !capabilities.canEdit || !(kind === "text" ? capabilities.canInsertText : capabilities.canInsertShape)) return
        const size = slideSize
        const box = { x: size.width * 0.35, y: size.height * 0.35,
                      width: kind === "text" ? size.width * 0.3 : size.width * 0.2,
                      height: kind === "text" ? size.height * 0.12 : size.height * 0.2 }
        objectInsertRequested(kind, box, slideId + ":insert:" + Date.now())
    }

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
                tooltip: qsTr("Insert a text object on this slide")
                enabled: control.capabilities.canEdit && control.capabilities.canInsertText
                onClicked: control.requestInsert("text")
            }
            Tk.Button {
                objectName: "slideInsertShape"
                text: qsTr("Shape")
                iconName: "shapes"
                tooltip: qsTr("Insert a rectangle shape")
                enabled: control.capabilities.canEdit && control.capabilities.canInsertShape
                onClicked: control.requestInsert("shape")
            }
            Tk.ToolSeparator {}
            Tk.ComboBox {
                id: masterPicker
                objectName: "slideMasterPicker"
                model: control.masterChoices
                textRole: "label"
                currentIndex: {
                    for (let i = 0; i < control.masterChoices.length; ++i)
                        if (control.masterChoices[i].id === control.masterId) return i
                    return -1
                }
                tooltip: qsTr("Choose a slide master")
                enabled: control.capabilities.canApplyMaster && control.masterChoices.length > 0
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "slideApplyMaster"
                text: qsTr("Apply master")
                tooltip: qsTr("Apply the selected master to this slide")
                enabled: masterPicker.currentIndex >= 0 && control.capabilities.canApplyMaster
                onClicked: control.masterApplyRequested(control.masterChoices[masterPicker.currentIndex].id, control.slideId)
            }
            Tk.NumberField {
                objectName: "slideZoom"
                value: control.zoom * 100
                from: 25
                to: 200
                stepSize: 10
                suffix: "%"
                decimals: 0
                tooltip: qsTr("Slide canvas zoom")
                onValueModified: function(value) { control.zoom = value / 100 }
            }
        }
        Item {
            objectName: "slideCanvasSurface"
            Accessible.ignored: true
            Tk.Flex.grow: 1
            Rectangle {
                id: page
                width: Math.max(1, control.slideSize.width * control.pageScale)
                height: Math.max(1, control.slideSize.height * control.pageScale)
                anchors.centerIn: parent
                color: Tk.Theme.color.surface
                border.color: Tk.Theme.color.borderStrong
                border.width: Tk.Theme.size.border
                clip: true
                Accessible.ignored: true

                Tk.SlideFrameView {
                    objectName: "slideCanvasRender"
                    anchors.fill: parent
                    visible: control.rendered
                    deck: control.deck
                    slideId: control.slideId
                    step: -1
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Slide %1 as the audience sees it").arg(control.slideId)
                }
                Repeater {
                    model: control.objects.filter(function(obj) { return obj.visible !== false })
                    delegate: Item {
                        id: slideObject
                        required property var modelData
                        readonly property var bounds: modelData.bounds || ({})
                        readonly property real startX: Number(bounds.x || 0) * control.pageScale
                        readonly property real startY: Number(bounds.y || 0) * control.pageScale
                        readonly property bool selected: control.selectedObjectIds.indexOf(modelData.id) >= 0
                        objectName: "slideObject_" + modelData.id
                        x: startX + (moveHandler.active ? moveHandler.translation.x : 0)
                        y: startY + (moveHandler.active ? moveHandler.translation.y : 0)
                        width: Math.max(Tk.Theme.size.control, Number(bounds.width || 120) * control.pageScale)
                        height: Math.max(Tk.Theme.size.control, Number(bounds.height || 48) * control.pageScale)
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("%1, %2").arg(modelData.kind || qsTr("object")).arg(modelData.text || modelData.id)
                        Accessible.description: selected ? qsTr("Selected slide object") : qsTr("Slide object")
                        Accessible.focusable: !modelData.locked
                        activeFocusOnTab: !modelData.locked
                        opacity: modelData.visible === false ? Tk.Theme.opacity.hidden : Tk.Theme.opacity.full

                        Rectangle {
                            objectName: "slideObjectChrome"
                            anchors.fill: parent
                            // Rendered: the painter draws the content; a drag shows a ghost.
                            color: control.rendered
                                   ? (moveHandler.active ? Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.ghost * Tk.Theme.opacity.disabled) : "transparent")
                                   : modelData.fill || (modelData.kind === "shape" ? Tk.Theme.color.controlBg : Tk.Theme.color.panelAlt)
                            border.color: slideObject.selected ? Tk.Theme.color.accent
                                        : control.rendered ? Tk.Theme.alpha(Tk.Theme.color.borderStrong, Tk.Theme.opacity.muted)
                                        : Tk.Theme.color.border
                            border.width: slideObject.selected ? Tk.Theme.size.focusRing : Tk.Theme.size.border
                            radius: modelData.kind === "shape" && !control.rendered ? Tk.Theme.radius.sm : 0
                        }
                        Tk.Label {
                            visible: !control.rendered
                            anchors.fill: parent
                            anchors.margins: Tk.Theme.space.xs
                            text: modelData.text || (modelData.kind === "shape" ? qsTr("Rectangle") : qsTr("Media object"))
                            color: Tk.Theme.color.text
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: if (control.enabled && !modelData.locked) control.selectionRequested(modelData.id)
                        }
                        DragHandler {
                            id: moveHandler
                            target: null
                            enabled: control.enabled && control.capabilities.canEdit && !modelData.locked && slideObject.selected
                            onActiveChanged: {
                                if (!active && (translation.x !== 0 || translation.y !== 0)) {
                                    const next = { x: Math.max(0, bounds.x + translation.x / control.pageScale),
                                                   y: Math.max(0, bounds.y + translation.y / control.pageScale),
                                                   width: bounds.width, height: bounds.height }
                                    control.objectMoveRequested(modelData.id, next)
                                }
                            }
                        }
                        Keys.onReturnPressed: if (!modelData.locked) control.selectionRequested(modelData.id)
                        Keys.onSpacePressed: if (!modelData.locked) control.selectionRequested(modelData.id)
                        Tk.ToolTip { text: slideObject.Accessible.name; visible: moveHandler.active }
                    }
                }
            }
            Rectangle {
                anchors.fill: parent
                color: Tk.Theme.alpha(Tk.Theme.color.focus, Tk.Theme.opacity.hidden)
                border.color: parent.activeFocus ? Tk.Theme.color.focus
                                                   : Tk.Theme.alpha(Tk.Theme.color.focus, Tk.Theme.opacity.hidden)
                border.width: Tk.Theme.size.focusRing
                Accessible.ignored: true
            }
        }
        Tk.Flex {
            objectName: "slideTextEditRow"
            visible: !!control.selectedTextObject && control.selectedTextObject.kind === "text"
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: textDraft
                objectName: "slideTextDraft"
                text: control.selectedText
                placeholderText: qsTr("Edit selected text")
                tooltip: qsTr("Text content for the selected slide object")
                enabled: control.capabilities.canEdit && !!control.selectedTextObject
                onAccepted: control.objectTextEditRequested(control.selectedObjectIds[0], text)
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "slideCommitText"
                text: qsTr("Apply text")
                tooltip: qsTr("Request text update for the selected object")
                enabled: control.capabilities.canEdit && !!control.selectedTextObject
                onClicked: control.objectTextEditRequested(control.selectedObjectIds[0], textDraft.text)
            }
        }
    }
}
