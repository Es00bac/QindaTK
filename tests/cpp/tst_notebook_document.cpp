// SPDX-License-Identifier: LGPL-3.0-or-later
// NotebookDocument structure, text and cross-block operations (WP-057,
// docs/notebooks.md). Every expected value is exact.
#include "clipboard_service.h"
#include "notebook_document.h"
#include "undo_service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUuid>
#include <QtTest>

using namespace QindaTK;

namespace {
class MemoryClipboard final : public ClipboardAdapter {
public:
    using ClipboardAdapter::ClipboardAdapter;
    const QMimeData *mimeData() const override { return m_data.get(); }
    bool setMimeData(QMimeData *data) override
    {
        if (!data) return false;
        m_data.reset(data);
        emit changed();
        return true;
    }
private:
    std::unique_ptr<QMimeData> m_data;
};

QStringList order(const NotebookDocument &doc, const QString &page)
{
    QStringList ids;
    for (const QVariant &block : doc.blocks(page)) ids.append(block.toMap().value("id").toString());
    return ids;
}
QVariantMap dataOf(const NotebookDocument &doc, const QString &id)
{
    return doc.block(id).value("data").toMap();
}
QString textOf(const NotebookDocument &doc, const QString &id)
{
    return dataOf(doc, id).value("text").toString();
}
QVariantMap run(const NotebookDocument &doc, const QString &id, int index)
{
    return dataOf(doc, id).value("runs").toList().value(index).toMap();
}
} // namespace

class TstNotebookDocument final : public QObject {
    Q_OBJECT
private slots:
    void identityNestingMovesAndUndo()
    {
        NotebookDocument doc;
        const QString section = doc.addSection("Field");
        QVERIFY(!QUuid::fromString(section).isNull());
        const QString page = doc.addPage(section, {}, "Day 1");
        const QString a = doc.insertBlock(page, {}, "text", {{"text", "Alpha"}});
        const QString b = doc.insertBlock(page, a, "text", {{"text", "Beta"}});
        const QString c = doc.insertBlock(page, b, "text", {{"text", "Gamma"}});
        QCOMPARE(order(doc, page), QStringList({a, b, c}));
        QCOMPARE(doc.blockCount(), 3);
        const QVariantList baseline = doc.blocks(page);

        QVERIFY(doc.indentBlock(b));
        QCOMPARE(doc.block(b).value("parentId").toString(), a);
        QCOMPARE(doc.block(b).value("depth").toInt(), 1);
        QVERIFY(doc.indentBlock(c));
        QCOMPARE(doc.block(c).value("parentId").toString(), a);
        QVERIFY(doc.moveBlockBy(c, -1));
        QCOMPARE(order(doc, page), QStringList({a, c, b}));
        QVERIFY(doc.outdentBlock(c));
        QCOMPARE(order(doc, page), QStringList({a, b, c}));
        QCOMPARE(doc.block(c).value("parentId").toString(), QString());
        // Moving a parent moves its subtree.
        QVERIFY(doc.moveBlockBy(a, 1));
        QCOMPARE(order(doc, page), QStringList({c, a, b}));
        QCOMPARE(doc.block(b).value("parentId").toString(), a);
        QVERIFY(!doc.moveBlock(a, page, b, 0)); // into its own subtree
        QVERIFY(!doc.lastError().isEmpty());
        const QVariantList moved = doc.blocks(page);

        for (int i = 0; i < 5; ++i) QVERIFY(doc.undo());
        QCOMPARE(doc.blocks(page), baseline);
        for (int i = 0; i < 5; ++i) QVERIFY(doc.redo());
        QCOMPARE(doc.blocks(page), moved);

        // Cross-page move keeps the IDs and the subtree.
        const QString other = doc.addPage(section, page, "Day 2");
        QVERIFY(doc.moveBlock(a, other, {}, 0));
        QCOMPARE(order(doc, other), QStringList({a, b}));
        QCOMPARE(order(doc, page), QStringList({c}));
        QCOMPARE(doc.pageOfBlock(b), other);

        NotebookDocument reopened;
        QVERIFY(reopened.loadJson(doc.toJson()));
        QCOMPARE(reopened.pages(), doc.pages());
        QCOMPARE(reopened.sections(), doc.sections());
        QCOMPARE(reopened.blocks(other), doc.blocks(other));
    }

    void sectionsAndPages()
    {
        NotebookDocument doc;
        const QString s1 = doc.addSection("One");
        const QString s2 = doc.addSection("Two");
        const QString p1 = doc.addPage(s1, {}, "A");
        const QString p2 = doc.addPage(s1, p1, "B");
        const QString p3 = doc.addPage(s2, {}, "C");
        QVERIFY(doc.movePage(p1, s2, 0));
        QCOMPARE(doc.page(p1).value("sectionId").toString(), s2);
        QCOMPARE(doc.pages().at(1).toMap().value("id").toString(), p1);
        QCOMPARE(doc.pages().at(2).toMap().value("id").toString(), p3);
        QVERIFY(doc.setPageTags(p2, {"tide", " tide ", "", "ink"}));
        QCOMPARE(doc.page(p2).value("tags").toStringList(), QStringList({"tide", "ink"}));
        QVERIFY(doc.setSectionCollapsed(s1, true));
        QVERIFY(!doc.canRedo());
        QCOMPARE(doc.sections().at(0).toMap().value("collapsed").toBool(), true);
        NotebookDocument reopened;
        QVERIFY(reopened.loadJson(doc.toJson()));
        QCOMPARE(reopened.sections().at(0).toMap().value("collapsed").toBool(), true);
        QVERIFY(doc.removeSection(s2));
        QCOMPARE(doc.pages().size(), 1);
        QVERIFY(doc.undo());
        QCOMPARE(doc.pages().size(), 3);
    }

    void splitMergeKeepRunsAndGraphemes()
    {
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const QString a = doc.insertBlock(page, {}, "text", {{"text", "Hello bold world"}});
        QVERIFY(doc.formatText(a, 6, 4, {{"bold", true}}));
        QCOMPARE(dataOf(doc, a).value("runs").toList().size(), 3);
        QCOMPARE(run(doc, a, 1).value("length").toInt(), 4);
        QCOMPARE(run(doc, a, 1).value("bold").toBool(), true);

        const QString tail = doc.splitBlock(a, 8);
        QCOMPARE(textOf(doc, a), QStringLiteral("Hello bo"));
        QCOMPARE(textOf(doc, tail), QStringLiteral("ld world"));
        QCOMPARE(run(doc, a, 1).value("length").toInt(), 2);
        QCOMPARE(run(doc, tail, 0).value("length").toInt(), 2);
        QCOMPARE(run(doc, tail, 0).value("bold").toBool(), true);
        QCOMPARE(doc.mergeBlocks(a, tail), 8);
        QCOMPARE(textOf(doc, a), QStringLiteral("Hello bold world"));
        QCOMPARE(dataOf(doc, a).value("runs").toList().size(), 3);
        QCOMPARE(run(doc, a, 1).value("length").toInt(), 4);
        QVERIFY(doc.block(tail).isEmpty());

        // Typing keeps the formatting of untouched text.
        QVERIFY(doc.setBlockText(a, "Hello bold new world"));
        QCOMPARE(run(doc, a, 1).value("length").toInt(), 4);
        QCOMPARE(run(doc, a, 2).value("length").toInt(), 10);

        // A split inside a surrogate pair snaps to the grapheme boundary.
        const QString emoji = doc.insertBlock(page, a, "text", {{"text", QString::fromUtf8("a\xF0\x9F\x91\x8D" "b")}});
        const QString right = doc.splitBlock(emoji, 2);
        QCOMPARE(textOf(doc, emoji), QStringLiteral("a"));
        QCOMPARE(textOf(doc, right), QString::fromUtf8("\xF0\x9F\x91\x8D" "b"));

        const QString heading = doc.insertBlock(page, right, "heading", {{"text", "Title here"}, {"level", 1}});
        const QString rest = doc.splitBlock(heading, 5);
        QCOMPARE(doc.block(rest).value("type").toString(), QStringLiteral("text"));
        QCOMPARE(textOf(doc, heading), QStringLiteral("Title"));
        QCOMPARE(textOf(doc, rest), QStringLiteral(" here"));
        QVERIFY(doc.undo());
        QVERIFY(doc.block(rest).isEmpty());
        QCOMPARE(textOf(doc, heading), QStringLiteral("Title here"));

        // Split children follow the tail; locked blocks refuse edits.
        const QString parent = doc.insertBlock(page, heading, "text", {{"text", "Parent"}});
        const QString child = doc.insertBlock(page, parent, "text", {{"text", "Child"}});
        QVERIFY(doc.indentBlock(child));
        const QString parentTail = doc.splitBlock(parent, 3);
        QCOMPARE(doc.block(child).value("parentId").toString(), parentTail);
        QVERIFY(doc.setBlockLocked(child, true));
        QVERIFY(!doc.setBlockText(child, "x"));
        QVERIFY(!doc.removeBlock(child));
    }

    void crossBlockCopyPasteIsOneStep()
    {
        MemoryClipboard clipboard;
        ClipboardService service;
        service.setAdapter(&clipboard);
        NotebookDocument source;
        const QString page = source.addPage(source.addSection("S"), {}, "P");
        const QString a = source.insertBlock(page, {}, "text", {{"text", "First paragraph"}});
        const QString b = source.insertBlock(page, a, "heading", {{"text", "Middle"}, {"level", 2}});
        const QString table = source.insertBlock(page, b, "table",
            {{"columns", QVariantList{"X", "Y"}}, {"rows", QVariantList{QVariantList{"1", "2"}, QVariantList{"3", "4"}}}});
        const QString c = source.insertBlock(page, table, "text", {{"text", "Last words"}});
        QVERIFY(source.formatText(a, 6, 9, {{"bold", true}}));
        const QVariantMap selection{{"anchorBlockId", c}, {"anchorOffset", 4}, {"focusBlockId", a}, {"focusOffset", 6}};
        QCOMPARE(source.selectionText(selection), QStringLiteral("paragraph\nMiddle\nX\tY\n1\t2\n3\t4\nLast"));
        QVERIFY(source.copySelection(&service, selection));
        QCOMPARE(service.rangePasteData().value("kind").toString(), QStringLiteral("range"));

        NotebookDocument target;
        const QString targetPage = target.addPage(target.addSection("T"), {}, "Q");
        const QString t = target.insertBlock(targetPage, {}, "text", {{"text", "Say: !"}});
        const QVariantList before = target.blocks(targetPage);
        const QVariantMap caret = target.paste(&service, targetPage, t, 5);
        const QVariantList after = target.blocks(targetPage);
        QCOMPARE(after.size(), 4);
        QCOMPARE(after.at(0).toMap().value("id").toString(), t);
        QCOMPARE(textOf(target, t), QStringLiteral("Say: paragraph"));
        QCOMPARE(run(target, t, 1).value("length").toInt(), 9);
        QCOMPARE(run(target, t, 1).value("bold").toBool(), true);
        QCOMPARE(after.at(1).toMap().value("type").toString(), QStringLiteral("heading"));
        QVERIFY(after.at(1).toMap().value("id").toString() != b);
        QCOMPARE(after.at(2).toMap().value("type").toString(), QStringLiteral("table"));
        QVERIFY(after.at(2).toMap().value("id").toString() != table);
        QCOMPARE(after.at(2).toMap().value("data").toMap().value("rows").toList().at(1).toList().at(1).toString(),
                 QStringLiteral("4"));
        const QString last = after.at(3).toMap().value("id").toString();
        QCOMPARE(textOf(target, last), QStringLiteral("Last!"));
        QCOMPARE(caret.value("blockId").toString(), last);
        QCOMPARE(caret.value("offset").toInt(), 4);
        QVERIFY(target.undo());
        QCOMPARE(target.blocks(targetPage), before);

        // Plain text from another application: one block per line.
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("one\ntwo"));
        clipboard.setMimeData(mime);
        const QVariantMap textCaret = target.paste(&service, targetPage, t, 0);
        QCOMPARE(order(target, targetPage).size(), 2);
        QCOMPARE(textOf(target, t), QStringLiteral("one"));
        QCOMPARE(textOf(target, textCaret.value("blockId").toString()), QStringLiteral("twoSay: !"));
        QCOMPARE(textCaret.value("offset").toInt(), 3);
    }

    void deleteSelectionMergesEndpointsAndUndoes()
    {
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const QString a = doc.insertBlock(page, {}, "text", {{"text", "Keep this"}});
        const QString b = doc.insertBlock(page, a, "text", {{"text", "remove all"}});
        const QString c = doc.insertBlock(page, b, "text", {{"text", "tail end"}});
        const QString d = doc.insertBlock(page, c, "text", {{"text", "child"}});
        QVERIFY(doc.indentBlock(d));
        const QVariantList before = doc.blocks(page);
        const QVariantMap caret = doc.deleteSelection(
            {{"anchorBlockId", a}, {"anchorOffset", 5}, {"focusBlockId", c}, {"focusOffset", 5}});
        QCOMPARE(order(doc, page), QStringList({a, d}));
        QCOMPARE(textOf(doc, a), QStringLiteral("Keep end"));
        QCOMPARE(doc.block(d).value("parentId").toString(), a);
        QCOMPARE(caret.value("blockId").toString(), a);
        QCOMPARE(caret.value("offset").toInt(), 5);
        QVERIFY(doc.undo());
        QCOMPARE(doc.blocks(page), before);

        // A whole non-text endpoint goes; the caret lands on the next block.
        const QString image = doc.insertBlock(page, a, "image", {{"resourceId", "photo"}});
        const QVariantMap after = doc.deleteSelection({{"anchorBlockId", image}, {"focusBlockId", image}});
        QVERIFY(doc.block(image).isEmpty());
        QCOMPARE(after.value("blockId").toString(), b);
    }

    void htmlExportAndImportUseRichText()
    {
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const QString a = doc.insertBlock(page, {}, "text", {{"text", "Use bold words"}});
        QVERIFY(doc.formatText(a, 4, 4, {{"bold", true}}));
        const QString html = doc.selectionHtml({{"anchorBlockId", a}, {"anchorOffset", 0},
                                                {"focusBlockId", a}, {"focusOffset", 14}});
        QVERIFY(html.contains(QStringLiteral("font-weight:700")));
        QVERIFY(html.contains(QStringLiteral("bold")));

        const QString other = doc.addPage(doc.sections().at(0).toMap().value("id").toString(), page, "Q");
        doc.insertHtml(other, {}, 0, html);
        doc.insertHtml(other, {}, 0, QStringLiteral(
            "<h2>Plan</h2><ul><li>one</li></ul><table><tr><td>A</td><td>B</td></tr><tr><td>1</td><td>2</td></tr></table>"));
        QString textBlock, heading, list, tableCells;
        for (const QVariant &value : doc.blocks(other)) {
            const QVariantMap block = value.toMap();
            const QVariantMap data = block.value("data").toMap();
            if (block.value("type") == "text" && data.value("text") == "Use bold words") textBlock = block.value("id").toString();
            if (block.value("type") == "heading" && data.value("text") == "Plan") heading = block.value("id").toString();
            if (block.value("type") == "list" && data.value("text") == "one") list = block.value("id").toString();
            if (block.value("type") == "table")
                for (const QVariant &row : data.value("rows").toList()) tableCells += row.toStringList().join(u',') + u';';
        }
        QVERIFY(!textBlock.isEmpty());
        QCOMPARE(run(doc, textBlock, 1).value("length").toInt(), 4);
        QCOMPARE(run(doc, textBlock, 1).value("bold").toBool(), true);
        QCOMPARE(dataOf(doc, heading).value("level").toInt(), 2);
        QCOMPARE(dataOf(doc, list).value("ordered").toBool(), false);
        QVERIFY2(tableCells.contains(QStringLiteral("1,2;")), qPrintable(tableCells));
    }

    void searchLinksAndBrokenLinks()
    {
        NotebookDocument doc;
        const QString section = doc.addSection("S");
        const QString p1 = doc.addPage(section, {}, "Tide log");
        const QString p2 = doc.addPage(section, p1, "Sources");
        QVERIFY(doc.setPageTags(p1, {"estuary"}));
        const QString x = doc.insertBlock(p1, {}, "text", {{"text", "Salinity rose after the ebb"}});
        doc.insertBlock(p1, x, "table", {{"columns", QVariantList{"A", "B"}},
                                          {"rows", QVariantList{QVariantList{"Salinity 19", "ok"}}}});
        const QVariantList hits = doc.search("salinity");
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits.at(0).toMap().value("blockId").toString(), x);
        QCOMPARE(hits.at(0).toMap().value("offset").toInt(), 0);
        QCOMPARE(hits.at(0).toMap().value("length").toInt(), 8);
        QCOMPARE(hits.at(1).toMap().value("field").toString(), QStringLiteral("table"));
        QCOMPARE(doc.search("sal", {{"wholeWords", true}}).size(), 0);
        QCOMPARE(doc.search("salinity", {{"caseSensitive", true}}).size(), 0);
        QCOMPARE(doc.searchPageIds("sources"), QStringList({p2}));
        QCOMPARE(doc.searchPageIds("estuary"), QStringList({p1}));

        const QString l = doc.insertBlock(p2, {}, "text", {{"text", "see log"}});
        QVERIFY(doc.formatText(l, 4, 3, {{"href", doc.linkTo(p1, x)}}));
        doc.insertBlock(p2, l, "embed", {{"targetId", doc.linkTo(p1)}, {"title", "Log"}});
        QCOMPARE(doc.backlinks(p1).size(), 2);
        QCOMPARE(doc.resolveLink(doc.linkTo(p1, x)).value("valid").toBool(), true);
        QCOMPARE(doc.brokenLinks().size(), 0);
        QVERIFY(doc.removePage(p1));
        QCOMPARE(doc.brokenLinks().size(), 2);
        QCOMPARE(doc.brokenLinks().at(0).toMap().value("reason").toString(), QStringLiteral("missing-page"));
        QVERIFY(doc.undo());
        QCOMPARE(doc.brokenLinks().size(), 0);
    }

    void caretMovesAcrossBlocksByGrapheme()
    {
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const QString a = doc.insertBlock(page, {}, "text", {{"text", "ab"}});
        const QString b = doc.insertBlock(page, a, "table");
        const QString c = doc.insertBlock(page, b, "text", {{"text", QString::fromUtf8("c\xF0\x9F\x91\x8D")}});
        auto at = [&](const QString &id, int offset, const QString &direction) {
            const QVariantMap caret = doc.moveCaret(id, offset, direction);
            return caret.value("blockId").toString() + u':' + QString::number(caret.value("offset").toInt());
        };
        QCOMPARE(at(a, 2, "right"), b + ":0");
        QCOMPARE(at(b, 0, "right"), c + ":0");
        QCOMPARE(at(c, 1, "right"), c + ":3");
        QCOMPARE(at(c, 3, "left"), c + ":1");
        QCOMPARE(at(c, 0, "left"), b + ":0");
        QCOMPARE(at(b, 0, "left"), a + ":2");
        QCOMPARE(at(a, 1, "down"), b + ":0");
        QCOMPARE(at(c, 1, "up"), b + ":0");
        QCOMPARE(at(a, 0, "end"), c + ":3");
        QCOMPARE(doc.moveCaret(a, 0, "up").value("moved").toBool(), false);
    }
    // Audit gap (WP-057 cf2): every paste path honours the per-block text
    // limit, so a paste never commits a block that decodeNative rejects.
    void pastesRespectTheBlockTextLimit()
    {
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const int half = Notebook::Limits::MaxTextLength / 2 + 1;
        const QString a = doc.insertBlock(page, {}, "text", {{"text", QString(half, u'a')}});
        QVERIFY(!a.isEmpty());
        const QString before = doc.toJson();
        QVERIFY(doc.insertText(page, a, 0, QString(Notebook::Limits::MaxTextLength + 1, u'x')).isEmpty());
        QVERIFY(doc.lastError().contains(QStringLiteral("1 Mi")));
        QVERIFY(doc.insertText(page, a, half, QString(half, u'b')).isEmpty());
        QVERIFY(doc.insertHtml(page, a, half, QStringLiteral("<p>%1</p>").arg(QString(half, u'c'))).isEmpty());
        const QVariantMap range{{"format", "qindatk.notebook.blocks"}, {"version", 1}, {"openStart", true},
                                {"openEnd", true}, {"blocks", QVariantList{QVariantMap{
                                    {"id", "r"}, {"type", "text"}, {"parent", -1},
                                    {"data", QVariantMap{{"text", QString(half, u'd')}}}}}}};
        QVERIFY(doc.insertRange(page, a, half, range).isEmpty());
        QCOMPARE(doc.toJson(), before);
        QVERIFY(!doc.insertText(page, a, half, QStringLiteral("fits\nnext")).isEmpty());
        NotebookDocument reopened;
        QVERIFY2(reopened.loadJson(doc.toJson()), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.blockCount(), 2);
    }

    // OFF-27: a range holding an unsupported block copies, cuts and pastes
    // it verbatim (fresh ID, same type and data) as one undo step.
    void rangesKeepUnsupportedBlocks()
    {
        MemoryClipboard clipboard;
        ClipboardService service;
        service.setAdapter(&clipboard);
        auto block = [](const char *id, const char *type, const QJsonObject &data) {
            return QJsonObject{{"id", id}, {"type", type}, {"data", data}};
        };
        const QJsonObject root{{"format", "QindaTK.Notebook"}, {"version", 1},
            {"documentId", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"revision", "1"},
            {"resetGeneration", "0"}, {"title", "T"}, {"sections", QJsonArray{QJsonObject{{"id", "s"}, {"title", "S"}}}},
            {"pages", QJsonArray{QJsonObject{{"id", "p"}, {"sectionId", "s"}, {"title", "P"}, {"tags", QJsonArray{}},
                {"blocks", QJsonArray{block("a", "text", {{"text", "Before"}}),
                                      block("future", "sketch-3d", {{"mesh", "cube"}, {"version", 7}}),
                                      block("c", "text", {{"text", "After"}})}}}}}};
        NotebookDocument doc;
        QVERIFY(doc.resetFromJson(QString::fromUtf8(QJsonDocument(root).toJson())));
        const QVariantMap selection{{"anchorBlockId", "a"}, {"anchorOffset", 3}, {"focusBlockId", "c"}, {"focusOffset", 2}};
        QVERIFY(doc.selectionRange(selection).value("blocks").toList().size() == 3);
        const QVariantMap caret = doc.cutSelection(&service, selection);
        QCOMPARE(caret.value("blockId").toString(), QStringLiteral("a"));
        QCOMPARE(textOf(doc, "a"), QStringLiteral("Befter"));
        QVERIFY(doc.block("future").isEmpty());

        const QVariantMap pasted = doc.paste(&service, "p", "a", 3);
        QVERIFY2(!pasted.isEmpty(), qPrintable(doc.lastError()));
        const QVariantList blocks = doc.blocks("p");
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(textOf(doc, "a"), QStringLiteral("Before"));
        const QVariantMap restored = blocks.at(1).toMap();
        QVERIFY(restored.value("id").toString() != QStringLiteral("future"));
        QCOMPARE(restored.value("type").toString(), QStringLiteral("sketch-3d"));
        QCOMPARE(restored.value("supported").toBool(), false);
        QCOMPARE(restored.value("data").toMap(), (QVariantMap{{"mesh", "cube"}, {"version", 7}}));
        QCOMPARE(textOf(doc, blocks.at(2).toMap().value("id").toString()), QStringLiteral("After"));
        NotebookDocument reopened;
        QVERIFY(reopened.loadJson(doc.toJson()));
        QCOMPARE(reopened.block(restored.value("id").toString()).value("data"), restored.value("data"));
        QVERIFY(doc.undo());
        QCOMPARE(doc.blocks("p").size(), 1);
    }

    // docs/notebooks.md#blocks: a read-only block refuses deletion through
    // its parent, its page and its section, not only when targeted.
    void lockedDescendantsRefuseSubtreePageAndSectionDeletes()
    {
        NotebookDocument doc;
        const QString section = doc.addSection("S");
        const QString page = doc.addPage(section, {}, "P");
        const QString parent = doc.insertBlock(page, {}, "text", {{"text", "Parent"}});
        const QString child = doc.insertBlock(page, parent, "text", {{"text", "Locked child"}});
        QVERIFY(doc.indentBlock(child));
        QVERIFY(doc.setBlockLocked(child, true));
        const QString revision = doc.revision();
        QVERIFY(!doc.removeBlock(parent));
        QVERIFY(doc.lastError().contains(QStringLiteral("read-only")));
        QVERIFY(!doc.removePage(page));
        QVERIFY(!doc.removeSection(section));
        QCOMPARE(doc.revision(), revision);
        QCOMPARE(textOf(doc, child), QStringLiteral("Locked child"));
        QVERIFY(doc.setBlockLocked(child, false));
        QVERIFY(doc.removeBlock(parent));
        QVERIFY(doc.block(child).isEmpty());
    }
};

QTEST_MAIN(TstNotebookDocument)
#include "tst_notebook_document.moc"
