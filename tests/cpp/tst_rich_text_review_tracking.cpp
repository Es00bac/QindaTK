// SPDX-License-Identifier: LGPL-3.0-or-later
// Tracked structure and formatting (WP-052, D-116): paragraph breaks,
// character and paragraph formatting, table rows, equation sources and
// reference edits are recorded while tracking and accept, reject and undo
// exactly; moves keep field and comment identity under tracking; review
// items are navigable from the document and the editing surface.
#include "rich_text_codec.h"
#include "rich_text_scene.h"
#include "rich_text_spans.h"

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextScene;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QStringList texts(const RichTextDocument &doc)
{
    QStringList list;
    for (const Paragraph *p : doc.paragraphsInOrder()) list.append(p->text);
    return list;
}

QVariantMap item(const RichTextDocument &doc, const QString &id)
{
    for (const QVariant &value : doc.reviewItems())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

QVariantMap reference(const RichTextDocument &doc, const QString &id)
{
    for (const QVariant &value : doc.referenceSnapshots())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

const CharFormat &formatAt(const RichTextDocument &doc, const Paragraph *p, int offset)
{
    return doc.charFormat(Spans::formatAt(p->spans, offset));
}

QStringList changesOf(const RichTextDocument &doc, ChangeKind kind)
{
    QStringList ids;
    for (const TrackedChange &change : doc.annotations().changes)
        if (change.kind == kind) ids.append(change.id);
    ids.sort();
    return ids;
}

bool resolveAll(RichTextDocument &doc, bool accept)
{
    for (const QString &id : doc.annotations().changes.keys())
        if (doc.annotations().changes.contains(id) && !(accept ? doc.acceptChange(id) : doc.rejectChange(id)))
            return false;
    return true;
}

} // namespace

class RichTextReviewTrackingTest : public QObject {
    Q_OBJECT
private slots:
    void paragraphBreaksAreTracked()
    {
        // A tracked delete across paragraphs, once accepted, is the untracked delete.
        RichTextDocument plain, tracked;
        for (RichTextDocument *doc : {&plain, &tracked})
            doc->insertText(doc->startPosition(), QStringLiteral("Alpha beta\nGamma delta\nEpsilon"));
        const auto range = [](const RichTextDocument &doc) {
            const auto &flat = doc.paragraphsInOrder();
            return std::make_pair(Position{flat.at(0)->id, 6}, Position{flat.at(2)->id, 3});
        };
        QVERIFY(plain.deleteRange(range(plain).first, range(plain).second));
        QCOMPARE(texts(plain), QStringList{QStringLiteral("Alpha ilon")});
        tracked.setReviewAuthor(QStringLiteral("Ada"));
        tracked.setTrackChanges(true);
        QVERIFY(tracked.deleteRange(range(tracked).first, range(tracked).second));
        QCOMPARE(texts(tracked).size(), 3);
        const QStringList deletions = changesOf(tracked, ChangeKind::Deletion);
        QCOMPARE(deletions.size(), 1);
        const Paragraph *first = tracked.paragraphsInOrder().first();
        QCOMPARE(tracked.charFormat(first->markFormat).deletionId, deletions.first());
        QVERIFY(item(tracked, deletions.first()).value(QStringLiteral("summary")).toString().contains(QChar(0xb6)));
        const QByteArray marked = RichTextCodec::encode(tracked);
        QVERIFY(tracked.acceptChange(deletions.first()));
        QCOMPARE(texts(tracked), texts(plain));
        QVERIFY(tracked.charFormat(tracked.paragraphsInOrder().first()->markFormat).deletionId.isEmpty());
        QVERIFY(tracked.undo());
        QCOMPARE(RichTextCodec::encode(tracked), marked);
        QVERIFY(tracked.rejectChange(deletions.first()));
        QCOMPARE(texts(tracked), (QStringList{QStringLiteral("Alpha beta"), QStringLiteral("Gamma delta"),
                                              QStringLiteral("Epsilon")}));
        QVERIFY(tracked.annotations().changes.isEmpty());

        // A tracked Enter is an inserted break; typing on joins the same change.
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Hello world"));
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.setTrackChanges(true);
        const QUuid id = doc.paragraphsInOrder().first()->id;
        QVERIFY(doc.insertText({id, 5}, QStringLiteral("\nNew")));
        QCOMPARE(texts(doc), (QStringList{QStringLiteral("Hello"), QStringLiteral("New world")}));
        const QStringList insertions = changesOf(doc, ChangeKind::Insertion);
        QCOMPARE(insertions.size(), 1);
        QCOMPARE(doc.charFormat(doc.paragraphsInOrder().first()->markFormat).insertionId, insertions.first());
        const QByteArray split = RichTextCodec::encode(doc);
        QVERIFY(doc.rejectChange(insertions.first()));
        QCOMPARE(texts(doc), QStringList{QStringLiteral("Hello world")});
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), split);
        QVERIFY(doc.acceptChange(insertions.first()));
        QCOMPARE(texts(doc), (QStringList{QStringLiteral("Hello"), QStringLiteral("New world")}));
        QVERIFY(doc.charFormat(doc.paragraphsInOrder().first()->markFormat).insertionId.isEmpty());
        // Deleting one's own inserted break joins the paragraphs outright.
        doc.setTrackChanges(false);
        QVERIFY(doc.undo());
        doc.setTrackChanges(true);
        const auto &flat = doc.paragraphsInOrder();
        QVERIFY(doc.deleteRange({flat.at(0)->id, 5}, {flat.at(1)->id, 0}));
        QCOMPARE(texts(doc), QStringList{QStringLiteral("HelloNew world")});
    }

    void formattingIsTracked()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Plain text here"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QByteArray before = RichTextCodec::encode(doc);
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.setTrackChanges(true);
        CharFormatPatch bold;
        bold.weight = 700;
        QVERIFY(doc.applyCharFormat({p->id, 0}, {p->id, 5}, bold));
        ParagraphFormatPatch centre;
        centre.alignment = Alignment::Center;
        QVERIFY(doc.setParagraphFormat({p->id, 0}, {p->id, 0}, centre));
        const QStringList formats = changesOf(doc, ChangeKind::Format);
        QCOMPARE(formats.size(), 2);
        QCOMPARE(formatAt(doc, p, 0).weight, 700);
        const QString characters = formatAt(doc, p, 0).formatChangeId;
        QVERIFY(formats.contains(characters));
        QVERIFY(formatAt(doc, p, 6).formatChangeId.isEmpty());
        const QVariantMap change = item(doc, characters);
        QCOMPARE(change.value(QStringLiteral("changeKind")).toString(), QStringLiteral("format"));
        QCOMPARE(change.value(QStringLiteral("summary")).toString(), QStringLiteral("Formatted “Plain”"));
        QVERIFY(doc.reviewContextAt(sid(p->id), 1).contains(QStringLiteral("Formatted by Ada")));
        const QString paragraph = formats.at(formats.at(0) == characters ? 1 : 0);
        QCOMPARE(item(doc, paragraph).value(QStringLiteral("location")).toMap().value(QStringLiteral("paragraphId")).toString(),
                 sid(p->id));
        // Former looks persist.
        const QByteArray tracked = RichTextCodec::encode(doc);
        RichTextDocument reopened;
        QVERIFY(reopened.loadJson(QString::fromUtf8(tracked)));
        QCOMPARE(RichTextCodec::encode(reopened), tracked);

        QVERIFY(doc.acceptChange(characters));
        QCOMPARE(formatAt(doc, p, 0).weight, 700);
        QVERIFY(formatAt(doc, p, 0).formatChangeId.isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), tracked);
        // Rejecting both restores the document exactly.
        QVERIFY(doc.rejectChange(characters));
        QVERIFY(doc.rejectChange(paragraph));
        QCOMPARE(RichTextCodec::encode(doc), before);
        QVERIFY(doc.undo());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), tracked);
    }

    void tableRowDeletionIsTracked()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Intro"));
        QVERIFY(doc.insertTable(doc.endPosition(), 2, 2));
        const Table *table = nullptr;
        for (const Block &block : doc.blocks())
            if (block.table) table = block.table.get();
        QVERIFY(table);
        const QString tableId = sid(table->id);
        const QString rowId = sid(table->rows.at(1).id);
        QVERIFY(doc.setTableCellText(tableId, sid(table->rows.at(1).cells.at(0).id), QStringLiteral("a")));
        QVERIFY(doc.setTableCellText(tableId, sid(table->rows.at(1).cells.at(1).id), QStringLiteral("b")));
        doc.setTrackChanges(true);
        doc.setReviewAuthor(QStringLiteral("Bob"));
        const Paragraph *cell = table->rows.at(1).cells.at(0).paragraphs.first().get();
        QVERIFY(doc.insertText({cell->id, 1}, QStringLiteral("X")));
        const QString bob = formatAt(doc, cell, 1).insertionId;
        doc.setReviewAuthor(QStringLiteral("Ada"));
        QVERIFY(doc.removeTableRow(tableId, rowId));
        QCOMPARE(table->rows.size(), 2);
        const QString ada = formatAt(doc, cell, 0).deletionId;
        QVERIFY(!ada.isEmpty());
        QVERIFY(item(doc, ada).value(QStringLiteral("summary")).toString().startsWith(QStringLiteral("Deleted table row")));
        // Structure edits that tracking cannot record are refused, typed.
        QVERIFY(!doc.insertTableRow(tableId, 0));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(!doc.mergeTableCells(tableId, {}));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));

        const QByteArray marked = RichTextCodec::encode(doc);
        RichTextDocument reopened;
        QVERIFY(reopened.loadJson(QString::fromUtf8(marked)));
        QCOMPARE(RichTextCodec::encode(reopened), marked);
        QVERIFY(doc.acceptChange(ada));
        QCOMPARE(doc.table(QUuid(tableId))->rows.size(), 1);
        // Bob's insertion went with the row: nothing is left to review.
        QVERIFY(!doc.annotations().changes.contains(bob));
        QVERIFY(doc.annotations().changes.isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), marked);
        QVERIFY(doc.rejectChange(ada));
        QCOMPARE(doc.table(QUuid(tableId))->rows.size(), 2);
        QVERIFY(formatAt(doc, cell, 0).deletionId.isEmpty());
        QVERIFY(doc.annotations().changes.contains(bob));
    }

    void equationAndReferenceEditsAreTracked()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("See "));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString equation = doc.insertEquation(sid(p->id), 4, QStringLiteral("x^2"));
        const QString wu = doc.addSource({{QStringLiteral("author"), QStringLiteral("Wu")}});
        const QString kim = doc.addSource({{QStringLiteral("author"), QStringLiteral("Kim")}});
        const QString citation = doc.insertReference(sid(p->id), 0, QStringLiteral("citation"), wu);
        const QString caption = doc.insertReference(sid(p->id), 0, QStringLiteral("caption"), QStringLiteral("sequence:Figure"));
        const QByteArray before = RichTextCodec::encode(doc);
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.setTrackChanges(true);

        // The equation keeps its ID with the new source; the old one is a deleted copy.
        QVERIFY(doc.setEquationSource(equation, QStringLiteral("\\frac{a}{b}")));
        QCOMPARE(doc.equationSnapshot(equation).value(QStringLiteral("source")).toString(), QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(doc.equationSnapshots().size(), 2);
        QCOMPARE(doc.reviewItems().size(), 2);
        // A retargeted citation is a deleted field and an inserted one.
        QVERIFY(doc.editReference(citation, {{QStringLiteral("targetId"), kim}}));
        QCOMPARE(doc.referenceSnapshots().size(), 3);
        // A caption's series cannot be tracked (references follow its ID); its label is not text.
        QVERIFY(!doc.editReference(caption, {{QStringLiteral("targetId"), QStringLiteral("sequence:Table")}}));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(doc.editReference(caption, {{QStringLiteral("label"), QStringLiteral("Results")}}));
        doc.setTrackChanges(false);
        QVERIFY(doc.editReference(caption, {{QStringLiteral("label"), QString()}}));

        const QByteArray tracked = RichTextCodec::encode(doc);
        QVERIFY(resolveAll(doc, true));
        QCOMPARE(doc.equationSnapshots().size(), 1);
        QCOMPARE(doc.equationSnapshots().first().toMap().value(QStringLiteral("id")).toString(), equation);
        QCOMPARE(doc.equationSnapshots().first().toMap().value(QStringLiteral("source")).toString(), QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(doc.referenceSnapshots().size(), 2);
        bool cited = false;
        for (const QVariant &value : doc.referenceSnapshots())
            cited = cited || value.toMap().value(QStringLiteral("targetId")).toString() == kim;
        QVERIFY(cited);
        while (RichTextCodec::encode(doc) != tracked) QVERIFY(doc.undo());
        QVERIFY(resolveAll(doc, false));
        QCOMPARE(texts(doc), QStringList{QString(QStringLiteral("Figure 1[1]See ")) + QChar(ObjectChar)});
        QCOMPARE(doc.equationSnapshots().first().toMap().value(QStringLiteral("source")).toString(), QStringLiteral("x^2"));
        QCOMPARE(reference(doc, citation).value(QStringLiteral("targetId")).toString(), wu);
        Q_UNUSED(before);
    }

    void trackedMovesKeepIdentity()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral(": the site\nBody text\nSee "));
        const auto &flat = doc.paragraphsInOrder();
        const QUuid p0 = flat.at(0)->id, p1 = flat.at(1)->id, p2 = flat.at(2)->id;
        const QString caption = doc.insertReference(sid(p0), 0, QStringLiteral("caption"), QStringLiteral("sequence:Figure"));
        const QString xref = doc.insertReference(sid(p2), 4, QStringLiteral("cross-reference"), caption);
        const QString thread = doc.addComment(sid(p1), 0, sid(p1), 4, QStringLiteral("Keep?"));
        QCOMPARE(reference(doc, xref).value(QStringLiteral("formattedText")).toString(), QStringLiteral("Figure 1"));
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.setTrackChanges(true);

        // Cut the caption number and paste it at the end: it moves, tracked.
        const Fragment cut = doc.extractFragment({p0, 0}, {p0, 8});
        QVERIFY(doc.deleteRange({p0, 0}, {p0, 8}));
        QVERIFY(doc.insertFragment(doc.paragraphEnd(p2), cut));
        QCOMPARE(reference(doc, caption).value(QStringLiteral("location")).toMap().value(QStringLiteral("paragraphId")).toString(),
                 sid(p2));
        QCOMPARE(reference(doc, xref).value(QStringLiteral("status")).toString(), QStringLiteral("resolved"));
        // The commented word moves too.
        const Fragment word = doc.extractFragment({p1, 0}, {p1, 4});
        QVERIFY(doc.deleteRange({p1, 0}, {p1, 4}));
        QVERIFY(doc.insertFragment(doc.paragraphEnd(p1), word));
        QCOMPARE(item(doc, thread).value(QStringLiteral("location")).toMap().value(QStringLiteral("offset")).toInt(), 9);

        const QByteArray moved = RichTextCodec::encode(doc);
        // Accepting the deletions leaves the moved caption and comment anchored.
        for (const QString &id : changesOf(doc, ChangeKind::Deletion)) QVERIFY(doc.acceptChange(id));
        QCOMPARE(reference(doc, xref).value(QStringLiteral("formattedText")).toString(), QStringLiteral("Figure 1"));
        QCOMPARE(reference(doc, xref).value(QStringLiteral("status")).toString(), QStringLiteral("resolved"));
        QCOMPARE(item(doc, thread).value(QStringLiteral("status")).toString(), QStringLiteral("open"));
        QCOMPARE(item(doc, thread).value(QStringLiteral("quote")).toString(), QStringLiteral("Body"));
        // Rejecting the whole move puts both back where they were.
        while (RichTextCodec::encode(doc) != moved) QVERIFY(doc.undo());
        QVERIFY(resolveAll(doc, false));
        QCOMPARE(reference(doc, caption).value(QStringLiteral("location")).toMap().value(QStringLiteral("paragraphId")).toString(),
                 sid(p0));
        QCOMPARE(item(doc, thread).value(QStringLiteral("location")).toMap().value(QStringLiteral("offset")).toInt(), 0);
        QCOMPARE(reference(doc, xref).value(QStringLiteral("formattedText")).toString(), QStringLiteral("Figure 1"));
    }

    void reviewItemsAreNavigable()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        RichTextDocument &doc = scene.document;
        doc.insertText(doc.startPosition(), QStringLiteral("one two three"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString first = doc.addComment(sid(p->id), 0, sid(p->id), 3, QStringLiteral("A"));
        const QString last = doc.addComment(sid(p->id), 8, sid(p->id), 13, QStringLiteral("B"));
        doc.setTrackChanges(true);
        QVERIFY(doc.insertText({p->id, 4}, QStringLiteral("X")));
        doc.setTrackChanges(false);
        const QString change = changesOf(doc, ChangeKind::Insertion).first();
        const QVariantMap location = doc.reviewItemLocation(last);
        QCOMPARE(location.value(QStringLiteral("offset")).toInt(), 9);
        QCOMPARE(location.value(QStringLiteral("endOffset")).toInt(), 14);
        QCOMPARE(doc.adjacentReviewItem(sid(p->id), 0, true), change);
        QCOMPARE(doc.adjacentReviewItem(sid(p->id), 4, true), last);
        QCOMPARE(doc.adjacentReviewItem(sid(p->id), 9, true), first); // wraps
        QCOMPARE(doc.adjacentReviewItem(sid(p->id), 4, false), first);
        QCOMPARE(doc.adjacentReviewItem(sid(p->id), 0, false), last);  // wraps
        QCOMPARE(doc.adjacentReviewItem(sid(p->id), 0, true, QStringLiteral("comment")), last);

        // The editing surface selects the anchored range.
        QVERIFY(scene.area->goToReviewItem(last));
        QVariantMap selection = scene.area->selectionRange();
        QCOMPARE(selection.value(QStringLiteral("anchorOffset")).toInt(), 9);
        QCOMPARE(selection.value(QStringLiteral("focusOffset")).toInt(), 14);
        QCOMPARE(scene.area->goToAdjacentReviewItem(true), first);
        selection = scene.area->selectionRange();
        QCOMPARE(selection.value(QStringLiteral("anchorOffset")).toInt(), 0);
        QCOMPARE(selection.value(QStringLiteral("focusOffset")).toInt(), 3);
        QCOMPARE(scene.area->goToAdjacentReviewItem(true), change);
        QCOMPARE(scene.area->selectionRange().value(QStringLiteral("anchorOffset")).toInt(), 4);
        QCOMPARE(scene.area->goToAdjacentReviewItem(false), first);
        QVERIFY(!scene.area->goToReviewItem(QStringLiteral("missing")));
    }
};

QTEST_MAIN(RichTextReviewTrackingTest)
#include "tst_rich_text_review_tracking.moc"
