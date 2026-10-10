// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// Geometry and property helpers for FormDesigner's positioned controls
// (D-311, docs/forms-reports.md#form-controls). Pure functions: the control
// list itself is design data (LocalDataDesignDocument or the host's design).

const TYPES = ["label", "textBox", "comboBox", "listBox", "checkBox", "subform",
               "commandButton", "tabControl", "image", "line", "rectangle"]
const BOUND = ["textBox", "comboBox", "listBox", "checkBox"]
const ICONS = {
    label: "type", textBox: "text-cursor-input", comboBox: "chevrons-up-down", listBox: "list",
    checkBox: "square-check", subform: "layout-template", commandButton: "mouse-pointer",
    tabControl: "panels-top-left", image: "image", line: "minus", rectangle: "square"
}
// Geometry the property sheet and keyboard edit; the rest are properties.
const GEOMETRY = ["x", "y", "width", "height"]
const MAX_COORDINATE = 20000
// The eight resize handles, as [name, x factor, y factor].
const HANDLES = [["nw", 0, 0], ["n", 0.5, 0], ["ne", 1, 0], ["e", 1, 0.5],
                 ["se", 1, 1], ["s", 0.5, 1], ["sw", 0, 1], ["w", 0, 0.5]]

// AGENT-NOTE: QML sees a library's functions, not its top-level const/let
// bindings, so the tables above are read through these.
function types() { return TYPES.slice() }
function handles() { return HANDLES }
function iconFor(type) { return ICONS[type] || "square" }
function isBound(type) { return BOUND.indexOf(String(type)) >= 0 }
function hasCaption(type) { return type === "label" || type === "checkBox" || type === "commandButton" }
function hasItems(type) { return type === "comboBox" || type === "listBox" }

function snap(value, grid) {
    const v = Number(value) || 0
    return grid > 0 ? Math.round(v / grid) * grid : Math.round(v)
}

function clamp(value, low, high) { return Math.max(low, Math.min(high, value)) }

function rectOf(control) {
    return { x: Number(control.x) || 0, y: Number(control.y) || 0,
             width: Number(control.width) || 0, height: Number(control.height) || 0 }
}

// A moved rectangle: snapped, never left of or above the form's origin.
function moved(start, dx, dy, grid) {
    return { x: clamp(snap(start.x + dx, grid), 0, MAX_COORDINATE - start.width),
             y: clamp(snap(start.y + dy, grid), 0, MAX_COORDINATE - start.height),
             width: start.width, height: start.height }
}

// A rectangle resized by dragging `handle` by (dx, dy): the opposite edges
// stay put, edges snap to the grid, and a side never shrinks below
// `minimum` (a line may be 0 thick along one axis, `minimum` 0 there).
function resized(start, handle, dx, dy, grid, minimumWidth, minimumHeight) {
    let left = start.x, top = start.y, right = start.x + start.width, bottom = start.y + start.height
    if (handle.indexOf("w") >= 0) left = clamp(snap(left + dx, grid), 0, right - minimumWidth)
    if (handle.indexOf("e") >= 0) right = clamp(snap(right + dx, grid), left + minimumWidth, MAX_COORDINATE)
    if (handle.indexOf("n") >= 0) top = clamp(snap(top + dy, grid), 0, bottom - minimumHeight)
    if (handle.indexOf("s") >= 0) bottom = clamp(snap(bottom + dy, grid), top + minimumHeight, MAX_COORDINATE)
    return { x: left, y: top, width: right - left, height: bottom - top }
}

// A copy of `control` with `changes`; an undefined or null value removes the property.
function merged(control, changes) {
    const out = Object.assign({}, control)
    for (const key in changes) {
        if (changes[key] === undefined || changes[key] === null) delete out[key]
        else out[key] = changes[key]
    }
    return out
}

// "Text3": the type's name and the first number no control's name uses
// (the same rule as LocalDataDesignDocument.addFormControl).
function uniqueName(controls, type) {
    let stem = String(type).charAt(0).toUpperCase() + String(type).slice(1)
    if (stem.endsWith("Box") && type !== "checkBox") stem = stem.slice(0, -3)
    const taken = {}
    for (let i = 0; i < controls.length; ++i) taken[String(controls[i].name || "")] = true
    for (let n = 1; ; ++n) if (!taken[stem + n]) return stem + n
}

// "North; South" <-> ["North", "South"] for item and page lists.
function listText(values) { return (values || []).map(function(v) { return String(v) }).join("; ") }
function parseList(text) {
    return String(text || "").split(";").map(function(v) { return v.trim() }).filter(function(v) { return v.length > 0 })
}

// The next free spot for a new control: under the lowest control.
function nextPosition(controls, grid) {
    let bottom = 0
    for (let i = 0; i < controls.length; ++i)
        if (!controls[i].parentId) bottom = Math.max(bottom, (Number(controls[i].y) || 0) + (Number(controls[i].height) || 0))
    const gap = grid > 0 ? grid : 8
    return { x: gap, y: bottom > 0 ? snap(bottom + gap, grid) : gap }
}

// Images the designer draws itself: data:, file:, qrc: and image provider
// URLs, never a network fetch (D-319 keeps imported HTML to the same rule).
function drawableImage(source) {
    return /^(data:image\/(?!svg)|file:|qrc:|image:)/i.test(String(source || ""))
}

// Whether a control is shown: a control on a tab page only on its page.
function onShownPage(control, pages) {
    if (!control.parentId) return true
    return (pages[control.parentId] || 0) === (Number(control.page) || 0)
}
