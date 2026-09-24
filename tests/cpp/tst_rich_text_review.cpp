// SPDX-License-Identifier: LGPL-3.0-or-later
// Document review (WP-052, D-116): comment ranges that follow edits and
// overlap, orphaned comments that recover through undo or a paste, replies
// and resolution, tracked insertions and deletions with accept/reject undo
// that keep other reviewers' content, persistence, and the editing surface.
#include "rich_text_codec.h"
#include "rich_text_scene.h"
#include "rich_text_spans.h"
#include <QJsonDocument>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextScene;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QVariantMap item(const RichTextDocument &doc, const QString &id)
{
    for (const QVariant &value : doc.reviewItems())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

std::pair<int, int> range(const RichTextDocument &doc, const QString &id)
{
    const QVariantMap location = item(doc, id).value(QStringLiteral("location")).toMap();
    return {location.value(QStringLiteral("offset"), -1).toInt(), location.value(QStringLiteral("endOffset"), -1).toInt()};
}

const CharFormat &formatAt(const RichTextDocument &doc, const Paragraph *p, int offset)
{
    return doc.charFormat(Spans::formatAt(p->spans, offset));
}

QStringList changeIds(const RichTextDocument &doc)
{
    QStringList ids = doc.annotations().changes.keys();
    ids.sort();
    return ids;
}

} // namespace

class RichTextReviewTest : public QObject {
    Q_OBJECT
private slots:
    void commentsFollowEditsAndOverlap()
    {
        RichTextDocument doc;
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.insertText(doc.startPosition(), QStringLiteral("The quick brown fox"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString first = doc.addComment(sid(p->id), 4, sid(p->id), 15, QStringLiteral("Which fox?"));
        QVERIFY(!first.isEmpty());
        QVariantMap comment = item(doc, first);
        QCOMPARE(comment.value(QStringLiteral("kind")).toString(), QStringLiteral("comment"));
        QCOMPARE(comment.value(QStringLiteral("author")).toString(), QStringLiteral("Ada"));
        QCOMPARE(comment.value(QStringLiteral("status")).toString(), QStringLiteral("open"));
        QCOMPARE(comment.value(QStringLiteral("threadId")).toString(), first);
        QCOMPARE(comment.value(QStringLiteral("quote")).toString(), QStringLiteral("quick brown"));
        QVERIFY(!comment.value(QStringLiteral("time")).toString().isEmpty());
        QCOMPARE(range(doc, first), std::make_pair(4, 15));

        // Text typed before the range moves it; text typed inside extends it.
        doc.insertText({p->id, 0}, QStringLiteral("Look: "));
        QCOMPARE(range(doc, first), std::make_pair(10, 21));
        doc.insertText({p->id, 15}, QStringLiteral("!!"));
        QCOMPARE(p->text, QStringLiteral("Look: The quick!! brown fox"));
        QCOMPARE(range(doc, first), std::make_pair(10, 23));
        // Typing at the edge does not.
        doc.insertText({p->id, 10}, QStringLiteral("very "));
        QCOMPARE(range(doc, first), std::make_pair(15, 28));
        QVERIFY(!formatAt(doc, p, 10).commentIds.contains(first));

        // A second comment overlapping the first.
        const QString second = doc.addComment(sid(p->id), 23, sid(p->id), 32, QStringLiteral("Colour?"));
        QCOMPARE(item(doc, second).value(QStringLiteral("quote")).toString(), QStringLiteral("brown fox"));
        QStringList both{first, second};
        both.sort();
        QCOMPARE(formatAt(doc, p, 25).commentIds, both);
        QCOMPARE(doc.reviewItems().size(), 2);
        const QByteArray before = RichTextCodec::encode(doc);
        // Deleting the shared word shrinks both ranges.
        QVERIFY(doc.deleteRange({p->id, 23}, {p->id, 28}));
        QCOMPARE(p->text, QStringLiteral("Look: The very quick!!  fox"));
        QCOMPARE(range(doc, first), std::make_pair(15, 23));
        QCOMPARE(range(doc, second), std::make_pair(23, 27));
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        // Refusals: no text, no body.
        QVERIFY(doc.addComment(sid(p->id), 3, sid(p->id), 3, QStringLiteral("Empty")).isEmpty());
        QVERIFY(doc.addComment(sid(p->id), 0, sid(p->id), 3, QStringLiteral("  ")).isEmpty());
        QCOMPARE(RichTextCodec::encode(doc), before);
    }

    void orphanedCommentsRecoverTheirAnchor()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Keep this sentence."));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString thread = doc.addComment(sid(p->id), 5, sid(p->id), 9, QStringLiteral("Why?"));
        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(doc.deleteRange({p->id, 5}, {p->id, 9}));
        QVariantMap orphan = item(doc, thread);
        QCOMPARE(orphan.value(QStringLiteral("status")).toString(), QStringLiteral("orphaned"));
        QVERIFY(!orphan.value(QStringLiteral("location")).isValid());
        QVERIFY(!orphan.value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        QCOMPARE(range(doc, thread), std::make_pair(5, 9));

        // Cut and paste takes the comment along; a copy does not duplicate it.
        const Fragment cut = doc.extractFragment({p->id, 5}, {p->id, 9});
        QVERIFY(doc.deleteRange({p->id, 5}, {p->id, 9}));
        QVERIFY(doc.insertFragment({p->id, 0}, cut));
        QCOMPARE(p->text, QStringLiteral("thisKeep  sentence."));
        QCOMPARE(item(doc, thread).value(QStringLiteral("status")).toString(), QStringLiteral("open"));
        QCOMPARE(range(doc, thread), std::make_pair(0, 4));
        const Fragment copy = doc.extractFragment({p->id, 0}, {p->id, 4});
        QVERIFY(doc.insertFragment({p->id, int(p->text.size())}, copy));
        QCOMPARE(range(doc, thread), std::make_pair(0, 4));
        QVERIFY(formatAt(doc, p, int(p->text.size()) - 1).commentIds.isEmpty());
    }

    void repliesResolveAndRemove()
    {
        RichTextDocument doc;
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.insertText(doc.startPosition(), QStringLiteral("Draft text"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString thread = doc.addComment(sid(p->id), 0, sid(p->id), 5, QStringLiteral("Rename?"));
        doc.setReviewAuthor(QStringLiteral("Bob"));
        QVERIFY(doc.replyToComment(thread, QStringLiteral("Agreed")));
        const QVariantList replies = item(doc, thread).value(QStringLiteral("replies")).toList();
        QCOMPARE(replies.size(), 1);
        QCOMPARE(replies.first().toMap().value(QStringLiteral("author")).toString(), QStringLiteral("Bob"));
        QCOMPARE(replies.first().toMap().value(QStringLiteral("body")).toString(), QStringLiteral("Agreed"));
        QVERIFY(!doc.replyToComment(thread, QStringLiteral(" ")));
        QVERIFY(doc.resolveComment(thread));
        QCOMPARE(item(doc, thread).value(QStringLiteral("status")).toString(), QStringLiteral("resolved"));
        QVERIFY(doc.undo());
        QCOMPARE(item(doc, thread).value(QStringLiteral("status")).toString(), QStringLiteral("open"));
        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(doc.removeComment(thread));
        QVERIFY(doc.reviewItems().isEmpty());
        QVERIFY(formatAt(doc, p, 0).commentIds.isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        QVERIFY(!doc.resolveComment(QStringLiteral("missing")));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("invalidArgument"));
    }

    void trackedChangesAcceptAndRejectWithUndo()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Hello world"));
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.setTrackChanges(true);
        const Paragraph *p = doc.paragraphsInOrder().first();
        doc.insertText({p->id, 5}, QStringLiteral(" big"));
        doc.insertText({p->id, 9}, QStringLiteral("!"));
        QCOMPARE(p->text, QStringLiteral("Hello big! world"));
        QCOMPARE(doc.annotations().changes.size(), 1); // typing continues one insertion
        const QString insertion = formatAt(doc, p, 5).insertionId;
        QVERIFY(!insertion.isEmpty());
        QCOMPARE(formatAt(doc, p, 9).insertionId, insertion);
        QVERIFY(formatAt(doc, p, 4).insertionId.isEmpty());
        QVariantMap change = item(doc, insertion);
        QCOMPARE(change.value(QStringLiteral("kind")).toString(), QStringLiteral("change"));
        QCOMPARE(change.value(QStringLiteral("changeKind")).toString(), QStringLiteral("insertion"));
        QCOMPARE(change.value(QStringLiteral("summary")).toString(), QStringLiteral("Inserted “big!”"));
        QCOMPARE(change.value(QStringLiteral("author")).toString(), QStringLiteral("Ada"));

        // A tracked deletion keeps the text, marked.
        QVERIFY(doc.deleteRange({p->id, 11}, {p->id, 16}));
        QCOMPARE(p->text, QStringLiteral("Hello big! world"));
        const QString deletion = formatAt(doc, p, 11).deletionId;
        QVERIFY(!deletion.isEmpty());
        QCOMPARE(formatAt(doc, p, 15).deletionId, deletion);
        QCOMPARE(item(doc, deletion).value(QStringLiteral("summary")).toString(), QStringLiteral("Deleted “world”"));
        // Deleting one's own insertion removes it for real.
        QVERIFY(doc.deleteRange({p->id, 9}, {p->id, 10}));
        QCOMPARE(p->text, QStringLiteral("Hello big world"));
        QStringList both{deletion, insertion};
        both.sort();
        QCOMPARE(changeIds(doc), both);

        const QByteArray reviewed = RichTextCodec::encode(doc);
        QVERIFY(doc.acceptChange(insertion));
        QCOMPARE(p->text, QStringLiteral("Hello big world"));
        QVERIFY(formatAt(doc, p, 5).insertionId.isEmpty());
        QCOMPARE(changeIds(doc), QStringList{deletion});
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), reviewed);

        QVERIFY(doc.rejectChange(deletion));
        QCOMPARE(p->text, QStringLiteral("Hello big world"));
        QVERIFY(formatAt(doc, p, 11).deletionId.isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), reviewed);

        QVERIFY(doc.acceptChange(deletion));
        QCOMPARE(p->text, QStringLiteral("Hello big "));
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), reviewed);

        QVERIFY(doc.rejectChange(insertion));
        QCOMPARE(p->text, QStringLiteral("Hello world"));
        QCOMPARE(changeIds(doc), QStringList{deletion});
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), reviewed);
        QVERIFY(!doc.acceptChange(QStringLiteral("missing")));
    }

    void rejectingOneChangeKeepsOtherReviewersContent()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Alpha beta gamma"));
        doc.setTrackChanges(true);
        const Paragraph *p = doc.paragraphsInOrder().first();
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.insertText({p->id, 5}, QStringLiteral(" one"));
        const QString ada = formatAt(doc, p, 6).insertionId;
        doc.setReviewAuthor(QStringLiteral("Bob"));
        doc.insertText({p->id, 20}, QStringLiteral(" two"));
        QCOMPARE(p->text, QStringLiteral("Alpha one beta gamma two"));
        const QString bobInsertion = formatAt(doc, p, 21).insertionId;
        // Bob deletes Ada's word: marked, not removed, since it is not his.
        QVERIFY(doc.deleteRange({p->id, 6}, {p->id, 9}));
        QCOMPARE(p->text, QStringLiteral("Alpha one beta gamma two"));
        const QString bobDeletion = formatAt(doc, p, 7).deletionId;
        QCOMPARE(formatAt(doc, p, 7).insertionId, ada);
        QCOMPARE(doc.annotations().changes.size(), 3);
        QCOMPARE(doc.annotations().changes.value(bobDeletion).author, QStringLiteral("Bob"));

        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(doc.rejectChange(ada));
        QCOMPARE(p->text, QStringLiteral("Alpha beta gamma two"));
        // Bob's deletion lay inside the removed text; his insertion stays.
        QCOMPARE(changeIds(doc), QStringList{bobInsertion});
        QCOMPARE(formatAt(doc, p, 17).insertionId, bobInsertion);
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        QCOMPARE(doc.annotations().changes.size(), 3);
    }

    void reviewPersists()
    {
        RichTextDocument doc;
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.insertText(doc.startPosition(), QStringLiteral("Shared draft"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString thread = doc.addComment(sid(p->id), 0, sid(p->id), 6, QStringLiteral("Title?"));
        QVERIFY(doc.replyToComment(thread, QStringLiteral("Yes")));
        doc.setTrackChanges(true);
        doc.insertText({p->id, 12}, QStringLiteral(" v2"));
        QVERIFY(doc.deleteRange({p->id, 7}, {p->id, 12}));
        const QByteArray json = RichTextCodec::encode(doc);
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
        QVERIFY(root.contains(QStringLiteral("comments")));
        QVERIFY(root.contains(QStringLiteral("changes")));
        RichTextDocument reopened;
        QVERIFY(reopened.loadJson(QString::fromUtf8(json)));
        QCOMPARE(RichTextCodec::encode(reopened), json);
        QCOMPARE(reopened.reviewItems(), doc.reviewItems());
        QCOMPARE(reopened.reviewItems().size(), 3);
    }

    void editingSurfaceTracksAndRequests()
    {
        Scene scene;
        QVERIFY(scene.create(false));
        RichTextDocument &doc = scene.document;
        doc.insertText(doc.startPosition(), QStringLiteral("abc"));
        const Paragraph *p = doc.paragraphsInOrder().first();
        doc.setReviewAuthor(QStringLiteral("Ada"));
        doc.setTrackChanges(true);
        QVERIFY(scene.area->goToParagraph(sid(p->id), 0));
        // Delete marks and moves on; one change for the gesture.
        QTest::keyClick(scene.window.get(), Qt::Key_Delete);
        QTest::keyClick(scene.window.get(), Qt::Key_Delete);
        QCOMPARE(p->text, QStringLiteral("abc"));
        QCOMPARE(scene.area->caretOffset(), 2);
        QCOMPARE(doc.annotations().changes.size(), 1);
        QVERIFY(!formatAt(doc, p, 1).deletionId.isEmpty());
        // Backspace over deleted text only moves the caret.
        QTest::keyClick(scene.window.get(), Qt::Key_Backspace);
        QCOMPARE(scene.area->caretOffset(), 1);
        QVERIFY(scene.area->caretContext().contains(QStringLiteral("Deleted by Ada")));

        // Requests at the selection.
        doc.setTrackChanges(false);
        QVERIFY(scene.area->selectRange(sid(p->id), 2, sid(p->id), 3));
        const QVariantMap selection = scene.area->selectionRange();
        QCOMPARE(selection.value(QStringLiteral("anchorOffset")).toInt(), 2);
        QCOMPARE(selection.value(QStringLiteral("focusOffset")).toInt(), 3);
        const QString thread = scene.area->addComment(QStringLiteral("Check"));
        QVERIFY(!thread.isEmpty());
        QCOMPARE(range(doc, thread), std::make_pair(2, 3));
        QVERIFY(scene.area->goToParagraph(sid(p->id), 3));
        const QString figure = scene.area->insertReference(QStringLiteral("caption"), QStringLiteral("Figure"));
        QVERIFY(!figure.isEmpty());
        QCOMPARE(p->text, QStringLiteral("abcFigure 1"));
        QCOMPARE(scene.area->caretOffset(), 11);
        const QString equation = scene.area->insertEquation(QStringLiteral("x^2"));
        QVERIFY(!equation.isEmpty());
        QCOMPARE(scene.area->caretOffset(), 12);
        QVERIFY(scene.area->insertEquation(QStringLiteral("\\over")).isEmpty());
        QCOMPARE(p->text.size(), 12);
    }
};

QTEST_MAIN(RichTextReviewTest)
#include "tst_rich_text_review.moc"
