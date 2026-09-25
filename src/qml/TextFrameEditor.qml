// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Geometry, story order and frame threading are owned by the typesetter. This
// inspector reports stable-ID edit requests and displays model overset state.
Item {
    id: control
    property var frames: [] // [{id,label,pageId,xMm,yMm,widthMm,heightMm,storyId,nextFrameId,columns,gutterMm,insetMm,oversetChars,flowStatus}]
    property string selectedFrameId: ""
    property string pageId: ""
    property string displayUnit: "mm"
    property var newFrameGeometryMm: null
    property string tooltip: qsTr("Edit text frame geometry and story flow")
    readonly property var currentFrame: frameById(selectedFrameId)
    // A frame's own `connectTargets` (RichTextDocument.frames) lists the
    // frames a thread may continue into; otherwise frames of the same story.
    readonly property var connectionTargets: frames.filter(function(frame) {
        if (!currentFrame || frame.id === currentFrame.id) return false
        if (currentFrame.connectTargets !== undefined) return currentFrame.connectTargets.indexOf(frame.id) >= 0
        return frame.storyId === currentFrame.storyId
    })
    property string targetFrameId: ""

    signal frameSelectionRequested(string frameId)
    signal geometryChangeRequested(string frameId, var geometryMm)
    signal layoutChangeRequested(string frameId, var layout)
    signal connectRequested(string sourceFrameId, string targetFrameId)
    signal disconnectRequested(string frameId)
    signal createFrameRequested(string pageId, var geometryMm)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function frameById(id) {
        for (let i = 0; i < frames.length; ++i) if (frames[i].id === id) return frames[i]
        return null
    }
    function display(mm) {
        if (displayUnit === "in") return mm / 25.4
        if (displayUnit === "pt") return mm * 72 / 25.4
        return mm
    }
    function canonical(value) {
        if (displayUnit === "in") return value * 25.4
        if (displayUnit === "pt") return value * 25.4 / 72
        return value
    }
    function geometryWith(patch) {
        if (!currentFrame) return
        const geometry = {
            xMm: currentFrame.xMm,
            yMm: currentFrame.yMm,
            widthMm: currentFrame.widthMm,
            heightMm: currentFrame.heightMm
        }
        Object.assign(geometry, patch)
        geometryChangeRequested(currentFrame.id, geometry)
    }
    function setGeometry(key, value) {
        if (!enabled || !currentFrame) return
        const patch = {}
        patch[key] = canonical(value)
        geometryWith(patch)
    }
    function setLayout(key, value) {
        if (!enabled || !currentFrame) return
        layoutChangeRequested(currentFrame.id, {
            columns: currentFrame.columns || 1,
            gutterMm: currentFrame.gutterMm || 0,
            insetMm: currentFrame.insetMm || 0,
            [key]: key === "gutterMm" || key === "insetMm" ? canonical(value) : value
        })
    }
    function createFrame() {
        if (enabled && pageId.length > 0 && newFrameGeometryMm)
            createFrameRequested(pageId, Object.assign({}, newFrameGeometryMm))
    }

    Tk.Flex {
        id: column
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("TEXT FRAME"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Button {
                objectName: "textFrameCreate"
                text: qsTr("Add frame")
                iconName: "plus"
                small: true
                tooltip: qsTr("Create a text frame on the current page")
                enabled: control.pageId.length > 0 && control.newFrameGeometryMm !== null
                onClicked: control.createFrame()
            }
        }
        Tk.ComboBox {
            objectName: "textFrameSelect"
            model: control.frames
            textRole: "label"
            currentIndex: {
                for (let i = 0; i < model.length; ++i) if (model[i].id === control.selectedFrameId) return i
                return -1
            }
            placeholderText: control.frames.length > 0 ? qsTr("Select a text frame") : qsTr("No frames on this page")
            tooltip: qsTr("Select a text frame")
            onActivated: function(index) { control.frameSelectionRequested(model[index].id) }
        }
        Tk.Caption {
            objectName: "textFrameStoryStatus"
            visible: control.currentFrame !== null
            text: control.currentFrame ? qsTr("Story %1 · %2").arg(control.currentFrame.storyId).arg(control.currentFrame.flowStatus || qsTr("flowing")) : ""
            wrapMode: Text.WordWrap
        }
        Tk.Grid {
            visible: control.currentFrame !== null
            columns: "1fr 1fr"
            gap: Tk.Theme.space.xs
            columnGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("X")
                Tk.NumberField {
                    objectName: "textFrameX"
                    readonly property real shown: control.currentFrame ? control.display(control.currentFrame.xMm) : 0
                    value: shown
                    from: -100000
                    to: 100000
                    stepSize: 1
                    decimals: 1
                    suffix: control.displayUnit
                    tooltip: qsTr("Frame left position")
                    onValueModified: function(value) { control.setGeometry("xMm", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Y")
                Tk.NumberField {
                    objectName: "textFrameY"
                    readonly property real shown: control.currentFrame ? control.display(control.currentFrame.yMm) : 0
                    value: shown
                    from: -100000
                    to: 100000
                    stepSize: 1
                    decimals: 1
                    suffix: control.displayUnit
                    tooltip: qsTr("Frame top position")
                    onValueModified: function(value) { control.setGeometry("yMm", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Width")
                Tk.NumberField {
                    objectName: "textFrameWidth"
                    readonly property real shown: control.currentFrame ? control.display(control.currentFrame.widthMm) : 1
                    value: shown
                    from: 1
                    to: 100000
                    stepSize: 1
                    decimals: 1
                    suffix: control.displayUnit
                    tooltip: qsTr("Frame width")
                    onValueModified: function(value) { control.setGeometry("widthMm", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Height")
                Tk.NumberField {
                    objectName: "textFrameHeight"
                    readonly property real shown: control.currentFrame ? control.display(control.currentFrame.heightMm) : 1
                    value: shown
                    from: 1
                    to: 100000
                    stepSize: 1
                    decimals: 1
                    suffix: control.displayUnit
                    tooltip: qsTr("Frame height")
                    onValueModified: function(value) { control.setGeometry("heightMm", value) }
                }
            }
        }
        Tk.Grid {
            visible: control.currentFrame !== null
            columns: "1fr 1fr"
            gap: Tk.Theme.space.xs
            columnGap: Tk.Theme.space.sm
            Tk.PropertyRow {
                label: qsTr("Columns")
                Tk.NumberField {
                    objectName: "textFrameColumns"
                    readonly property real shown: control.currentFrame ? control.currentFrame.columns || 1 : 1
                    value: shown
                    from: 1
                    to: 24
                    stepSize: 1
                    decimals: 0
                    tooltip: qsTr("Number of text columns in the frame")
                    onValueModified: function(value) { control.setLayout("columns", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Gap")
                Tk.NumberField {
                    objectName: "textFrameGutter"
                    readonly property real shown: control.currentFrame ? control.display(control.currentFrame.gutterMm || 0) : 0
                    value: shown
                    from: 0
                    to: 100000
                    stepSize: 1
                    decimals: 1
                    suffix: control.displayUnit
                    tooltip: qsTr("Text column gap")
                    onValueModified: function(value) { control.setLayout("gutterMm", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Inset")
                Tk.NumberField {
                    objectName: "textFrameInset"
                    readonly property real shown: control.currentFrame ? control.display(control.currentFrame.insetMm || 0) : 0
                    value: shown
                    from: 0
                    to: 100000
                    stepSize: 1
                    decimals: 1
                    suffix: control.displayUnit
                    tooltip: qsTr("Frame text inset")
                    onValueModified: function(value) { control.setLayout("insetMm", value) }
                }
            }
        }
        Tk.Flex {
            visible: control.currentFrame !== null
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.ComboBox {
                id: targetSelector
                objectName: "textFrameConnectTarget"
                model: control.connectionTargets
                textRole: "label"
                currentIndex: {
                    for (let i = 0; i < model.length; ++i)
                        if (model[i].id === (control.targetFrameId || (control.currentFrame ? control.currentFrame.nextFrameId : ""))) return i
                    return -1
                }
                placeholderText: qsTr("Next frame…")
                tooltip: qsTr("Next frame in this story")
                onActivated: function(index) { control.targetFrameId = model[index].id }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "textFrameConnect"
                text: qsTr("Connect")
                enabled: control.currentFrame !== null && targetSelector.currentIndex >= 0
                tooltip: qsTr("Connect this frame to the selected next frame")
                onClicked: control.connectRequested(control.currentFrame.id, targetSelector.currentText.length ? control.connectionTargets[targetSelector.currentIndex].id : "")
            }
            Tk.IconButton {
                objectName: "textFrameDisconnect"
                iconName: "unlink"
                tooltip: qsTr("Disconnect this frame from its next frame")
                enabled: control.currentFrame !== null && (control.currentFrame.nextFrameId || "").length > 0
                onClicked: control.disconnectRequested(control.currentFrame.id)
            }
        }
        Tk.Notice {
            objectName: "textFrameOverset"
            visible: control.currentFrame !== null && Number(control.currentFrame.oversetChars || 0) > 0
            variant: "warning"
            title: qsTr("Overset text")
            text: control.currentFrame ? qsTr("%1 characters do not fit in this frame.").arg(control.currentFrame.oversetChars) : ""
        }
    }
}
