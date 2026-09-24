// SPDX-License-Identifier: LGPL-3.0-or-later
// Linked text frames and page columns (WP-051): a story threaded through
// frames of different widths, overset text, resize with undo, thread
// validation, persistence, page columns and frame navigation in the view.
#include "document_session.h"
#include "rich_text_codec.h"
#include "rich_text_layout.h"
#include "rich_text_scene.h"
#include "rich_text_session_adapter.h"
#include <QJsonArray>
#include <QJsonDocument>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;
using namespace QindaTK::RichTextScene;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QString words(int count)
{
    QStringList list;
    for (int i = 0; i < count; ++i) list.append(QStringLiteral("word%1").arg(i % 10));
    return list.join(u' ');
}

// A 120-word paragraph and `lines` short lines, all with 18 pt line boxes.
void buildStory(RichTextDocument &doc, int lines)
{
    doc.setPageSetup(fixturePage());
    QStringList text{words(120)};
    for (int i = 0; i < lines; ++i) text.append(mixedLabel(i));
    doc.insertText(doc.startPosition(), text.join(u'\n'));
    fixLineBoxes(doc);
}

TextFrame frame(const QString &id, int page, double x, double y, double width, double height, const QString &next = {})
{
    TextFrame f;
    f.id = id;
    f.page = page;
    f.xPt = x;
    f.yPt = y;
    f.widthPt = width;
    f.heightPt = height;
    f.next = next;
    return f;
}

// A (page 0, 300 x 120 pt) -> B (page 0, 150 x 120 pt) -> C (page 1, 300 x 120 pt):
// six 24 px lines each, B half as wide.
FrameSet threeFrames()
{
    FrameSet set;
    set.frames = {frame(QStringLiteral("A"), 0, 36, 36, 300, 120, QStringLiteral("B")),
                  frame(QStringLiteral("B"), 0, 36, 216, 150, 120, QStringLiteral("C")),
                  frame(QStringLiteral("C"), 1, 36, 36, 300, 120)};
    set.storyStart = QStringLiteral("A");
    return set;
}

// Text shown per paragraph must tile it from its start in page order, and
// the shown paragraphs must be a prefix of the story. Returns "" or the
// first mismatch; `shown` receives each paragraph's shown length.
QString coverage(RichTextLayout &layout, const RichTextDocument &doc, QHash<QUuid, int> *shown)
{
    shown->clear();
    for (int page = 0; page < layout.pageCount(); ++page)
        for (const PageLine &line : layout.pageLines(page)) {
            const LineBox &box = line.metrics->lines.at(line.line);
            const int from = layout.modelOffset(*line.metrics, box.start);
            if (from != shown->value(line.paragraph->id, 0))
                return QStringLiteral("page %1: '%2' from %3").arg(page).arg(line.paragraph->text.left(12)).arg(from);
            shown->insert(line.paragraph->id, layout.modelOffset(*line.metrics, box.start + box.length));
        }
    bool whole = true;
    for (const Paragraph *p : doc.paragraphsInOrder()) {
        const int length = shown->value(p->id, 0);
        if (!whole && length > 0) return QStringLiteral("'%1' shown after overset text").arg(p->text.left(12));
        whole = whole && length == p->text.size();
    }
    return {};
}

QStringList pageSummary(RichTextLayout &layout)
{
    QStringList pages;
    for (int i = 0; i < layout.pageCount(); ++i) {
        const Page &page = layout.page(i);
        QString line = sid(page.key) + u' ' + sid(page.first.paragraph) + u':' + QString::number(page.first.offset);
        for (const PageItem &item : page.items)
            line += QStringLiteral(" %1/%2+%3@%4").arg(int(item.kind)).arg(item.first).arg(item.count).arg(item.frameId);
        pages.append(line);
    }
    return pages;
}

} // namespace

class RichTextFramesTest : public QObject {
    Q_OBJECT
private slots:
    void storyFlowsThroughFramesOfDifferentWidths()
    {
        RichTextDocument doc;
        buildStory(doc, 8);
        QVERIFY(doc.setFrameSet(threeFrames(), QStringLiteral("Frames")));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QVERIFY(layout.frameMode());
        QCOMPARE(layout.pageCount(), 2);
        QCOMPARE(layout.contentRect(), QRectF(QPointF(0, 0), layout.pageSize()));
        QHash<QUuid, int> shown;
        QCOMPARE(coverage(layout, doc, &shown), QString());
        // The long paragraph fills A, then continues in the narrower B at B's
        // width with the line breaks it had in A.
        const Paragraph *story = doc.paragraphsInOrder().first();
        QVector<PageItem> items;
        for (const PageItem &item : layout.page(0).items)
            if (item.block == story->id) items.append(item);
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).frameId, QStringLiteral("A"));
        QCOMPARE(items.at(0).first, 0);
        QCOMPARE(items.at(0).count, 6);
        QCOMPARE(items.at(0).x, 36 * PxPerPt);
        QCOMPARE(items.at(0).width, 300 * PxPerPt);
        QCOMPARE(items.at(1).frameId, QStringLiteral("B"));
        QCOMPARE(items.at(1).first, 6);
        QCOMPARE(items.at(1).y, 216 * PxPerPt);
        QCOMPARE(items.at(1).width, 150 * PxPerPt);
        QCOMPARE(items.at(1).widths.size(), 2);
        QCOMPARE(items.at(1).widths.at(0).width, 300 * PxPerPt);
        QCOMPARE(items.at(1).widths.at(1).line, 6);
        QCOMPARE(items.at(1).widths.at(1).width, 150 * PxPerPt);
        // What no frame holds is overset, counted exactly.
        QVERIFY(layout.overset());
        int expected = 0;
        for (const Paragraph *p : doc.paragraphsInOrder()) expected += int(p->text.size()) - shown.value(p->id, 0);
        QVERIFY(expected > 0);
        QCOMPARE(layout.oversetCharacters(), expected);
        // A caret late in the continued paragraph maps to its page and back.
        const Position late{story->id, shown.value(story->id) - 2};
        const CaretBox caret = layout.caretBox(late);
        QVERIFY(caret.page >= 0);
        const Position hit = layout.hitTest(caret.page, caret.rect.center());
        QCOMPARE(layout.caretBox(hit).page, caret.page);
        QVERIFY(qAbs(layout.caretBox(hit).rect.x() - caret.rect.x()) < 0.5);
    }

    void resizingAFrameReflowsWithUndo()
    {
        RichTextDocument doc;
        buildStory(doc, 8);
        QVERIFY(doc.setFrameSet(threeFrames(), QStringLiteral("Frames")));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        const int overset = layout.oversetCharacters();
        QVERIFY(overset > 0);
        const QByteArray before = RichTextCodec::encode(doc);
        // C grows to 360 pt (twenty lines): the whole story fits.
        QVERIFY(doc.setTextFrameGeometry(QStringLiteral("C"), {{QStringLiteral("heightMm"), 360 * 25.4 / 72}}));
        QCOMPARE(doc.undoText(), QStringLiteral("Resize text frame"));
        layout.paginateAll();
        QVERIFY(!layout.overset());
        QCOMPARE(layout.oversetCharacters(), 0);
        QHash<QUuid, int> shown;
        QCOMPARE(coverage(layout, doc, &shown), QString());
        for (const Paragraph *p : doc.paragraphsInOrder()) QCOMPARE(shown.value(p->id, -1), int(p->text.size()));
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        layout.paginateAll();
        QCOMPARE(layout.oversetCharacters(), overset);
        QCOMPARE(coverage(layout, doc, &shown), QString());
        // With two columns in C the story continues in C's second column.
        QVERIFY(doc.setTextFrameLayout(QStringLiteral("C"), {{QStringLiteral("columns"), 2}, {QStringLiteral("gutterMm"), 4}}));
        layout.paginateAll();
        QCOMPARE(coverage(layout, doc, &shown), QString());
        const QVector<Region> columns = layout.regions(1);
        QCOMPARE(columns.size(), 2);
        bool second = false;
        for (const PageItem &item : layout.page(1).items)
            second = second || (item.frameId == QStringLiteral("C") && qAbs(item.x - columns.at(1).rect.x()) < 0.01);
        QVERIFY(second);
    }

    void threadEditsAndValidation()
    {
        RichTextDocument doc;
        buildStory(doc, 4);
        auto geometry = [](double x, double y, double w, double h) {
            return QVariantMap{{QStringLiteral("xMm"), x}, {QStringLiteral("yMm"), y},
                               {QStringLiteral("widthMm"), w}, {QStringLiteral("heightMm"), h}};
        };
        const QString a = doc.createTextFrame(0, geometry(10, 10, 100, 40));
        QVERIFY(!a.isEmpty());
        QCOMPARE(doc.storyFrameId(), a);
        const QString b = doc.createTextFrame(0, geometry(10, 60, 60, 40));
        const QString c = doc.createTextFrame(1, geometry(10, 10, 100, 100));
        QVERIFY(doc.connectTextFrames(a, b));
        QVERIFY(doc.connectTextFrames(b, c));
        QCOMPARE(doc.frameSet().storyChain(), (QStringList{a, b, c}));
        const QVariantMap snapshot = doc.frameSnapshots().at(2).toMap();
        QCOMPARE(snapshot.value(QStringLiteral("id")).toString(), c);
        QCOMPARE(snapshot.value(QStringLiteral("widthMm")).toDouble(), 100.0);
        QCOMPARE(snapshot.value(QStringLiteral("previousFrameId")).toString(), b);
        QCOMPARE(snapshot.value(QStringLiteral("storyIndex")).toInt(), 2);
        const QByteArray threaded = RichTextCodec::encode(doc);
        const QString revision = doc.revisionString();
        QVERIFY(!doc.connectTextFrames(c, a));                          // a cycle
        QVERIFY(!doc.connectTextFrames(a, c));                          // c already follows b
        QVERIFY(!doc.setTextFrameGeometry(a, {{QStringLiteral("widthMm"), qQNaN()}}));
        QVERIFY(!doc.setTextFrameGeometry(a, {{QStringLiteral("widthMm"), 2}})); // too narrow
        QVERIFY(!doc.setTextFrameLayout(a, {{QStringLiteral("columns"), 0}}));
        QVERIFY(!doc.setTextFrameGeometry(QStringLiteral("missing"), geometry(0, 0, 50, 50)));
        QVERIFY(doc.createTextFrame(-1, geometry(0, 0, 50, 50)).isEmpty());
        QVERIFY(doc.createTextFrame(0, {{QStringLiteral("xMm"), 0}}).isEmpty());
        QCOMPARE(RichTextCodec::encode(doc), threaded);
        QCOMPARE(doc.revisionString(), revision);
        // A thread cannot go back a page.
        const QString d = doc.createTextFrame(0, geometry(120, 10, 50, 50));
        QVERIFY(!doc.connectTextFrames(c, d));
        QVERIFY(doc.lastError().contains(QStringLiteral("earlier page")));
        // Disconnecting ends the story; removing a frame closes the thread.
        QVERIFY(doc.disconnectTextFrame(b));
        QCOMPARE(doc.frameSet().storyChain(), (QStringList{a, b}));
        QVERIFY(doc.undo());
        QCOMPARE(doc.frameSet().storyChain(), (QStringList{a, b, c}));
        QVERIFY(doc.removeTextFrame(b));
        QCOMPARE(doc.frameSet().storyChain(), (QStringList{a, c}));
        QVERIFY(doc.removeTextFrame(a));
        QCOMPARE(doc.storyFrameId(), c);
        QVERIFY(doc.setTextFrameLayout(c, {{QStringLiteral("columns"), 2}, {QStringLiteral("gutterMm"), 5},
                                           {QStringLiteral("insetMm"), 2}}));
        QCOMPARE(doc.frameSet().frame(c)->columns, 2);
        QCOMPARE(doc.frameSet().frame(c)->gutterPt, 5 * 72 / 25.4);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QCOMPARE(layout.regions(1).size(), 2); // C's two columns
        QCOMPARE(layout.regions(0).size(), 0); // d is not in the story
        QCOMPARE(layout.regions(1).at(1).rect.left() - layout.regions(1).at(0).rect.right(), 5 * 72 / 25.4 * PxPerPt);
    }

    void framesPersistAndReopenWithTheSameLayout()
    {
        RichTextDocument doc;
        buildStory(doc, 8);
        QVERIFY(doc.setFrameSet(threeFrames(), QStringLiteral("Frames")));
        const QByteArray bytes = RichTextCodec::encode(doc);
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
        RichTextDocument reopened;
        QVERIFY(RichTextCodec::decode(bytes, reopened));
        QCOMPARE(RichTextCodec::encode(reopened), bytes);
        QVERIFY(reopened.frameSet() == doc.frameSet());
        RichTextLayout original;
        original.setDocument(&doc);
        original.paginateAll();
        RichTextLayout again;
        again.setDocument(&reopened);
        again.paginateAll();
        QCOMPARE(pageSummary(again), pageSummary(original));
        QCOMPARE(again.oversetCharacters(), original.oversetCharacters());
        RichTextSessionAdapter adapter;
        adapter.setDocument(&doc);
        RichTextDocument session;
        QVERIFY(RichTextCodec::fromJsonObject(adapter.liveJson(), session));
        QVERIFY(session.frameSet() == doc.frameSet());
        // Invalid frames are refused with their field.
        auto withFrame = [&root](int index, const char *key, const QJsonValue &value) {
            QJsonObject copy = root;
            QJsonObject frames = copy.value(QStringLiteral("textFrames")).toObject();
            QJsonArray list = frames.value(QStringLiteral("frames")).toArray();
            QJsonObject f = list.at(index).toObject();
            f.insert(QLatin1String(key), value);
            list.replace(index, f);
            frames.insert(QStringLiteral("frames"), list);
            copy.insert(QStringLiteral("textFrames"), frames);
            return copy;
        };
        RichTextDocument target;
        OperationError error;
        QVERIFY(!RichTextCodec::fromJsonObject(withFrame(1, "next", QStringLiteral("A")), target, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("textFrames")); // B -> A closes a cycle
        QVERIFY(!RichTextCodec::fromJsonObject(withFrame(0, "widthPt", 5), target, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("textFrames.frames[0].widthPt"));
    }

    void pageColumnsFillLeftToRight()
    {
        RichTextDocument doc;
        buildLines(doc, 45);
        PageSetup page = fixturePage();
        page.columns = 2;
        page.columnGapPt = 18;
        QVERIFY(doc.setPageSetup(page));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), 2); // 2 x 20 lines, then 5
        const qreal column = (300 - 18) / 2.0 * PxPerPt;
        const Page &first = layout.page(0);
        const auto ps = doc.paragraphsInOrder();
        QCOMPARE(first.items.at(0).x, 0.0);
        QCOMPARE(first.items.at(0).width, column);
        QCOMPARE(first.items.at(20).block, ps.at(20)->id);
        QCOMPARE(first.items.at(20).x, column + 18 * PxPerPt);
        QCOMPARE(first.items.at(20).y, 0.0);
        QCOMPARE(layout.page(1).first, (Position{ps.at(40)->id, 0}));
        QHash<QUuid, int> shown;
        QCOMPARE(coverage(layout, doc, &shown), QString());
        // Down from a column's last line goes to the top of the next column.
        const Position below = layout.lineMove({ps.at(19)->id, 0}, 1, nullptr);
        QCOMPARE(below.paragraph, ps.at(20)->id);
        // A page break ends the page, not only the column.
        RichTextDocument breaks;
        buildLines(breaks, 30, 10);
        QVERIFY(breaks.setPageSetup(page));
        RichTextLayout broken;
        broken.setDocument(&breaks);
        broken.paginateAll();
        QCOMPARE(broken.pageCount(), 3);
        // The page setup map sets columns and their gap in one undoable step.
        RichTextDocument dialog;
        QVariantMap settings = dialog.pageSetupMm();
        QCOMPARE(settings.value(QStringLiteral("columns")).toInt(), 1);
        settings.insert(QStringLiteral("columns"), 3);
        settings.insert(QStringLiteral("gutterMm"), 5);
        QVERIFY(dialog.applyPageSetupMm(settings));
        QCOMPARE(dialog.pageSetupValue().columns, 3);
        QCOMPARE(dialog.pageSetupValue().columnGapPt, 5 * 72 / 25.4);
        QCOMPARE(dialog.pageSetupMm().value(QStringLiteral("columns")).toInt(), 3);
        settings.insert(QStringLiteral("scope"), QStringLiteral("section"));
        QVERIFY(!dialog.applyPageSetupMm(settings));
        settings.insert(QStringLiteral("scope"), QStringLiteral("document"));
        settings.insert(QStringLiteral("bleedMm"), 3);
        QVERIFY(!dialog.applyPageSetupMm(settings));
        settings.insert(QStringLiteral("bleedMm"), 0);
        settings.insert(QStringLiteral("columns"), 1.5);
        QVERIFY(!dialog.applyPageSetupMm(settings));
        QCOMPARE(dialog.pageSetupValue().columns, 3);
        QVERIFY(dialog.undo());
        QCOMPARE(dialog.pageSetupValue().columns, 1);
        const QByteArray bytes = RichTextCodec::encode(doc);
        RichTextDocument reopened;
        QVERIFY(RichTextCodec::decode(bytes, reopened));
        QCOMPARE(reopened.pageSetupValue(), page);
    }

    void viewNavigatesFramesByKeyboardAndPointer()
    {
        Scene scene;
        QVERIFY(scene.create(true));
        buildStory(scene.document, 8);
        QVERIFY(scene.document.setFrameSet(threeFrames(), QStringLiteral("Frames")));
        scene.area->layout()->paginateAll();
        QVERIFY(scene.area->frameLayout());
        QVERIFY(scene.area->oversetCharacters() > 0);
        QVERIFY(scene.area->goToFrame(QStringLiteral("B")));
        QCOMPARE(scene.area->caretFrameId(), QStringLiteral("B"));
        QCOMPARE(scene.area->caretContext(), QStringLiteral("Frame 2 of 3"));
        const Paragraph *story = scene.document.paragraphsInOrder().first();
        QCOMPARE(scene.area->caretParagraphId(), sid(story->id)); // B continues the first paragraph
        QVERIFY(scene.area->caretOffset() > 0);
        QTest::keyClick(scene.window.get(), Qt::Key_PageDown, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(scene.area->caretFrameId(), QStringLiteral("C"));
        QVERIFY(!scene.area->commandState(QStringLiteral("navigate.nextFrame")).value(QStringLiteral("enabled")).toBool());
        QTest::keyClick(scene.window.get(), Qt::Key_PageUp, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(scene.area->caretFrameId(), QStringLiteral("B"));
        const QRectF page = scene.area->pageRect(0);
        const QPointF inA = page.topLeft() + QPointF(40, 40) * PxPerPt * scene.area->zoom();
        QCOMPARE(scene.area->frameAt(inA.x(), inA.y()), QStringLiteral("A"));
        QCOMPARE(scene.area->frameAt(page.left() + 2, page.top() + 2), QString());
    }
};

QTEST_MAIN(RichTextFramesTest)
#include "tst_rich_text_frames.moc"
