// SPDX-License-Identifier: LGPL-3.0-or-later
// 500-page pagination locality. Prints MEASURE lines for the WP-050 evidence.
#include "rich_text_fixtures.h"
#include "rich_text_layout.h"
#include <QElapsedTimer>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;

namespace {
constexpr int Paragraphs = 500 * LinesPerPage;

struct PageSnapshot {
    QVector<QUuid> keys;
    QVector<quint64> revisions;
    QVector<Position> firsts;
};

PageSnapshot snapshot(const RichTextLayout &layout)
{
    PageSnapshot s;
    for (int i = 0; i < layout.pageCount(); ++i) {
        s.keys.append(layout.page(i).key);
        s.revisions.append(layout.page(i).revision);
        s.firsts.append(layout.page(i).first);
    }
    return s;
}

double ms(qint64 ns) { return double(ns) / 1e6; }
} // namespace

class RichTextScaleTest : public QObject {
    Q_OBJECT
private slots:
    void fiveHundredPagesTypingIsLocal()
    {
        RichTextDocument doc;
        buildLines(doc, Paragraphs);
        RichTextLayout layout;
        layout.setDocument(&doc);
        QElapsedTimer timer;
        timer.start();
        layout.paginateAll();
        const qint64 initial = timer.nsecsElapsed();
        QCOMPARE(layout.pageCount(), 500);
        QCOMPARE(layout.stats().paragraphsShaped, quint64(Paragraphs));
        QVERIFY(layout.stats().shapedLayoutsCached <= RichTextLayout::MaximumShapedLayouts);
        qInfo("MEASURE initial pagination 500 pages / %d paragraphs: %.1f ms, shaped cache %d (limit %d)",
              Paragraphs, ms(initial), layout.stats().shapedLayoutsCached, RichTextLayout::MaximumShapedLayouts);
        const PageSnapshot before = snapshot(layout);
        const Paragraph *target = doc.paragraphsInOrder().at(250 * LinesPerPage + 7);
        layout.resetStats();
        timer.restart();
        QVERIFY(doc.insertText({target->id, 2}, QStringLiteral("x")));
        const qint64 typing = timer.nsecsElapsed();
        const LayoutStats stats = layout.stats();
        qInfo("MEASURE type one character on page 250: %.2f ms, shaped %llu, pages rebuilt %llu, reused %llu, restart page %d, converged at %d",
              ms(typing), stats.paragraphsShaped, stats.pagesPaginated, stats.pagesReused,
              stats.lastRestartPage, stats.lastConvergedPage);
        QCOMPARE(stats.paragraphsShaped, 1u);
        QVERIFY(stats.pagesPaginated <= 2);
        QVERIFY(stats.pagesReused >= 248);
        const PageSnapshot after = snapshot(layout);
        QCOMPARE(after.keys, before.keys);
        for (int i = 0; i < 500; ++i)
            QCOMPARE(after.revisions.at(i) != before.revisions.at(i), i == 250);
    }

    void lineShiftConvergesAtTheNextSection()
    {
        RichTextDocument doc;
        buildLines(doc, Paragraphs, 25 * LinesPerPage); // a page break every 25 pages
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), 500);
        const PageSnapshot before = snapshot(layout);
        const Paragraph *target = doc.paragraphsInOrder().at(10 * LinesPerPage + 3);
        layout.resetStats();
        QElapsedTimer timer;
        timer.start();
        QVERIFY(doc.splitParagraph({target->id, 2}));
        const qint64 elapsed = timer.nsecsElapsed();
        const LayoutStats stats = layout.stats();
        qInfo("MEASURE new line on page 10 with sections: %.2f ms, shaped %llu, pages rebuilt %llu, reused %llu, converged at page %d",
              ms(elapsed), stats.paragraphsShaped, stats.pagesPaginated, stats.pagesReused, stats.lastConvergedPage);
        // Section 0 grows from 25 to 26 pages; section 1 onward is reused whole.
        QCOMPARE(layout.pageCount(), 501);
        QCOMPARE(stats.pagesPaginated, 16u);
        QCOMPARE(stats.pagesReused, 475u);
        QCOMPARE(stats.lastConvergedPage, 26);
        QVERIFY(stats.paragraphsShaped <= 2);
        const PageSnapshot after = snapshot(layout);
        for (int i = 25; i < 500; ++i) {
            QCOMPARE(after.keys.at(i + 1), before.keys.at(i));
            QCOMPARE(after.revisions.at(i + 1), before.revisions.at(i));
        }
        for (int i = 0; i < 10; ++i) QCOMPARE(after.revisions.at(i), before.revisions.at(i));
        QVERIFY(doc.undo());
        const PageSnapshot undone = snapshot(layout);
        QCOMPARE(undone.keys, before.keys);
        QCOMPARE(undone.firsts, before.firsts);
    }

    void lineShiftWithoutSectionsShapesOnce()
    {
        RichTextDocument doc;
        buildLines(doc, Paragraphs);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        const Paragraph *target = doc.paragraphsInOrder().at(10 * LinesPerPage + 3);
        layout.resetStats();
        QElapsedTimer timer;
        timer.start();
        QVERIFY(doc.splitParagraph({target->id, 2}));
        const qint64 elapsed = timer.nsecsElapsed();
        const LayoutStats stats = layout.stats();
        qInfo("MEASURE new line on page 10, one flow: %.2f ms, shaped %llu, pages rebuilt %llu, reused %llu",
              ms(elapsed), stats.paragraphsShaped, stats.pagesPaginated, stats.pagesReused);
        QCOMPARE(layout.pageCount(), 501);
        QVERIFY(stats.paragraphsShaped <= 2);   // the two halves of the split paragraph
        QCOMPARE(stats.pagesReused, 0u);        // every later page gains a shifted line
        QCOMPARE(layout.page(500).first, (Position{doc.paragraphsInOrder().last()->id, 0}));
    }

    void progressivePaginationHonoursItsBudget()
    {
        RichTextDocument doc;
        buildLines(doc, Paragraphs);
        RichTextLayout layout;
        layout.setDocument(&doc);
        int steps = 0;
        QElapsedTimer timer;
        timer.start();
        while (layout.paginateStep(8 * 1000 * 1000)) ++steps;
        qInfo("MEASURE progressive pagination: %d slices of 8 ms budget, %.1f ms total", steps + 1, ms(timer.nsecsElapsed()));
        QVERIFY(steps > 0);
        QCOMPARE(layout.pageCount(), 500);
        QVERIFY(layout.paginationComplete());
    }
};

QTEST_MAIN(RichTextScaleTest)
#include "tst_rich_text_scale.moc"
