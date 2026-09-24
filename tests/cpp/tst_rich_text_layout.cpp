// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_fixtures.h"
#include "rich_text_graphemes.h"
#include "rich_text_layout.h"
#include <QGlyphRun>
#include <QRawFont>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;

class RichTextLayoutTest : public QObject {
    Q_OBJECT
private slots:
    void fixedLineBoxesPaginateExactly()
    {
        RichTextDocument doc;
        buildLines(doc, 45);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QVERIFY(layout.paginationComplete());
        QCOMPARE(layout.pageCount(), 3); // 20 + 20 + 5 lines of 24 px in 480 px
        const auto ps = doc.paragraphsInOrder();
        QCOMPARE(layout.page(1).first, (Position{ps.at(20)->id, 0}));
        QCOMPARE(layout.page(2).first, (Position{ps.at(40)->id, 0}));
        QCOMPARE(layout.page(0).items.last().y, 19 * LinePx);
        QCOMPARE(layout.pageOfPosition({ps.at(39)->id, 2}), 1);
    }

    void hardFixtureTableAndFloatCrossPageBoundaries()
    {
        RichTextDocument doc;
        const HardFixture fixture = buildHardFixture(doc);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        // Page 0: 15 lines (360 px), then the header and two body rows.
        const Page &first = layout.page(0);
        const PageItem &rows = first.items.last();
        QCOMPARE(rows.kind, PageItem::TableRows);
        QCOMPARE(rows.first, 0);
        QCOMPARE(rows.count, 3);
        QCOMPARE(rows.y, 15 * LinePx);
        QCOMPARE(rows.height, 3 * RowPx + BorderPx);
        // Page 1 repeats the header, then rows 3..9, then the next paragraph.
        const Page &second = layout.page(1);
        QCOMPARE(second.items.at(0).kind, PageItem::TableRows);
        QVERIFY(second.items.at(0).headerRepeat);
        QCOMPARE(second.items.at(1).first, 3);
        QCOMPARE(second.items.at(1).count, 7);
        QCOMPARE(second.items.at(1).y, RowPx);
        // Every cell holds one mixed-script line (fonts would otherwise wrap).
        for (const Paragraph *p : doc.paragraphsInOrder())
            if (p->table) QCOMPARE(layout.metrics(p->id)->lines.size(), 1);
        // The float would cross page 1's bottom, so it moves with its anchor.
        int objectPage = -1;
        const auto rect = layout.objectRect(fixture.floatId, &objectPage);
        QVERIFY(rect);
        QCOMPARE(objectPage, 2);
        QCOMPARE(layout.pageOfPosition({fixture.floatAnchor, 0}), 2);
        QCOMPARE(rect->top(), layout.contentRect().top());
        const auto anchorPage = layout.page(2);
        const PageItem &anchorLines = anchorPage.items.first();
        QCOMPARE(anchorLines.block, fixture.floatAnchor);
        QVERIFY(!anchorLines.exclusions.isEmpty());
        // Text beside the 120 px float (+8 px gap) is narrower than the column.
        const auto lines = layout.selectionRects(2, {{fixture.floatAnchor, 0}, {fixture.floatAnchor, 10}});
        QVERIFY(!lines.isEmpty());
        QVERIFY(lines.first().right() <= layout.contentRect().right() - 128 + 0.5);
        // A page-anchored object belongs to its anchor paragraph's page.
        int pageObjectPage = -1;
        const auto pageObject = layout.objectRect(fixture.pageObjectId, &pageObjectPage);
        QVERIFY(pageObject);
        QCOMPARE(pageObjectPage, layout.pageOfPosition({fixture.pageObjectAnchor, 0}));
        QCOMPARE(pageObject->topLeft(), layout.contentRect().topLeft() + QPointF(200 * PxPerPt, 12 * PxPerPt));
    }

    void everyPositionMapsToItsPageAndBack()
    {
        RichTextDocument doc;
        buildHardFixture(doc);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        int checked = 0;
        for (const Paragraph *p : doc.paragraphsInOrder()) {
            QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, p->text);
            for (int offset = 0; offset <= p->text.size(); ++offset) {
                graphemes.setPosition(offset);
                if (!graphemes.isAtBoundary()) continue;
                const CaretBox caret = layout.caretBox({p->id, offset});
                QVERIFY2(caret.page >= 0, qPrintable(p->text));
                const Position hit = layout.hitTest(caret.page, caret.rect.center());
                const CaretBox back = layout.caretBox(hit);
                QCOMPARE(back.page, caret.page);
                QVERIFY2(qAbs(back.rect.x() - caret.rect.x()) < 0.5, qPrintable(QStringLiteral("%1 @%2").arg(p->text).arg(offset)));
                ++checked;
            }
        }
        QVERIFY(checked > 500);
    }

    void graphemeClustersAndBidiRuns()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral(
            "a\U0001F469‍\U0001F469‍\U0001F467\U0001F1FA\U0001F1F8éक्ष "
            "אבג end"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        // Visual right from the start steps over whole grapheme clusters.
        QVector<int> visited{0};
        Position at{p->id, 0};
        for (int guard = 0; guard < 64; ++guard) {
            const Position next = layout.visualMove(at, true);
            if (next.paragraph != p->id || next == at) break;
            at = next;
            visited.append(at.offset);
        }
        QVERIFY(visited.contains(1));
        QVERIFY(visited.contains(9));  // after the 8-unit family ZWJ sequence
        QVERIFY(visited.contains(13)); // after the 4-unit flag
        QVERIFY(visited.contains(15)); // after e + combining acute
        // Qt alone would stop inside surrogate pairs here (bidi paragraph).
        QVERIFY(!visited.contains(2) && !visited.contains(14));
        QVERIFY(!visited.contains(11) && !visited.contains(12));
        QCOMPARE(visited.last(), int(p->text.size()));
        // Qt 6.11's QTextBoundaryFinder alone splits this flag (it follows an
        // emoji); the paragraph boundaries used by the editor keep it whole.
        const GraphemeBoundaries boundaries(p->text);
        QVERIFY(!boundaries.isBoundary(11));
        QCOMPARE(boundaries.next(9), 13);
        QCOMPARE(boundaries.previous(13), 9);
        QCOMPARE(GraphemeBoundaries(QStringLiteral("🇺🇸🇩🇪")).next(0), 4);
        QCOMPARE(GraphemeBoundaries(QStringLiteral("🇺🇸🇩")).next(4), 6);
        // Inside the RTL run a larger logical offset sits further left.
        const int alef = int(p->text.indexOf(QChar(0x05D0)));
        const qreal x1 = layout.caretBox({p->id, alef + 1}).rect.x();
        const qreal x2 = layout.caretBox({p->id, alef + 2}).rect.x();
        QVERIFY2(x2 < x1, qPrintable(QStringLiteral("%1 %2").arg(x1).arg(x2)));
    }

    void missingFontsFallBackWithGlyphsForEveryScript()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Latin 中文 العربية"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        CharFormatPatch missing;
        missing.family = QStringLiteral("No Such Font QTK");
        QVERIFY(doc.applyCharFormat({p->id, 0}, {p->id, 5}, missing));
        CharFormatPatch latinOnly;
        latinOnly.family = QStringLiteral("Liberation Sans");
        QVERIFY(doc.applyCharFormat({p->id, 5}, doc.endPosition(), latinOnly));
        QCOMPARE(doc.missingFontFamilies(), QStringList{QStringLiteral("No Such Font QTK")});
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        ParagraphMetrics &m = layout.ensureParagraph(*p, layout.contentRect().width());
        QTextLayout *shaped = layout.shaped(*p, m);
        QStringList families;
        for (const QGlyphRun &run : shaped->glyphRuns()) {
            families.append(run.rawFont().familyName());
            for (quint32 glyph : run.glyphIndexes()) QVERIFY2(glyph != 0, qPrintable(run.rawFont().familyName()));
        }
        QVERIFY(!families.contains(QStringLiteral("No Such Font QTK")));
        QVERIFY2(QSet<QString>(families.begin(), families.end()).size() >= 3, qPrintable(families.join(u',')));
    }

    void preeditReflowsOnlyItsParagraph()
    {
        RichTextDocument doc;
        buildLines(doc, 30);
        const Paragraph *p = doc.paragraphsInOrder().at(25);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        layout.resetStats();
        layout.setPreedit({p->id, 2}, QStringLiteral("日本"), {}, 1);
        QCOMPARE(layout.stats().paragraphsShaped, 1u);
        const CaretBox composing = layout.caretBox({p->id, 2});
        const CaretBox plain = layout.caretBox({p->id, 3});
        QVERIFY(composing.rect.x() > layout.contentRect().left());
        QVERIFY(plain.rect.x() > composing.rect.x()); // text after the preedit moved right
        layout.setPreedit({}, {}, {});
        QCOMPARE(layout.caretBox({p->id, 3}).rect.x() < plain.rect.x(), true);
    }

    void objectsTakeTheirWidthInTheLine()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("ab text\ncd text"));
        const auto ps = doc.paragraphsInOrder();
        RichObject inlineObject;
        inlineObject.widthPt = 30;  // 40 px
        inlineObject.heightPt = 12;
        QVERIFY(doc.insertObject({ps.at(0)->id, 2}, inlineObject));
        RichObject floating;
        floating.placement = Placement::FloatEnd;
        floating.widthPt = 60;
        floating.heightPt = 60;
        QVERIFY(doc.insertObject({ps.at(1)->id, 0}, floating));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        const qreal before = layout.caretBox({ps.at(0)->id, 2}).rect.x();
        const qreal after = layout.caretBox({ps.at(0)->id, 3}).rect.x();
        // Exact to Qt's 26.6 fixed-point advances (1/64 px per rounding).
        QVERIFY2(qAbs(after - before - 30 * PxPerPt) <= 1.0 / 16, qPrintable(QString::number(after - before)));
        // An anchored object's anchor character is zero width in its line.
        QCOMPARE(layout.caretBox({ps.at(1)->id, 1}).rect.x(), layout.caretBox({ps.at(1)->id, 0}).rect.x());
        QCOMPARE(layout.caretBox({ps.at(1)->id, 0}).rect.x(), layout.contentRect().left());
    }

    void renumberingRevisesReusedPages()
    {
        RichTextDocument doc;
        buildLines(doc, 3 * LinesPerPage);
        const auto ps = doc.paragraphsInOrder();
        QVERIFY(doc.setList({ps.first()->id, 0}, {ps.last()->id, 0}, ListKind::Decimal));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), 3);
        const quint64 before = layout.page(2).revision;
        QCOMPARE(layout.listMarker(ps.last()->id), QStringLiteral("60."));
        // Lifting the first item out of the list changes only page 0's lines,
        // so pages 1 and 2 are reused, but every later number shifts.
        QVERIFY(doc.setList({ps.first()->id, 0}, {ps.first()->id, 0}, std::nullopt));
        QCOMPARE(layout.listMarker(ps.last()->id), QStringLiteral("59.")); // before pagination
        layout.paginateAll();
        QVERIFY(layout.stats().lastConvergedPage >= 0);
        QVERIFY(layout.page(2).revision != before);
    }

    void continuousModeIsOneFlow()
    {
        RichTextDocument doc;
        buildLines(doc, 45);
        RichTextLayout layout;
        layout.setMode(RichTextLayout::Mode::Continuous);
        layout.setFlowWidth(300);
        layout.setDocument(&doc);
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), 1);
        QCOMPARE(layout.flowHeight(), 45 * LinePx);
        const Position hit = layout.hitTest(0, QPointF(1, 44 * LinePx + 2));
        QCOMPARE(hit.paragraph, doc.paragraphsInOrder().last()->id);
    }
};

QTEST_MAIN(RichTextLayoutTest)
#include "tst_rich_text_layout.moc"
