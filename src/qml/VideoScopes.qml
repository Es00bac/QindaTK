// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    // AGENT-CONTRACT: VideoScopesModel analyzes host-decoded QImage frames;
    // this control consumes only its immutable bounded snapshot and never owns
    // frame decoding, color interpretation, or analysis authority.
    id: control
    objectName: "videoScopes"
    Accessible.role: Accessible.Graphic
    Accessible.name: "Video scopes"
    Accessible.description: statusLabel.text
    property var snapshot: ({})
    property string scopeMode: "waveform" // waveform, histogram, vectorscope
    property string tooltip: "CPU scopes: Rec.709 encoded-sRGB luma, full-range chroma"
    implicitWidth: Tk.Theme.size.controlLg * 12
    implicitHeight: Tk.Theme.size.waveformHeight * 8
    padding: Tk.Theme.space.md
    HoverHandler { id: hover }
    Tk.ToolTip { visible: hover.hovered && control.tooltip.length > 0; text: control.tooltip }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }

    contentItem: Item {
        id: content
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.Flex {
                id: headerRow
                width: parent.width
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Text {
                    text: control.scopeMode.toUpperCase()
                    color: Tk.Theme.color.text
                    font.pixelSize: Tk.Theme.font.small
                    objectName: "videoScopesMode"
                }
                Text {
                    text: control.snapshot.valid ? String(control.snapshot.frameId || "") : "NO FRAME"
                    color: Tk.Theme.color.textMuted
                    font.family: Tk.Theme.font.monoFamily
                    font.pixelSize: Tk.Theme.font.body
                    objectName: "videoScopesFrameId"
                }
            }
            Canvas {
                id: plot
                objectName: "videoScopesCanvas"
                width: parent.width
                implicitHeight: Tk.Theme.size.waveformHeight
                Tk.Flex.grow: 1
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = Tk.Theme.color.canvas
                    ctx.fillRect(0, 0, width, height)
                    if (!control.snapshot.valid) return
                    const accent = Tk.Theme.color.accent
                    const muted = Tk.Theme.color.border
                    ctx.strokeStyle = muted
                    ctx.lineWidth = Tk.Theme.size.border
                    for (let i = 1; i < 4; ++i) {
                        ctx.beginPath(); ctx.moveTo(width * i / 4, 0); ctx.lineTo(width * i / 4, height); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(0, height * i / 4); ctx.lineTo(width, height * i / 4); ctx.stroke()
                    }
                    ctx.fillStyle = accent
                    if (control.scopeMode === "histogram") {
                        const bins = control.snapshot.histogram || []
                        let peak = 1
                        for (let i = 0; i < bins.length; ++i) peak = Math.max(peak, bins[i])
                        const bar = width / Math.max(1, bins.length)
                        for (let i = 0; i < bins.length; ++i)
                            ctx.fillRect(i * bar, height, Math.max(Tk.Theme.size.border, bar), -height * bins[i] / peak)
                    } else if (control.scopeMode === "vectorscope") {
                        const bins = control.snapshot.vectorscope || []
                        let peak = 1
                        for (let i = 0; i < bins.length; ++i) peak = Math.max(peak, bins[i])
                        const side = control.snapshot.vectorscopeSide || 64
                        const cellW = width / side, cellH = height / side
                        for (let i = 0; i < bins.length; ++i) {
                            if (!bins[i]) continue
                            ctx.globalAlpha = 0.25 + 0.75 * bins[i] / peak
                            ctx.fillRect((i % side) * cellW, Math.floor(i / side) * cellH,
                                         Math.max(Tk.Theme.size.border, cellW), Math.max(Tk.Theme.size.border, cellH))
                        }
                        ctx.globalAlpha = 1
                    } else {
                        const bins = control.snapshot.waveform || []
                        const columns = control.snapshot.waveformColumns || 128
                        const rows = control.snapshot.waveformRows || 64
                        let peak = 1
                        for (let i = 0; i < bins.length; ++i) peak = Math.max(peak, bins[i])
                        const cellW = width / columns, cellH = height / rows
                        for (let i = 0; i < bins.length; ++i) {
                            if (!bins[i]) continue
                            ctx.globalAlpha = 0.2 + 0.8 * bins[i] / peak
                            ctx.fillRect(Math.floor(i / rows) * cellW,
                                         (i % rows) * cellH, Math.max(Tk.Theme.size.border, cellW), Math.max(Tk.Theme.size.border, cellH))
                        }
                        ctx.globalAlpha = 1
                    }
                }
                Connections {
                    target: control
                    function onSnapshotChanged() { plot.requestPaint() }
                    function onScopeModeChanged() { plot.requestPaint() }
                }
                Component.onCompleted: requestPaint()
            }
            Text {
                id: statusLabel
                objectName: "videoScopesStatus"
                width: parent.width
                color: control.snapshot.valid ? Tk.Theme.color.textMuted : Tk.Theme.color.danger
                font.pixelSize: Tk.Theme.font.caption
                elide: Text.ElideRight
                text: control.snapshot.valid
                      ? "Full-range sRGB • " + String(control.snapshot.sampleCount) + " pixels • " + String(control.snapshot.lumaConvention || "")
                      : String(control.snapshot.error || "Waiting for a decoded frame")
            }
        }
    }
}
