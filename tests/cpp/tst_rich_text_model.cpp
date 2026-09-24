// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_document.h"
#include "rich_text_spans.h"
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;

class RichTextModelTest : public QObject {
    Q_OBJECT
    static Position pos(const Paragraph *p, int offset) { return {p->id, offset}; }
    static QString textOf(const RichTextDocument &doc) { return doc.text(); }

    // Body paragraphs "one", "two", "three" in a fresh document.
    static QVector<Paragraph *> threeParagraphs(RichTextDocument &doc)
    {
        doc.insertText(doc.startPosition(), QStringLiteral("one\ntwo\nthree"));
        return doc.paragraphsInOrder();
    }

private slots:
    void freshDocumentHasOneEmptyParagraph()
    {
        RichTextDocument doc;
        QCOMPARE(doc.paragraphCount(), 1);
        QCOMPARE(doc.text(), QString());
        QVERIFY(!doc.canUndo());
        QVERIFY(doc.blocks().last().paragraph);
    }

    void insertUndoRedo()
    {
        RichTextDocument doc;
        const QString before = doc.revisionString();
        const auto caret = doc.insertText(doc.startPosition(), QStringLiteral("Hello"));
        QVERIFY(caret);
        QCOMPARE(caret->offset, 5);
        QCOMPARE(doc.text(), QStringLiteral("Hello"));
        QVERIFY(doc.revisionString() != before);
        QVERIFY(doc.undo());
        QCOMPARE(doc.text(), QString());
        QVERIFY(doc.redo());
        QCOMPARE(doc.text(), QStringLiteral("Hello"));
    }

    void typingCoalescesWhenTheCaretContinues()
    {
        RichTextDocument doc;
        Position caret = doc.startPosition();
        for (const QString &letter : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            QVERIFY(doc.beginTransaction(QStringLiteral("Typing"), QStringLiteral("typing"), {caret, caret}));
            caret = *doc.insertText(caret, letter);
            QVERIFY(doc.commitTransaction({caret, caret}));
        }
        // A jump elsewhere starts a new step even with the same merge key.
        const Position start = doc.startPosition();
        QVERIFY(doc.beginTransaction(QStringLiteral("Typing"), QStringLiteral("typing"), {start, start}));
        doc.insertText(start, QStringLiteral(">"));
        QVERIFY(doc.commitTransaction({{start.paragraph, 1}, {start.paragraph, 1}}));
        QCOMPARE(doc.text(), QStringLiteral(">abc"));
        QVERIFY(doc.undo());
        QCOMPARE(doc.text(), QStringLiteral("abc"));
        QVERIFY(doc.undo());
        QCOMPARE(doc.text(), QString());
        QVERIFY(!doc.canUndo());
    }

    void positionsNeverSplitSurrogatePairs()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("a\U0001F469b"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        QVERIFY(doc.isValidPosition(pos(p, 1)));
        QVERIFY(!doc.isValidPosition(pos(p, 2)));
        QVERIFY(doc.isValidPosition(pos(p, 3)));
        QCOMPARE(doc.clampPosition(pos(p, 2)).offset, 1);
        QVERIFY(!doc.insertText(pos(p, 2), QStringLiteral("x")));
        QCOMPARE(p->text.size(), 4);
    }

    void splitKeepsIdentityAndMapsAnchors()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("HelloWorld"));
        Paragraph *p = doc.paragraphsInOrder().first();
        const QUuid original = p->id;
        PositionAnchor anchor{pos(p, 7)};
        doc.registerAnchor(&anchor);
        const auto caret = doc.splitParagraph(pos(p, 5));
        QVERIFY(caret);
        QCOMPARE(doc.paragraph(original)->text, QStringLiteral("Hello"));
        QCOMPARE(doc.paragraph(caret->paragraph)->text, QStringLiteral("World"));
        QCOMPARE(anchor.position.paragraph, caret->paragraph);
        QCOMPARE(anchor.position.offset, 2);
        QVERIFY(doc.undo());
        QCOMPARE(doc.paragraphCount(), 1);
        QCOMPARE(anchor.position.paragraph, original);
        QCOMPARE(anchor.position.offset, 7);
        doc.unregisterAnchor(&anchor);
    }

    void enterAtParagraphStartKeepsTextIdentityAndPageBreak()
    {
        RichTextDocument doc;
        const auto ps = threeParagraphs(doc);
        const QUuid two = ps.at(1)->id;
        QVERIFY(doc.insertPageBreak(pos(ps.at(1), 0)));
        QVERIFY(doc.paragraph(two)->format.pageBreakBefore);
        const auto caret = doc.splitParagraph({two, 0});
        QVERIFY(caret);
        QCOMPARE(caret->paragraph, two);
        const auto order = doc.paragraphsInOrder();
        QCOMPARE(order.size(), 4);
        QCOMPARE(order.at(1)->text, QString());
        QVERIFY(order.at(1)->format.pageBreakBefore);
        QCOMPARE(order.at(2)->id, two);
        QVERIFY(!order.at(2)->format.pageBreakBefore);
    }

    void formattingSplitsRunsAndUndoesAsOneStep()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Hello World"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        CharFormatPatch bold;
        bold.weight = 700;
        QVERIFY(doc.applyCharFormat(pos(p, 0), pos(p, 5), bold));
        QCOMPARE(p->spans.size(), 2);
        QCOMPARE(p->spans.at(0).length, 5);
        QCOMPARE(doc.charFormat(p->spans.at(0).format).weight, 700);
        CharFormatPatch italic;
        italic.italic = 1;
        QVERIFY(doc.applyCharFormat(pos(p, 3), pos(p, 8), italic));
        QCOMPARE(p->spans.size(), 4); // Hel | lo(bold italic) | " Wo"(italic) | rld
        QCOMPARE(doc.charFormat(p->spans.at(1).format).italic, qint8(1));
        QCOMPARE(doc.charFormat(p->spans.at(1).format).weight, 700);
        QCOMPARE(doc.charFormat(p->spans.at(2).format).weight, 0);
        QVERIFY(doc.undo());
        QCOMPARE(p->spans.size(), 2);
        QVERIFY(doc.undo());
        QCOMPARE(p->spans.size(), 1);
        QCOMPARE(p->spans.first().format, 0);
    }

    void deleteAcrossParagraphsMergesAndRestores()
    {
        RichTextDocument doc;
        const auto ps = threeParagraphs(doc);
        const QVector<QUuid> ids{ps.at(0)->id, ps.at(1)->id, ps.at(2)->id};
        QVERIFY(doc.deleteRange(pos(ps.at(0), 1), pos(ps.at(2), 2)));
        QCOMPARE(doc.paragraphCount(), 1);
        QCOMPARE(doc.text(), QStringLiteral("oree"));
        QCOMPARE(doc.paragraphsInOrder().first()->id, ids.at(0));
        QVERIFY(doc.undo());
        const auto restored = doc.paragraphsInOrder();
        QCOMPARE(restored.size(), 3);
        for (int i = 0; i < 3; ++i) QCOMPARE(restored.at(i)->id, ids.at(i));
        QCOMPARE(doc.text(), QStringLiteral("one\ntwo\nthree"));
    }

    void deleteOverAWholeTableRemovesIt()
    {
        RichTextDocument doc;
        const auto ps = threeParagraphs(doc);
        const auto cell = doc.insertTable(pos(ps.at(1), 0), 2, 2, 1);
        QVERIFY(cell);
        doc.insertText(*cell, QStringLiteral("head"));
        QCOMPARE(doc.blocks().size(), 4);
        const QUuid tableId = doc.blocks().at(1).table->id;
        QVERIFY(doc.deleteRange(pos(ps.at(0), 2), pos(ps.at(1), 1)));
        QCOMPARE(doc.blocks().size(), 2);
        QCOMPARE(doc.text(), QStringLiteral("onwo\nthree"));
        QVERIFY(doc.undo());
        QCOMPARE(doc.blocks().size(), 4);
        QCOMPARE(doc.blocks().at(1).table->id, tableId);
        QCOMPARE(doc.table(tableId)->rows.at(0).cells.at(0).paragraphs.first()->text, QStringLiteral("head"));
    }

    void deleteIntoATableClearsCellsButKeepsStructure()
    {
        RichTextDocument doc;
        const auto ps = threeParagraphs(doc);
        const auto first = doc.insertTable(pos(ps.at(1), 0), 2, 2);
        QVERIFY(first);
        Table *table = doc.blocks().at(1).table.get();
        const QStringList texts{QStringLiteral("a1"), QStringLiteral("b1"), QStringLiteral("a2"), QStringLiteral("b2")};
        for (int i = 0; i < 4; ++i)
            doc.insertText({table->rows.at(i / 2).cells.at(i % 2).paragraphs.first()->id, 0}, texts.at(i));
        const Paragraph *a2 = table->rows.at(1).cells.at(0).paragraphs.first().get();
        QVERIFY(doc.deleteRange(pos(ps.at(0), 1), pos(a2, 1)));
        QCOMPARE(doc.blocks().size(), 4);
        QCOMPARE(ps.at(0)->text, QStringLiteral("o"));
        QCOMPARE(table->rows.at(0).cells.at(0).paragraphs.first()->text, QString());
        QCOMPARE(table->rows.at(0).cells.at(1).paragraphs.first()->text, QString());
        QCOMPARE(a2->text, QStringLiteral("2"));
        QCOMPARE(table->rows.at(1).cells.at(1).paragraphs.first()->text, QStringLiteral("b2"));
        QVERIFY(doc.undo());
        QCOMPARE(table->rows.at(0).cells.at(1).paragraphs.first()->text, QStringLiteral("b1"));
        QCOMPARE(ps.at(0)->text, QStringLiteral("one"));
    }

    void objectsFollowTheirAnchorCharacter()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("ab"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        RichObject image;
        image.placement = Placement::FloatEnd;
        image.widthPt = 120;
        image.heightPt = 80;
        image.altText = QStringLiteral("Chart");
        const auto after = doc.insertObject(pos(p, 1), image);
        QVERIFY(after);
        QCOMPARE(p->text, QStringLiteral("a￼b"));
        QCOMPARE(doc.objects().size(), 1);
        const QString id = doc.objects().begin().key();
        QCOMPARE(doc.charFormat(Spans::formatAt(p->spans, 1)).objectId, id);
        // Typing after the object never copies its identity.
        QVERIFY(doc.charFormat(doc.formatIndexAt(pos(p, 2))).objectId.isEmpty());
        QVERIFY(doc.deleteRange(pos(p, 1), pos(p, 2)));
        QVERIFY(doc.objects().isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(doc.object(id)->altText, QStringLiteral("Chart"));
        QCOMPARE(doc.object(id)->placement, Placement::FloatEnd);
        RichObject resized = *doc.object(id);
        resized.heightPt = 40;
        QVERIFY(doc.setObject(resized));
        QCOMPARE(doc.object(id)->heightPt, 40.0);
        QVERIFY(doc.undo());
        QCOMPARE(doc.object(id)->heightPt, 80.0);
    }

    void listsJoinNeighboursAndChangeLevels()
    {
        RichTextDocument doc;
        const auto ps = threeParagraphs(doc);
        QVERIFY(doc.setList(pos(ps.at(0), 0), pos(ps.at(1), 0), ListKind::Decimal));
        const QString listId = ps.at(0)->format.listId;
        QVERIFY(!listId.isEmpty());
        QCOMPARE(ps.at(1)->format.listId, listId);
        QVERIFY(doc.setList(pos(ps.at(2), 0), pos(ps.at(2), 0), ListKind::Decimal));
        QCOMPARE(ps.at(2)->format.listId, listId);
        QVERIFY(doc.changeListLevel(pos(ps.at(1), 0), pos(ps.at(1), 0), 1));
        QCOMPARE(ps.at(1)->format.listLevel, 1);
        QCOMPARE(doc.list(listId)->levels.at(1).kind, ListKind::LowerAlpha);
        QVERIFY(doc.setList(pos(ps.at(0), 0), pos(ps.at(2), 0), std::nullopt));
        QVERIFY(ps.at(1)->format.listId.isEmpty());
        QCOMPARE(ps.at(1)->format.listLevel, 0);
        QCOMPARE(listMarkerText(ListKind::LowerRoman, 14), QStringLiteral("xiv."));
        QCOMPARE(listMarkerText(ListKind::UpperAlpha, 28), QStringLiteral("AB."));
    }

    void tablesAreNeverLastAndNeverNested()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("end"));
        const auto cell = doc.insertTable(doc.endPosition(), 3, 2, 1);
        QVERIFY(cell);
        QCOMPARE(doc.blocks().size(), 3);
        QVERIFY(doc.blocks().at(1).table);
        QVERIFY(doc.blocks().last().paragraph);
        QCOMPARE(doc.blocks().at(1).table->headerRows, 1);
        QVERIFY(!doc.insertTable(*cell, 1, 1));
        QVERIFY(!doc.insertPageBreak(*cell));
    }

    void sectionsMoveWithStableIdentity()
    {
        RichTextDocument doc;
        const auto ps = threeParagraphs(doc);
        QVERIFY(doc.insertPageBreak(pos(ps.at(1), 0)));
        QVERIFY(doc.insertPageBreak(pos(ps.at(2), 0)));
        const QVector<QUuid> sections = doc.sectionIds();
        QCOMPARE(sections, (QVector<QUuid>{ps.at(0)->id, ps.at(1)->id, ps.at(2)->id}));
        PositionAnchor caret{pos(ps.at(0), 2)};
        doc.registerAnchor(&caret);
        QVERIFY(doc.moveSection(ps.at(0)->id, 3));
        QCOMPARE(doc.text(), QStringLiteral("two\nthree\none"));
        QCOMPARE(doc.sectionIds(), (QVector<QUuid>{ps.at(1)->id, ps.at(2)->id, ps.at(0)->id}));
        QVERIFY(ps.at(0)->format.pageBreakBefore);
        QCOMPARE(caret.position, pos(ps.at(0), 2));
        QVERIFY(doc.undo());
        QCOMPARE(doc.text(), QStringLiteral("one\ntwo\nthree"));
        QCOMPARE(doc.sectionIds(), sections);
        doc.unregisterAnchor(&caret);
    }

    void anchorsSurviveUnrelatedEdits()
    {
        RichTextDocument doc;
        const auto ps = threeParagraphs(doc);
        PositionAnchor anchor{pos(ps.at(2), 3)};
        doc.registerAnchor(&anchor);
        doc.insertText(pos(ps.at(0), 0), QStringLiteral("zero\n"));
        doc.insertText(pos(ps.at(1), 1), QStringLiteral("XX"));
        doc.deleteRange(pos(ps.at(0), 0), pos(ps.at(0), 2));
        QCOMPARE(anchor.position, pos(ps.at(2), 3));
        doc.insertText(pos(ps.at(2), 1), QStringLiteral("__"));
        QCOMPARE(anchor.position.offset, 5);
        doc.deleteRange(pos(ps.at(2), 0), pos(ps.at(2), 4));
        QCOMPARE(anchor.position.offset, 1);
        doc.unregisterAnchor(&anchor);
    }

    void cancelledTransactionLeavesNoTrace()
    {
        RichTextDocument doc;
        threeParagraphs(doc);
        const QString revision = doc.revisionString();
        const QString before = doc.toJson();
        QSignalSpy content(&doc, &RichTextDocument::contentChanged);
        QVERIFY(doc.beginTransaction(QStringLiteral("Scratch")));
        doc.insertText(doc.startPosition(), QStringLiteral("temporary\n"));
        doc.deleteRange(doc.startPosition(), doc.endPosition());
        doc.cancelTransaction();
        QCOMPARE(doc.toJson(), before);
        QCOMPARE(doc.revisionString(), revision);
        QCOMPARE(content.count(), 0);
    }

    void staleStepIsRejectedWithoutChange()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("abc"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        QVERIFY(doc.beginTransaction(QStringLiteral("Stale")));
        StepText stale;
        stale.paragraph = p->id;
        stale.offset = 0;
        stale.removed = QStringLiteral("xyz");
        stale.removedSpans = {{3, 0}};
        const OperationError error = doc.apply(stale);
        QCOMPARE(error.code, ErrorCode::Conflict);
        doc.cancelTransaction();
        QCOMPARE(p->text, QStringLiteral("abc"));
    }

    void readOnlyRejectsEdits()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("abc"));
        doc.setReadOnly(true);
        QVERIFY(!doc.insertText(doc.startPosition(), QStringLiteral("x")));
        QVERIFY(!doc.canUndo());
        QVERIFY(!doc.undo());
        QCOMPARE(doc.text(), QStringLiteral("abc"));
    }

    void externalHistoryReplaysGuardedSteps()
    {
        RichTextDocument doc;
        doc.setExternalUndo(true);
        QSignalSpy committed(&doc, &RichTextDocument::committed);
        doc.insertText(doc.startPosition(), QStringLiteral("abc"));
        doc.insertText(doc.endPosition(), QStringLiteral("def"));
        QCOMPARE(committed.count(), 2);
        QVERIFY(!doc.canUndo());
        QVERIFY(!doc.undo());
        const QString second = committed.at(1).at(0).toString();
        const QString first = committed.at(0).at(0).toString();
        QVERIFY(!doc.applyHistoryChanges(second, true, QStringLiteral("0")));
        QCOMPARE(doc.text(), QStringLiteral("abcdef"));
        QVERIFY(!doc.applyHistoryChanges(second, false, doc.revisionString()));
        QVERIFY(doc.applyHistoryChanges(second, true, doc.revisionString()));
        QCOMPARE(doc.text(), QStringLiteral("abc"));
        QVERIFY(doc.applyHistoryChanges(first, true, doc.revisionString()));
        QCOMPARE(doc.text(), QString());
        QVERIFY(doc.applyHistoryChanges(first, false, doc.revisionString()));
        QVERIFY(doc.applyHistoryChanges(second, false, doc.revisionString()));
        QCOMPARE(doc.text(), QStringLiteral("abcdef"));
        QVERIFY(doc.releaseHistoryTransaction(first));
        QVERIFY(!doc.applyHistoryChanges(first, true, doc.revisionString()));
    }

    void cleanTextDropsControlsAndObjectCharacters()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("a\r\nb\u0007c￼d e f"));
        QCOMPARE(doc.paragraphCount(), 3);
        QCOMPARE(doc.paragraphsInOrder().at(1)->text, QStringLiteral("bcd e"));
        QVERIFY(doc.objects().isEmpty());
    }
};

QTEST_GUILESS_MAIN(RichTextModelTest)
#include "tst_rich_text_model.moc"
