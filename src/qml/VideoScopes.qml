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
    property string scopeMode: "waveform" // waveform, parade, histogram, vectorscope
    property bool showTargets: true // vectorscope 75%/100% graticule boxes from the snapshot
    property string tooltip: "CPU scopes of the encoded signal; the status line states matrix, range and space"
    // Canvas row of an 8-bit code; waveform and parade rows index codes (row 0 = 255).
    function codeY(code, height) { return (255 - code) / 256 * height }
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
                    // AGENT-NOTE: the vectorscope is a Cb/Cr plane, so it draws in a
                    // centred square inset by space.md; stretched to the canvas it
                    // skewed hue angles and clipped the 100% targets at the edges.
                    const vector = control.scopeMode === "vectorscope"
                    const inset = Tk.Theme.space.md
                    const sq = vector ? Math.max(1, Math.min(width, height) - 2 * inset) : 0
                    const gx = vector ? (width - sq) / 2 : 0, gy = vector ? (height - sq) / 2 : 0
                    const gw = vector ? sq : width, gh = vector ? sq : height
                    if (vector) ctx.strokeRect(gx, gy, gw, gh)
                    for (let i = 1; i < 4; ++i) {
                        ctx.beginPath(); ctx.moveTo(gx + gw * i / 4, gy); ctx.lineTo(gx + gw * i / 4, gy + gh); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(gx, gy + gh * i / 4); ctx.lineTo(gx + gw, gy + gh * i / 4); ctx.stroke()
                    }
                    const scale = control.snapshot.codeScale || ({ black: 0, white: 255 })
                    if (control.scopeMode === "waveform" || control.scopeMode === "parade") {
                        ctx.strokeStyle = Tk.Theme.color.warning
                        for (const code of [scale.black, scale.white]) {
                            const ly = control.codeY(code, height)
                            ctx.beginPath(); ctx.moveTo(0, ly); ctx.lineTo(width, ly); ctx.stroke()
                        }
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
                        const cellW = gw / side, cellH = gh / side
                        for (let i = 0; i < bins.length; ++i) {
                            if (!bins[i]) continue
                            ctx.globalAlpha = 0.25 + 0.75 * bins[i] / peak
                            ctx.fillRect(gx + (i % side) * cellW, gy + Math.floor(i / side) * cellH,
                                         Math.max(Tk.Theme.size.border, cellW), Math.max(Tk.Theme.size.border, cellH))
                        }
                        ctx.globalAlpha = 1
                        const targets = control.showTargets ? (control.snapshot.vectorTargets || []) : []
                        ctx.strokeStyle = Tk.Theme.color.textMuted
                        ctx.fillStyle = Tk.Theme.color.textMuted
                        ctx.font = Tk.Theme.font.caption + "px " + Tk.Theme.font.family
                        for (let t = 0; t < targets.length; ++t) {
                            const box = targets[t].level === 75 ? cellW * 2 : cellW * 3
                            const tx = gx + targets[t].x * gw, ty = gy + targets[t].y * gh
                            ctx.strokeRect(tx - box / 2, ty - box / 2, box, box)
                            if (targets[t].level !== 100) continue
                            // Labels sit outside the target, away from the centre.
                            const right = tx >= gx + gw / 2
                            ctx.textAlign = right ? "left" : "right"
                            ctx.textBaseline = "middle"
                            ctx.fillText(String(targets[t].label), tx + (right ? box : -box), ty)
                        }
                    } else if (control.scopeMode === "parade") {
                        const bins = control.snapshot.parade || []
                        const columns = control.snapshot.waveformColumns || 128
                        const rows = control.snapshot.waveformRows || 64
                        const roles = [Tk.Theme.color.danger, Tk.Theme.color.success, Tk.Theme.color.info]
                        let peak = 1
                        for (let i = 0; i < bins.length; ++i) peak = Math.max(peak, bins[i])
                        const panel = width / 3, cellW = panel / columns, cellH = height / rows
                        for (let i = 0; i < bins.length; ++i) {
                            if (!bins[i]) continue
                            const channel = Math.floor(i / (columns * rows))
                            const column = Math.floor(i / rows) % columns
                            ctx.fillStyle = roles[channel]
                            ctx.globalAlpha = 0.2 + 0.8 * bins[i] / peak
                            ctx.fillRect(channel * panel + column * cellW, (i % rows) * cellH,
                                         Math.max(Tk.Theme.size.border, cellW), Math.max(Tk.Theme.size.border, cellH))
                        }
                        ctx.globalAlpha = 1
                        ctx.strokeStyle = Tk.Theme.color.textMuted
                        for (let c = 1; c < 3; ++c) {
                            ctx.beginPath(); ctx.moveTo(panel * c, 0); ctx.lineTo(panel * c, height); ctx.stroke()
                        }
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
                      ? String(control.snapshot.matrix || "bt709") + (control.snapshot.matrixOrigin === "source" ? " (source)" : "")
                        + " • " + String(control.snapshot.signalRange || "full")
                        + (control.snapshot.rangeOrigin === "source" ? " (source)" : "") + " range • " + String(control.snapshot.analysisSpace || "srgb") + " (" + String(control.snapshot.transfer || "srgb")
                        + ") • " + String(control.snapshot.sampleCount) + " pixels"
                      : String(control.snapshot.error || "Waiting for a decoded frame")
            }
        }
    }
}
