// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// AGENT-NOTE (WP-061): scale-label thinning shared by Fader and AudioMeter.
// Ticks stay for every mark; a label is drawn only when its extent along the
// scale clears every label already kept, and every `reserved` [start, end]
// pixel span, by `gap`. Values in `priority` are placed first (unity, then the
// range ends), the other marks follow in their order. `centreOf(value)` and
// `extentOf(value)` give the label's centre and length along the scale axis in
// the same pixels. Returns {"<value>": true} for each kept label.
function thin(marks, centreOf, extentOf, priority, gap, reserved) {
    const values = marks || []
    const spans = (reserved || []).map(function(span) { return [span[0], span[1]] })
    const order = []
    const seen = {}
    const add = function(value) {
        const key = String(value)
        if (seen[key] || values.indexOf(value) < 0) return
        seen[key] = true
        order.push(value)
    }
    for (let i = 0; i < (priority || []).length; ++i) add(priority[i])
    for (let i = 0; i < values.length; ++i) add(values[i])
    const shown = {}
    for (let i = 0; i < order.length; ++i) {
        const centre = centreOf(order[i])
        const half = extentOf(order[i]) / 2
        if (!isFinite(centre) || !isFinite(half)) continue
        let clear = true
        for (let s = 0; s < spans.length && clear; ++s)
            clear = centre + half + gap <= spans[s][0] || centre - half - gap >= spans[s][1]
        if (!clear) continue
        spans.push([centre - half, centre + half])
        shown[String(order[i])] = true
    }
    return shown
}
