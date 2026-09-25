// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// AGENT-CONTRACT (D-179, docs/notation.md#layout): draws one ScoreView
// layout snapshot on a Canvas 2D context. Positions come only from the
// snapshot (Score::pageSnapshot in score_render.cpp mirrors Score::paintPage);
// this file decides glyph shapes, never where anything goes. Coordinates are
// normalized to the page: x * width, y * height; a staff step is half the
// staff's lineGap and step 0 is the bottom line.
// `style`: {surface, ink, muted, border, accent, fontFamily, captionSize, lineWidth}.

const glyphs = { "sharp": "♯", "flat": "♭", "natural": "♮",
                 "double-sharp": "x", "double-flat": "♭♭" }
const marks = { "staccato": ".", "staccatissimo": "'", "tenuto": "–", "accent": ">",
                "marcato": "^", "fermata": "⌢" }

function stepY(top, gap, step) {
    return top + (8 - Number(step)) * gap / 2
}

function text(ctx, style, value, x, y, size, bold, italic) {
    ctx.font = (italic ? "italic " : "") + (bold ? "bold " : "") + Math.max(1, Math.round(size)) + "px " + style.fontFamily
    ctx.textAlign = "center"
    ctx.textBaseline = "middle"
    ctx.fillText(String(value), x, y)
}

function paintRest(ctx, style, event, x, top, gap) {
    const y = stepY(top, gap, event.staffStep === undefined ? 4 : event.staffStep)
    const base = Number(event.base === undefined ? 2 : event.base)
    if (base <= 1) {
        ctx.fillRect(x - 0.6 * gap, base === 0 ? y - 0.5 * gap : y - 0.5 * gap, 1.2 * gap, 0.5 * gap)
    } else if (base === 2) {
        ctx.beginPath()
        ctx.moveTo(x - 0.3 * gap, y - 1.4 * gap)
        ctx.lineTo(x + 0.3 * gap, y - 0.6 * gap)
        ctx.lineTo(x - 0.3 * gap, y + 0.1 * gap)
        ctx.lineTo(x + 0.3 * gap, y + 0.9 * gap)
        ctx.stroke()
    } else {
        ctx.beginPath(); ctx.moveTo(x + 0.4 * gap, y - gap); ctx.lineTo(x - 0.2 * gap, y + gap); ctx.stroke()
        for (let f = 0; f < base - 2; ++f) {
            ctx.beginPath()
            ctx.arc(x - 0.1 * gap + f * 0.15 * gap, y - 0.8 * gap + f * 0.6 * gap, 0.25 * gap, 0, 2 * Math.PI)
            ctx.fill()
        }
    }
    for (let d = 0; d < Number(event.dotCount || 0); ++d) {
        ctx.beginPath(); ctx.arc(x + (1 + 0.5 * d) * gap, y - 0.25 * gap, 0.15 * gap, 0, 2 * Math.PI); ctx.fill()
    }
}

function paintNote(ctx, style, event, x, w, top, gap) {
    const heads = event.heads && event.heads.length ? event.heads
                : [{ staffStep: event.staffStep || 0, dx: 0, accidental: event.accidental || "" }]
    let low = Infinity, high = -Infinity
    for (let i = 0; i < heads.length; ++i) {
        const head = heads[i]
        const step = Number(head.staffStep)
        low = Math.min(low, step); high = Math.max(high, step)
        const hx = x + Number(head.dx || 0) * w
        const y = stepY(top, gap, step)
        for (let ledger = -2; ledger >= step - 0.01; ledger -= 2) {
            ctx.beginPath(); ctx.moveTo(hx - 0.9 * gap, stepY(top, gap, ledger)); ctx.lineTo(hx + 0.9 * gap, stepY(top, gap, ledger)); ctx.stroke()
        }
        for (let ledger = 10; ledger <= step + 0.01; ledger += 2) {
            ctx.beginPath(); ctx.moveTo(hx - 0.9 * gap, stepY(top, gap, ledger)); ctx.lineTo(hx + 0.9 * gap, stepY(top, gap, ledger)); ctx.stroke()
        }
        ctx.beginPath()
        ctx.ellipse(hx - 0.62 * gap, y - 0.42 * gap, 1.24 * gap, 0.84 * gap)
        if (event.filledHead !== false) ctx.fill(); else ctx.stroke()
        const accidental = String(head.accidental || "")
        if (accidental.length) text(ctx, style, glyphs[accidental] || accidental, hx - 1.3 * gap, y, 1.8 * gap)
        const dotY = Math.abs(step % 2) < 0.01 ? y - gap / 2 : y
        for (let d = 0; d < Number(event.dotCount || 0); ++d) {
            ctx.beginPath(); ctx.arc(x + (1 + 0.5 * d) * gap, dotY, 0.15 * gap, 0, 2 * Math.PI); ctx.fill()
        }
    }
    const stem = String(event.stemDirection || "")
    if (stem.length) {
        const up = stem !== "down"
        const sx = event.stemX !== undefined ? Number(event.stemX) * w : x + (up ? 0.55 : -0.55) * gap
        const end = event.stemEndStep !== undefined ? Number(event.stemEndStep) : (up ? high + 7 : low - 7)
        ctx.beginPath(); ctx.moveTo(sx, stepY(top, gap, up ? low : high)); ctx.lineTo(sx, stepY(top, gap, end)); ctx.stroke()
        for (let f = 0; f < Number(event.flags || 0); ++f) {
            const fy = stepY(top, gap, end) + (up ? 1 : -1) * f * 0.75 * gap
            ctx.beginPath(); ctx.moveTo(sx, fy); ctx.lineTo(sx + 0.8 * gap, fy + (up ? 1.2 : -1.2) * gap); ctx.stroke()
        }
    }
    const articulations = event.articulations || []
    for (let a = 0; a < articulations.length; ++a) {
        const step = Number(event.articulationStep || 0) + (stem === "down" ? 1.5 : -1.5) * a
        text(ctx, style, marks[articulations[a]] || articulations[a], x, stepY(top, gap, step), 1.6 * gap, true)
    }
}

function paintStaffDecor(ctx, style, staff, w, top, gap) {
    ctx.fillStyle = style.ink
    const beams = staff.beams || []
    for (let i = 0; i < beams.length; ++i) {
        const b = beams[i], thickness = (b.up ? 0.5 : -0.5) * gap
        const x0 = Number(b.x0) * w, x1 = Number(b.x1) * w
        const y0 = stepY(top, gap, b.step0), y1 = stepY(top, gap, b.step1)
        ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.lineTo(x1, y1 + thickness); ctx.lineTo(x0, y0 + thickness)
        ctx.closePath(); ctx.fill()
    }
    const ties = staff.ties || []
    for (let i = 0; i < ties.length; ++i) {
        const t = ties[i], x0 = Number(t.x0) * w, x1 = Number(t.x1) * w
        const y = stepY(top, gap, t.staffStep) + (t.above ? -0.6 : 0.6) * gap
        ctx.beginPath(); ctx.moveTo(x0, y); ctx.quadraticCurveTo((x0 + x1) / 2, y + (t.above ? -1 : 1) * gap, x1, y); ctx.stroke()
    }
    const tuplets = staff.tuplets || []
    for (let i = 0; i < tuplets.length; ++i) {
        const t = tuplets[i], x0 = Number(t.x0) * w, x1 = Number(t.x1) * w, mid = (x0 + x1) / 2
        const y = stepY(top, gap, t.staffStep)
        ctx.beginPath(); ctx.moveTo(x0, y); ctx.lineTo(mid - gap, y); ctx.moveTo(mid + gap, y); ctx.lineTo(x1, y); ctx.stroke()
        text(ctx, style, t.label, mid, y, 1.3 * gap, false, true)
    }
}

function paintMeasureHeader(ctx, style, staff, measure, w, top, gap) {
    ctx.fillStyle = style.muted
    const number = String(measure.number || "")
    if (number.length) text(ctx, style, number, Number(measure.x0) * w + gap, top - 1.5 * gap, style.captionSize)
    ctx.fillStyle = style.ink
    if (measure.clefX !== undefined) {
        const clef = String(staff.clef || "treble")
        const step = clef === "bass" ? 6 : clef === "alto" ? 4 : 2
        text(ctx, style, clef === "bass" ? "F" : clef === "alto" ? "C" : "G", Number(measure.clefX) * w + gap,
             stepY(top, gap, step), 3 * gap, true)
    }
    const key = measure.keyGlyphs || []
    for (let k = 0; k < key.length; ++k)
        text(ctx, style, glyphs[key[k].name] || "", Number(key[k].x) * w, stepY(top, gap, key[k].staffStep), 1.8 * gap)
    if (typeof measure.keySignature === "string" && measure.keySignature.length)
        text(ctx, style, measure.keySignature, Number(measure.x0) * w + 2 * gap, top + 2 * gap, style.captionSize)
    if (measure.time) {
        const tx = Number(measure.time.x) * w
        text(ctx, style, measure.time.numerator, tx, stepY(top, gap, 6), 2 * gap, true)
        text(ctx, style, measure.time.denominator, tx, stepY(top, gap, 2), 2 * gap, true)
    } else if (typeof measure.timeSignature === "string" && measure.timeSignature.length) {
        text(ctx, style, measure.timeSignature, Number(measure.x0) * w + 4 * gap, top + 2 * gap, style.captionSize)
    }
}

// Paints the snapshot; returns the number of events drawn.
function paint(ctx, w, h, snapshot, style, selectedEventId, cursorX, maxEvents) {
    ctx.reset()
    ctx.fillStyle = style.surface
    ctx.fillRect(0, 0, w, h)
    if (!snapshot || !snapshot.available) return 0
    ctx.lineWidth = style.lineWidth
    if (snapshot.title) {
        ctx.fillStyle = style.ink
        text(ctx, style, snapshot.title, w / 2, Number(snapshot.titleY || 0) * h, style.captionSize * 1.6, true)
    }
    let painted = 0
    const systems = snapshot.systems || []
    for (let s = 0; s < systems.length; ++s) {
        const staves = systems[s].staves || []
        for (let t = 0; t < staves.length; ++t) {
            const staff = staves[t]
            const x0 = Number(staff.x0) * w, x1 = Number(staff.x1) * w
            const top = Number(staff.top) * h, gap = Number(staff.lineGap) * h
            ctx.strokeStyle = style.muted
            for (let line = 0; line < 5; ++line) {
                ctx.beginPath(); ctx.moveTo(x0, top + line * gap); ctx.lineTo(x1, top + line * gap); ctx.stroke()
            }
            ctx.fillStyle = style.muted
            if (staff.label) text(ctx, style, staff.label, Math.max(style.captionSize, x0 - 2.2 * gap), top + 2 * gap, style.captionSize)
            const measures = staff.measures || []
            for (let m = 0; m < measures.length; ++m) {
                const measure = measures[m]
                ctx.strokeStyle = style.border
                const bx0 = Number(measure.x0) * w, bx1 = Number(measure.x1) * w
                if (m === 0) { ctx.beginPath(); ctx.moveTo(bx0, top); ctx.lineTo(bx0, top + 4 * gap); ctx.stroke() }
                ctx.beginPath(); ctx.moveTo(bx1, top); ctx.lineTo(bx1, top + 4 * gap); ctx.stroke()
                paintMeasureHeader(ctx, style, staff, measure, w, top, gap)
                ctx.strokeStyle = style.ink
                const voices = measure.voices || []
                for (let v = 0; v < voices.length && painted < maxEvents; ++v) {
                    const events = voices[v].events || []
                    for (let e = 0; e < events.length && painted < maxEvents; ++e, ++painted) {
                        const event = events[e]
                        const selected = String(event.id) === String(selectedEventId)
                        ctx.fillStyle = selected ? style.accent : style.ink
                        ctx.strokeStyle = selected ? style.accent : style.ink
                        const x = Number(event.x) * w
                        if (String(event.kind) === "rest") paintRest(ctx, style, event, x, top, gap)
                        else paintNote(ctx, style, event, x, w, top, gap)
                        if (event.dynamic) {
                            ctx.fillStyle = style.ink
                            text(ctx, style, event.dynamic, x, stepY(top, gap, -6), 1.6 * gap, true, true)
                        }
                    }
                }
            }
            ctx.strokeStyle = style.ink
            paintStaffDecor(ctx, style, staff, w, top, gap)
        }
    }
    if (cursorX >= 0) {
        ctx.strokeStyle = style.accent
        ctx.beginPath(); ctx.moveTo(cursorX * w, 0); ctx.lineTo(cursorX * w, h); ctx.stroke()
    }
    return painted
}
