// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// The plain text of a field report as qindatk.richtext version 1
// (docs/rich-text.md#persistence). Main.qml adds its sources, captions,
// cross-reference, citations, equation, bibliography, comment and tracked
// change through the document's own operations (docs/references-review.md),
// so the demo shows the real behaviour rather than a stored picture of it.
// Demo data only.

function uuid(n) {
    var hex = n.toString(16)
    while (hex.length < 12) hex = "0" + hex
    return "00000000-0000-4000-8000-" + hex
}

function paragraph(n, text, format) {
    return { paragraph: { id: uuid(n), text: text, spans: text.length ? [[text.length, 0]] : [], mark: 0,
                          format: format || {} } }
}

// Paragraph indexes the demo edits (Main.qml).
var Title = 0
var Method = 1
var FigureCaption = 2
var Balance = 3
var Estimates = 4
var ReferencesHeading = 5
var Bibliography = 6

function documentJson() {
    return JSON.stringify({
        schema: "qindatk.richtext", version: 1, documentId: uuid(99),
        page: { widthPt: 595.276, heightPt: 841.89, marginsPt: [56, 56, 56, 56] },
        defaults: { family: "sans-serif", sizePt: 11 },
        formats: [{}],
        lists: [],
        objects: [],
        blocks: [
            paragraph(1, "Field report", { heading: 1, spaceAfterPt: 8 }),
            paragraph(2, "The revised method follows prior work and extends it to the winter stations. "
                         + "The readings are summarized in .", { spaceAfterPt: 6 }),
            paragraph(3, ": Sensor readings over the season.", { align: "center", spaceAfterPt: 10 }),
            paragraph(4, "The energy balance at each station is  ", { align: "center", spaceAfterPt: 10 }),
            paragraph(5, "Initial estimates were checked against the archive of earlier seasons.",
                      { spaceAfterPt: 6 }),
            paragraph(6, "References", { heading: 2, spaceBeforePt: 12, spaceAfterPt: 6 }),
            paragraph(7, "")
        ]
    })
}
