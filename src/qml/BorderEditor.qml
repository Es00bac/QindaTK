// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// D-335: borders are document data. The host owns the map and applies each
// bordersEdited proposal as one undo step. Style changes affect the next
// clicked edge; toggling an existing edge removes it without changing others.
T.Control {
    id: control
    property var borders: ({}) // top/right/bottom/left/insideHorizontal/insideVertical: {style,color,width}
    property string lineStyle: "solid"
    property color lineColor: Tk.Theme.color.text
    property real lineWidth: 1 // document points
    property bool innerLines: true
    property string tooltip: qsTr("Borders and shading")
    readonly property var edgeNames: innerLines
        ? ["top", "right", "bottom", "left", "insideHorizontal", "insideVertical"]
        : ["top", "right", "bottom", "left"]
    signal bordersEdited(var borders)
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: contentItem.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    function toggleEdge(edge) {
        const next = Object.assign({}, borders)
        if (next[edge]) delete next[edge]
        else next[edge] = { style: lineStyle, color: String(lineColor), width: lineWidth }
        bordersEdited(next)
    }
    function edgeLabel(edge) {
        return ({ top: qsTr("Top border"), right: qsTr("Right border"), bottom: qsTr("Bottom border"),
                  left: qsTr("Left border"), insideHorizontal: qsTr("Inner horizontal border"),
                  insideVertical: qsTr("Inner vertical border") })[edge]
    }
    onBordersChanged: preview.requestPaint()
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.ComboBox {
                objectName: "borderStyle"
                model: ["solid", "dashed", "dotted", "double"]
                currentIndex: model.indexOf(control.lineStyle)
                tooltip: qsTr("Border style")
                onActivated: control.lineStyle = model[currentIndex]
            }
            Tk.NumberField {
                objectName: "borderWidth"
                value: control.lineWidth
                from: 0.1; to: 24; decimals: 1; stepSize: 0.5
                suffix: " pt"
                tooltip: qsTr("Border width in points")
                onValueModified: value => control.lineWidth = value
            }
            Tk.ColorField {
                objectName: "borderColor"
                color: control.lineColor
                tooltip: qsTr("Border colour")
                onColorEdited: color => control.lineColor = color
            }
        }
        Item {
            objectName: "borderPreview"
            width: parent.width
            implicitHeight: Tk.Theme.size.panelMinHeight
            Canvas {
                id: preview
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const margin = Tk.Theme.size.control
                    const x1 = margin, y1 = margin, x2 = width - margin, y2 = height - margin
                    const lines = { top: [x1,y1,x2,y1], bottom: [x1,y2,x2,y2], left: [x1,y1,x1,y2], right: [x2,y1,x2,y2],
                        insideHorizontal: [x1,height/2,x2,height/2], insideVertical: [width/2,y1,width/2,y2] }
                    for (const edge of control.edgeNames) {
                        const border = control.borders[edge]
                        const points = lines[edge]
                        ctx.strokeStyle = border ? border.color : Tk.Theme.color.divider
                        ctx.lineWidth = border ? Math.max(Tk.Theme.size.border, border.width) : Tk.Theme.size.border
                        ctx.setLineDash(border && border.style === "dashed" ? [Tk.Theme.space.sm,Tk.Theme.space.xs]
                            : !border || border.style === "dotted" ? [Tk.Theme.size.border,Tk.Theme.space.xs] : [])
                        ctx.beginPath(); ctx.moveTo(points[0],points[1]); ctx.lineTo(points[2],points[3]); ctx.stroke()
                        if (border && border.style === "double") {
                            const offset = Math.max(Tk.Theme.space.xs, border.width * 2)
                            const horizontal = points[1] === points[3]
                            ctx.beginPath(); ctx.moveTo(points[0] + (horizontal ? 0 : offset),points[1] + (horizontal ? offset : 0))
                            ctx.lineTo(points[2] + (horizontal ? 0 : offset),points[3] + (horizontal ? offset : 0)); ctx.stroke()
                        }
                    }
                }
            }
            Repeater {
                model: control.edgeNames
                delegate: T.AbstractButton {
                    id: edgeButton
                    required property string modelData
                    objectName: "borderEdge_" + modelData
                    readonly property bool horizontal: modelData === "top" || modelData === "bottom" || modelData === "insideHorizontal"
                    width: horizontal ? parent.width - Tk.Theme.size.control * 2 : Tk.Theme.size.control
                    height: horizontal ? Tk.Theme.size.control : parent.height - Tk.Theme.size.control * 2
                    x: horizontal ? Tk.Theme.size.control : modelData === "left" ? Tk.Theme.size.control / 2
                        : modelData === "right" ? parent.width - Tk.Theme.size.control * 1.5 : (parent.width - width) / 2
                    y: !horizontal ? Tk.Theme.size.control : modelData === "top" ? Tk.Theme.size.control / 2
                        : modelData === "bottom" ? parent.height - Tk.Theme.size.control * 1.5 : (parent.height - height) / 2
                    hoverEnabled: true
                    checkable: true
                    checked: !!control.borders[modelData]
                    focusPolicy: Qt.StrongFocus
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: control.edgeLabel(modelData)
                    Accessible.checked: checked
                    Accessible.onPressAction: control.toggleEdge(modelData)
                    onClicked: control.toggleEdge(modelData)
                    background: Rectangle {
                        color: "transparent"
                        border.color: Tk.Theme.color.focus
                        border.width: edgeButton.visualFocus ? Tk.Theme.size.focusRing : 0
                    }
                    Tk.ToolTip { text: control.edgeLabel(edgeButton.modelData); visible: edgeButton.hovered }
                }
            }
        }
    }
}
