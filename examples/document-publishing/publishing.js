// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// A press release as qindatk.richtext version 2 (docs/rich-text.md): named
// paragraph, character and table styles; a price table with a merged cell;
// a story threaded through frames A -> B on page 1, and a free frame C on
// page 2 that the story can be connected to. Demo data only.

var next = 1

function uuid() {
    var hex = (next++).toString(16)
    while (hex.length < 12) hex = "0" + hex
    return "00000000-0000-4000-8000-" + hex
}

// runs: [[text, formatIndex], ...]; format: paragraph format members.
function paragraph(runs, format) {
    var text = ""
    var spans = []
    for (var i = 0; i < runs.length; ++i) {
        text += runs[i][0]
        if (runs[i][0].length) spans.push([runs[i][0].length, runs[i][1]])
    }
    return { id: uuid(), text: text, spans: spans, mark: 0, format: format || {} }
}

function body(text) { return { paragraph: paragraph([[text, 0]], { style: "body" }) } }

function cell(text) { return { id: uuid(), paragraphs: [paragraph([[text, 0]], { style: "body" })] } }

var sentences = [
    "The city library opens its new reading hall on the first Monday of spring.",
    "Architects kept the original brick vaults and added daylight from a glass roof.",
    "Readers will find quiet desks, a children's corner and a studio for local writers.",
    "Opening hours grow to twelve hours a day, with late evenings on Thursdays.",
    "The catalogue moves online, so every loan can be reserved from home.",
    "Volunteers will guide visitors through the building during the first week.",
    "A small cafe on the mezzanine serves coffee from a cooperative roaster."
]

function longParagraph(seed) {
    var text = ""
    for (var i = 0; i < 5; ++i) text += (i ? " " : "") + sentences[(seed + i) % sentences.length]
    return text
}

function documentJson() {
    next = 1
    var blocks = [
        { paragraph: paragraph([["New reading hall opens in spring", 0]], { style: "heading" }) },
        { paragraph: paragraph([["After two years of building work, the hall is ready for ", 0],
                                ["every reader", 1], [" in the city.", 0]], { style: "lead" }) }
    ]
    for (var i = 0; i < 5; ++i) blocks.push(body(longParagraph(i)))
    var columnIds = [uuid(), uuid(), uuid()]
    var merged = cell("Ships in March")
    merged.rowSpan = 2
    blocks.push({ table: { id: uuid(), columns: [2, 1, 2], columnIds: columnIds, headerRows: 1, style: "grid",
        rows: [
            { id: uuid(), cells: [cell("Membership"), cell("Price"), cell("Card")] },
            { id: uuid(), cells: [cell("Adult"), cell("12"), merged] },
            { id: uuid(), cells: [cell("Student"), cell("6"), { id: uuid(), paragraphs: [] }] },
            { id: uuid(), cells: [cell("Child"), cell("Free"), cell("At the desk")] }
        ] } })
    for (var j = 5; j < 10; ++j) blocks.push(body(longParagraph(j)))
    blocks.push({ paragraph: paragraph([["Press contact: the library office.", 0]], { style: "body" }) })
    return JSON.stringify({
        schema: "qindatk.richtext", version: 2, documentId: uuid(),
        page: { widthPt: 595.276, heightPt: 841.89, marginsPt: [36, 36, 36, 36] },
        defaults: { family: "sans-serif", sizePt: 11 },
        styles: [
            { id: "body", name: "Body", kind: "paragraph", paragraph: { spaceAfterPt: 6, lineHeight: 1.2 },
              character: { sizePt: 10.5 } },
            { id: "heading", name: "Heading", kind: "paragraph", parent: "body",
              paragraph: { heading: 1, spaceAfterPt: 8 }, character: { sizePt: 18, weight: 700 } },
            { id: "lead", name: "Lead", kind: "paragraph", parent: "body", character: { sizePt: 12, italic: true } },
            { id: "emphasis", name: "Emphasis", kind: "character", character: { weight: 700, color: "#1c62d4" } },
            { id: "grid", name: "Price grid", kind: "table",
              table: { borderPt: 0.75, headerBackground: "#e8eef8", bandBackground: "#f4f6fa" } }
        ],
        formats: [{}, { style: "emphasis" }],
        lists: [],
        objects: [],
        blocks: blocks,
        textFrames: {
            story: "frame-a",
            frames: [
                { id: "frame-a", page: 0, xPt: 36, yPt: 36, widthPt: 250, heightPt: 360, next: "frame-b" },
                { id: "frame-b", page: 0, xPt: 309, yPt: 36, widthPt: 250, heightPt: 280 },
                { id: "frame-c", page: 1, xPt: 36, yPt: 36, widthPt: 523, heightPt: 720, columns: 2, gutterPt: 18 }
            ]
        }
    })
}
