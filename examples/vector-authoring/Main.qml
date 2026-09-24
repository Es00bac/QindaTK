// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Small host-owned vector document used to exercise the authoring request
// contracts. It intentionally keeps geometry and history in the host object.
Rectangle {
    id: scene
    width: 1180
    height: 820
    color: Tk.Theme.color.bg

    QtObject {
        id: document
        property string selectedObjectId: "shape-1"
        property string selectedShapeId: "rounded-rectangle"
        property string lastRequest: qsTr("Ready · one vector object selected")
        property int revision: 0
        property var objects: [{ id: "shape-1", type: "rounded-rectangle", parameters: { radius: 18 } }]
        property var strokeStyle: ({ width: 5, unit: "px", alignment: "center", cap: "round", join: "round", miterLimit: 4, dash: [], dashMode: "solid", color: Tk.Theme.color.accent })
        property var fillStyle: ({ kind: "solid", settings: { solid: { color: "#235368" }, gradient: { gradient: { type: "linear", stops: [{ color: "#235368" }, { color: "#17313e" }] } }, pattern: { patternId: "grid" } } })
        property var appearance: [
            { id: "opacity", name: "Opacity", kind: "adjustment", enabled: true, supported: true, parameters: { amount: 1 } },
            { id: "shadow", name: "Drop shadow", kind: "effect", enabled: false, supported: true, parameters: { blur: 12, offsetY: 5 } },
            { id: "legacy-glow", name: "Legacy outer glow", kind: "effect", enabled: true, supported: false, unsupportedReason: qsTr("Unavailable in this renderer; saved values are preserved.") }
        ]

        function setStroke(next) {
            strokeStyle = next
            ++revision
            lastRequest = qsTr("Stroke updated · revision %1").arg(revision)
        }
        function setFill(next) {
            fillStyle = next
            ++revision
            lastRequest = qsTr("Fill updated · revision %1").arg(revision)
        }
        function insertShape(shapeId, parameters) {
            const nextId = "shape-" + (objects.length + 1)
            objects = objects.concat([{ id: nextId, type: shapeId, parameters: parameters }])
            selectedObjectId = nextId
            selectedShapeId = shapeId
            ++revision
            lastRequest = qsTr("Inserted %1 · %2 document objects").arg(shapeId).arg(objects.length)
        }
        function effectEnabled(effectId, nextEnabled) {
            appearance = appearance.map(function(effect) {
                return effect.id === effectId ? Object.assign({}, effect, { enabled: nextEnabled }) : effect
            })
            ++revision
            lastRequest = qsTr("%1 %2").arg(nextEnabled ? qsTr("Enabled") : qsTr("Bypassed")).arg(effectId)
        }
        function reorderEffect(effectId, newIndex) {
            const next = appearance.slice()
            let oldIndex = -1
            for (let i = 0; i < next.length; ++i) if (next[i].id === effectId) oldIndex = i
            if (oldIndex < 0 || newIndex < 0 || newIndex >= next.length) return
            const moved = next.splice(oldIndex, 1)[0]
            next.splice(newIndex, 0, moved)
            appearance = next
            ++revision
            lastRequest = qsTr("Moved %1 to position %2").arg(effectId).arg(newIndex + 1)
        }
        function editEffect(effectId) {
            lastRequest = qsTr("Edit requested for %1").arg(effectId)
        }
        function renderedFill() {
            if (fillStyle.kind === "none") return "transparent"
            if (fillStyle.kind === "solid") return fillStyle.settings.solid.color
            if (fillStyle.kind === "gradient") {
                const stops = fillStyle.settings.gradient.gradient.stops
                return stops.length ? stops[0].color : Tk.Theme.color.accentSubtle
            }
            return Tk.Theme.color.panelAlt
        }
        function strokePixels() {
            const width = strokeStyle.width || 1
            return strokeStyle.unit === "pt" ? width * 96 / 72 : width
        }
    }

    QtObject {
        id: library
        readonly property var shapes: [
            { id: "rectangle", label: qsTr("Rectangle"), category: qsTr("Basic"), iconName: "square", parameters: { radius: 0 }, description: qsTr("Four-corner path") },
            { id: "rounded-rectangle", label: qsTr("Rounded"), category: qsTr("Basic"), iconName: "square-round-corner", parameters: { radius: 18 }, description: qsTr("Rounded rectangle path") },
            { id: "ellipse", label: qsTr("Ellipse"), category: qsTr("Basic"), iconName: "circle", parameters: { startAngle: 0, sweepAngle: 360 }, description: qsTr("Closed elliptical path") },
            { id: "line", label: qsTr("Line"), category: qsTr("Lines"), iconName: "minus", parameters: { closed: false }, description: qsTr("Open two-node path") },
            { id: "star", label: qsTr("Star"), category: qsTr("Symbols"), iconName: "star", parameters: { points: 5, innerRadius: 0.45 }, description: qsTr("Five-point parametric star") },
            { id: "polygon", label: qsTr("Polygon"), category: qsTr("Symbols"), iconName: "hexagon", parameters: { sides: 6 }, description: qsTr("Six-sided parametric polygon") }
        ]
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.md
            Tk.Overline { title: qsTr("VECTOR APPEARANCE WORKBENCH"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Badge { text: qsTr("%1 objects").arg(document.objects.length); variant: "accent" }
            Tk.Badge { text: qsTr("Revision %1").arg(document.revision); variant: "default" }
        }

        Tk.Flex {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.lg

            Tk.Box {
                objectName: "vectorDocumentHost"
                color: Tk.Theme.color.panel
                borderColor: Tk.Theme.color.divider
                padding: Tk.Theme.space.md
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.shrink: 1

                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        Tk.SectionHeader { title: qsTr("Untitled vector document"); count: document.objects.length; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                        Tk.Caption { text: qsTr("100% · RGB") }
                    }
                    Item {
                        objectName: "vectorArtboardHost"
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Rectangle {
                            id: artboard
                            anchors.centerIn: parent
                            width: Math.min(parent.width - Tk.Theme.space.xl * 2, 560)
                            height: Math.min(parent.height - Tk.Theme.space.xl * 2, 420)
                            color: Tk.Theme.color.surface
                            border.width: Tk.Theme.size.border
                            border.color: Tk.Theme.color.divider
                            Rectangle {
                                objectName: "vectorPreviewShape"
                                anchors.centerIn: parent
                                width: Math.min(parent.width * 0.58, 300)
                                height: Math.min(parent.height * 0.44, 180)
                                radius: document.selectedShapeId === "ellipse" ? width / 2
                                      : document.selectedShapeId === "rounded-rectangle" ? Tk.Theme.radius.md : 0
                                color: document.renderedFill()
                                border.color: document.strokeStyle.color
                                border.width: document.strokePixels()
                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: "transparent"
                                    border.color: Tk.Theme.color.focus
                                    border.width: Tk.Theme.size.border
                                    anchors.margins: -Tk.Theme.space.sm
                                }
                            }
                            Tk.Caption {
                                anchors { right: parent.right; bottom: parent.bottom }
                                anchors.margins: Tk.Theme.space.md
                                text: qsTr("Selected: %1 · %2").arg(document.selectedShapeId).arg(document.selectedObjectId)
                                mono: true
                            }
                        }
                    }
                    Tk.Notice {
                        objectName: "vectorHostStatus"
                        text: document.lastRequest
                        variant: "info"
                    }
                    Tk.Caption {
                        text: qsTr("Each panel emits an edit request; this document host applies it and advances one revision.")
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Tk.Scroll {
                objectName: "vectorInspectorScroll"
                width: 390
                Tk.Flex.shrink: 0
                Tk.Flex.basis: 390
                Tk.Flex.minWidth: 320
                overflowY: Tk.Scroll.Auto
                accessibleName: qsTr("Vector appearance inspector")

                Tk.Flex {
                    width: 390
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    padding: Tk.Theme.space.xs

                    Tk.ShapePalette {
                        objectName: "vectorShapePalette"
                        width: parent.width
                        definitions: library.shapes
                        columns: 2
                        onShapeRequested: function(shapeId, parameters) { document.insertShape(shapeId, parameters) }
                    }
                    Tk.Divider {}
                    Tk.StrokeStyleEditor {
                        objectName: "vectorStrokeEditor"
                        width: parent.width
                        style: document.strokeStyle
                        capabilities: ({ units: ["px", "pt", "document"], alignments: ["center", "inside", "outside"], caps: ["butt", "round", "square"], joins: ["miter", "round", "bevel"] })
                        onStyleChangeRequested: function(style) { document.setStroke(style) }
                    }
                    Tk.Divider {}
                    Tk.FillStyleEditor {
                        objectName: "vectorFillEditor"
                        width: parent.width
                        fill: document.fillStyle
                        patterns: [{ id: "grid", label: qsTr("Grid") }, { id: "dots", label: qsTr("Dots") }, { id: "crosshatch", label: qsTr("Crosshatch") }]
                        onFillChangeRequested: function(fill) { document.setFill(fill) }
                        onGradientEditRequested: function(gradient) {
                            document.lastRequest = gradient ? qsTr("Gradient edit requested · %1 stops").arg(gradient.stops.length) : qsTr("Create gradient requested")
                        }
                    }
                    Tk.Divider {}
                    Tk.AppearanceStack {
                        objectName: "vectorAppearanceStack"
                        width: parent.width
                        effects: document.appearance
                        onReorderRequested: function(id, index) { document.reorderEffect(id, index) }
                        onEnabledChangeRequested: function(id, enabled) { document.effectEnabled(id, enabled) }
                        onEditRequested: function(id) { document.editEffect(id) }
                    }
                }
            }
        }
    }
}
