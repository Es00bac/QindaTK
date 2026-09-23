// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"

// Known-answer fixtures shared by the rich-text tests. Every paragraph uses an
// exact 18 pt (24 px) line box, so page arithmetic is independent of font
// metrics while shaping, fallback and bidi still run on real mixed scripts.
namespace QindaTK::RichTextFixtures {

using namespace QindaTK::RichText;

inline constexpr qreal LinePx = 24.0;       // 18 pt
inline constexpr qreal BorderPx = 1.0;      // 0.75 pt
inline constexpr qreal RowPx = BorderPx + 2 * 4.0 * PxPerPt + LinePx; // default 4 pt padding
inline constexpr int LinesPerPage = 20;     // 480 px content height

inline PageSetup fixturePage()
{
    PageSetup page;
    page.widthPt = 372;                     // 300 pt = 400 px content width
    page.heightPt = 432;                    // 360 pt = 480 px content height
    page.marginTopPt = page.marginRightPt = page.marginBottomPt = page.marginLeftPt = 36;
    return page;
}

inline QString mixedLabel(int i)
{
    static const QStringList scripts{QStringLiteral("שלום"), QStringLiteral("مرحبا"),
                                     QStringLiteral("中文"), QStringLiteral("क्ष"),
                                     QStringLiteral("\U0001F469‍\U0001F469‍\U0001F467"),
                                     QStringLiteral("été")};
    return QStringLiteral("L%1 %2").arg(i).arg(scripts.at(i % scripts.size()));
}

inline void fixLineBoxes(RichTextDocument &doc)
{
    ParagraphFormatPatch patch;
    patch.lineHeightPt = 18;
    doc.setParagraphFormat(doc.startPosition(), doc.endPosition(), patch);
}

// n one-line paragraphs "L<i> <script sample>".
inline void buildLines(RichTextDocument &doc, int n, int breakEvery = 0)
{
    doc.setPageSetup(fixturePage());
    QStringList lines;
    for (int i = 0; i < n; ++i) lines.append(mixedLabel(i));
    doc.insertText(doc.startPosition(), lines.join(u'\n'));
    fixLineBoxes(doc);
    if (breakEvery > 0) {
        const auto ps = doc.paragraphsInOrder();
        ParagraphFormatPatch pageBreak;
        pageBreak.pageBreakBefore = true;
        for (int i = breakEvery; i < ps.size(); i += breakEvery)
            doc.setParagraphFormat({ps.at(i)->id, 0}, {ps.at(i)->id, 0}, pageBreak);
    }
}

struct HardFixture {
    QString floatId;
    QUuid floatAnchor;
    QString pageObjectId;
    QUuid pageObjectAnchor;
    QUuid table;
};

// 15 lines, a 10 x 3 table with a repeated header crossing page 0 -> 1, five
// lines, a paragraph whose 90 pt float cannot fit page 1 (so it moves to
// page 2 with its anchor), and a page-anchored object further down.
inline HardFixture buildHardFixture(RichTextDocument &doc)
{
    HardFixture fixture;
    doc.setPageSetup(fixturePage());
    QStringList before;
    for (int i = 0; i < 15; ++i) before.append(mixedLabel(i));
    doc.insertText(doc.startPosition(), before.join(u'\n'));
    const auto cell = doc.insertTable(doc.endPosition(), 10, 3, 1);
    const Table *table = doc.paragraph(cell->paragraph)->table;
    fixture.table = table->id;
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 3; ++c)
            doc.insertText({table->rows.at(r).cells.at(c).paragraphs.first()->id, 0},
                           r == 0 ? QStringLiteral("Head %1").arg(c)
                                  : QStringLiteral("R%1 %2").arg(r).arg(r % 2 ? QStringLiteral("שלום")
                                                                             : QStringLiteral("中文")));
    QStringList after;
    for (int i = 0; i < 5; ++i) after.append(mixedLabel(100 + i));
    after.append(QStringLiteral("Anchored paragraph: its float is taller than the space left on the page, "
                                "so the anchor paragraph moves to the next page together with the float, "
                                "and these words wrap beside the float in a narrower column."));
    for (int i = 0; i < 6; ++i) after.append(mixedLabel(200 + i));
    doc.insertText(doc.endPosition(), after.join(u'\n'));
    const auto ps = doc.paragraphsInOrder();
    const Paragraph *anchor = ps.at(ps.size() - 7);
    fixture.floatAnchor = anchor->id;
    RichObject floatObject;
    floatObject.placement = Placement::FloatEnd;
    floatObject.widthPt = 90;
    floatObject.heightPt = 90;
    floatObject.altText = QStringLiteral("Float");
    doc.insertObject({anchor->id, 0}, floatObject);
    const Paragraph *pageAnchor = ps.at(ps.size() - 2);
    fixture.pageObjectAnchor = pageAnchor->id;
    RichObject pageObject;
    pageObject.placement = Placement::Page;
    pageObject.widthPt = 60;
    pageObject.heightPt = 40;
    pageObject.offsetXPt = 200;
    pageObject.offsetYPt = 12;
    pageObject.altText = QStringLiteral("Stamp");
    doc.insertObject({pageAnchor->id, 0}, pageObject);
    for (const RichObject &object : doc.objects())
        (object.placement == Placement::FloatEnd ? fixture.floatId : fixture.pageObjectId) = object.id;
    fixLineBoxes(doc);
    return fixture;
}

} // namespace QindaTK::RichTextFixtures
