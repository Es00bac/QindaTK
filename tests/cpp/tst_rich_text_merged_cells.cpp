// SPDX-License-Identifier: LGPL-3.0-or-later
// Merged table cells (WP-051): merge and split with exact undo, grid edits
// across spans, validation, persistence, HTML, pagination across page breaks
// and keyboard navigation over covered slots.
#include "document_session.h"
#include "rich_text_codec.h"
#include "rich_text_html.h"
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

// "intro", a rows x columns table with one header row whose cells read
// "r<row>c<column>", and the trailing paragraph. Returns the table ID.
QUuid buildTable(RichTextDocument &doc, int rows, int columns)
{
    doc.insertText(doc.startPosition(), QStringLiteral("intro"));
    const auto caret = doc.insertTable(doc.endPosition(), rows, columns, 1);
    const QUuid id = doc.paragraph(caret->paragraph)->table->id;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c)
            doc.setTableCellText(sid(id), sid(doc.table(id)->rows.at(r).cells.at(c).id),
                                 QStringLiteral("r%1c%2").arg(r).arg(c));
    return id;
}

const TableCell &cellAt(const RichTextDocument &doc, const QUuid &table, int row, int column)
{
    return doc.table(table)->rows.at(row).cells.at(column);
}

QString cellId(const RichTextDocument &doc, const QUuid &table, int row, int column)
{
    return sid(cellAt(doc, table, row, column).id);
}

QVariantMap snapshotCell(const QVariantMap &snapshot, int row, int column)
{
    for (const QVariant &cell : snapshot.value(QStringLiteral("cells")).toList()) {
        const QVariantMap map = cell.toMap();
        if (map.value(QStringLiteral("rowIndex")).toInt() == row && map.value(QStringLiteral("columnIndex")).toInt() == column)
            return map;
    }
    return {};
}

// Every paragraph's text is shown exactly once, in page order: "" or the first mismatch.
QString coverageProblem(RichTextLayout &layout, const RichTextDocument &doc)
{
    QHash<QUuid, int> shown;
    for (int page = 0; page < layout.pageCount(); ++page)
        for (const PageLine &line : layout.pageLines(page)) {
            const LineBox &box = line.metrics->lines.at(line.line);
            const int from = layout.modelOffset(*line.metrics, box.start);
            const int to = layout.modelOffset(*line.metrics, box.start + box.length);
            if (from != shown.value(line.paragraph->id, 0))
                return QStringLiteral("page %1: '%2' from %3").arg(page).arg(line.paragraph->text).arg(from);
            shown.insert(line.paragraph->id, to);
        }
    for (const Paragraph *p : doc.paragraphsInOrder())
        if (shown.value(p->id, -1) != p->text.size()) return QStringLiteral("'%1' not shown whole").arg(p->text);
    return {};
}

// The table block's JSON of an encoded buildTable document, edited by `edit`.
template <typename Edit>
QJsonObject withTable(const QByteArray &bytes, Edit edit)
{
    QJsonObject root = QJsonDocument::fromJson(bytes).object();
    QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
    QJsonObject block = blocks.at(1).toObject();
    QJsonObject table = block.value(QStringLiteral("table")).toObject();
    QJsonArray rows = table.value(QStringLiteral("rows")).toArray();
    edit(rows);
    table.insert(QStringLiteral("rows"), rows);
    block.insert(QStringLiteral("table"), table);
    blocks.replace(1, block);
    root.insert(QStringLiteral("blocks"), blocks);
    return root;
}

void editCell(QJsonArray &rows, int row, int column, const std::function<void(QJsonObject &)> &edit)
{
    QJsonObject r = rows.at(row).toObject();
    QJsonArray cells = r.value(QStringLiteral("cells")).toArray();
    QJsonObject cell = cells.at(column).toObject();
    edit(cell);
    cells.replace(column, cell);
    r.insert(QStringLiteral("cells"), cells);
    rows.replace(row, r);
}

} // namespace

class RichTextMergedCellsTest : public QObject {
    Q_OBJECT
private slots:
    void mergeAndSplitKeepContentAndUndoExactly()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 4, 3);
        const QString table = sid(id);
        const QByteArray original = RichTextCodec::encode(doc);
        const QVector<QUuid> content{cellAt(doc, id, 1, 0).paragraphs.first()->id, cellAt(doc, id, 1, 1).paragraphs.first()->id,
                                     cellAt(doc, id, 2, 0).paragraphs.first()->id, cellAt(doc, id, 2, 1).paragraphs.first()->id};
        const QString anchor = cellId(doc, id, 1, 0);
        const QString covered = cellId(doc, id, 2, 1);
        QVERIFY(doc.mergeTableCells(table, {cellId(doc, id, 1, 0), cellId(doc, id, 1, 1), cellId(doc, id, 2, 0),
                                            cellId(doc, id, 2, 1)}));
        QCOMPARE(doc.undoText(), QStringLiteral("Merge table cells"));
        QCOMPARE(cellAt(doc, id, 1, 0).rowSpan, 2);
        QCOMPARE(cellAt(doc, id, 1, 0).columnSpan, 2);
        QCOMPARE(cellAt(doc, id, 1, 0).paragraphs.size(), 4);
        for (int i = 0; i < 4; ++i) QCOMPARE(cellAt(doc, id, 1, 0).paragraphs.at(i)->id, content.at(i)); // reading order
        QVERIFY(cellAt(doc, id, 1, 1).paragraphs.isEmpty() && cellAt(doc, id, 2, 0).paragraphs.isEmpty()
                && cellAt(doc, id, 2, 1).paragraphs.isEmpty());
        const QVariantMap snapshot = doc.tableSnapshot(table);
        QCOMPARE(snapshot.value(QStringLiteral("cells")).toList().size(), 12 - 3);
        QVERIFY(snapshot.value(QStringLiteral("canMerge")).toBool());
        const QVariantMap merged = snapshotCell(snapshot, 1, 0);
        QCOMPARE(merged.value(QStringLiteral("id")).toString(), anchor);
        QCOMPARE(merged.value(QStringLiteral("rowSpan")).toInt(), 2);
        QCOMPARE(merged.value(QStringLiteral("columnSpan")).toInt(), 2);
        QCOMPARE(merged.value(QStringLiteral("text")).toString(), QStringLiteral("r1c0\nr1c1\nr2c0\nr2c1"));
        QVERIFY(snapshotCell(snapshot, 1, 1).isEmpty());
        const QByteArray mergedBytes = RichTextCodec::encode(doc);
        // Splitting keeps the content in the first cell; the others come back
        // with their identities and an empty paragraph.
        QVERIFY(doc.splitTableCell(table, anchor));
        QCOMPARE(doc.undoText(), QStringLiteral("Split table cell"));
        QCOMPARE(cellAt(doc, id, 1, 0).rowSpan, 1);
        QCOMPARE(cellAt(doc, id, 1, 0).columnSpan, 1);
        QCOMPARE(cellAt(doc, id, 1, 0).paragraphs.size(), 4);
        QCOMPARE(cellId(doc, id, 2, 1), covered);
        QCOMPARE(cellAt(doc, id, 2, 1).paragraphs.size(), 1);
        QVERIFY(cellAt(doc, id, 2, 1).paragraphs.first()->text.isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), mergedBytes);
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), original);
        QVERIFY(doc.redo());
        QCOMPARE(RichTextCodec::encode(doc), mergedBytes);
    }

    void invalidMergesAndSplitsChangeNothing()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 4, 3);
        const QString table = sid(id);
        const QByteArray before = RichTextCodec::encode(doc);
        auto cell = [&](int r, int c) { return cellId(doc, id, r, c); };
        QVERIFY(!doc.mergeTableCells(table, {cell(1, 0), cell(1, 1), cell(2, 0)})); // an L shape
        QVERIFY(doc.lastError().contains(QStringLiteral("rectangle")));
        QVERIFY(!doc.mergeTableCells(table, {cell(0, 0), cell(1, 0)}));              // header and body
        QVERIFY(doc.lastError().contains(QStringLiteral("header")));
        QVERIFY(!doc.mergeTableCells(table, {cell(1, 0)}));
        QVERIFY(!doc.mergeTableCells(table, {cell(1, 0), sid(QUuid::createUuid())}));
        QVERIFY(!doc.mergeTableCells(sid(QUuid::createUuid()), {cell(1, 0), cell(1, 1)}));
        QVERIFY(!doc.splitTableCell(table, cell(1, 0)));                              // not merged
        QCOMPARE(RichTextCodec::encode(doc), before);
        QVERIFY(doc.mergeTableCells(table, {cell(1, 0), cell(2, 0)}));
        const QString coveredId = cell(2, 0);
        QVERIFY(!doc.mergeTableCells(table, {coveredId, cell(2, 1)}));                // a covered slot
        QVERIFY(!doc.splitTableCell(table, coveredId));
        // A merged cell can join a larger rectangle.
        QVERIFY(doc.mergeTableCells(table, {cell(1, 0), cell(1, 1), cell(2, 1)}));
        QCOMPARE(cellAt(doc, id, 1, 0).rowSpan, 2);
        QCOMPARE(cellAt(doc, id, 1, 0).columnSpan, 2);
        QCOMPARE(cellAt(doc, id, 1, 0).paragraphs.size(), 4);
    }

    void gridEditsCarryMergedCells()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 5, 4);
        const QString table = sid(id);
        auto cell = [&](int r, int c) { return cellId(doc, id, r, c); };
        const QString anchor = cell(1, 1);
        QVERIFY(doc.mergeTableCells(table, {cell(1, 1), cell(1, 2), cell(2, 1), cell(2, 2)}));
        // A row inserted inside the merged cell joins it.
        QVERIFY(doc.insertTableRow(table, 2));
        QCOMPARE(cellAt(doc, id, 1, 1).rowSpan, 3);
        QVERIFY(cellAt(doc, id, 2, 1).paragraphs.isEmpty() && cellAt(doc, id, 2, 2).paragraphs.isEmpty());
        QCOMPARE(cellAt(doc, id, 2, 0).paragraphs.size(), 1);
        QVERIFY(cellAt(doc, id, 2, 0).paragraphs.first()->text.isEmpty());
        // A column inserted inside it widens it.
        QVERIFY(doc.insertTableColumn(table, 2));
        QCOMPARE(cellAt(doc, id, 1, 1).columnSpan, 3);
        QCOMPARE(doc.table(id)->columnWeights.size(), 5);
        QVERIFY(cellAt(doc, id, 3, 2).paragraphs.isEmpty());
        // Deleting its first row or column moves it on with its content and identity.
        const QVector<ParagraphPtr> content = cellAt(doc, id, 1, 1).paragraphs;
        QVERIFY(doc.removeTableRow(table, sid(doc.table(id)->rows.at(1).id)));
        QCOMPARE(cell(1, 1), anchor);
        QCOMPARE(cellAt(doc, id, 1, 1).rowSpan, 2);
        QCOMPARE(cellAt(doc, id, 1, 1).columnSpan, 3);
        QCOMPARE(cellAt(doc, id, 1, 1).paragraphs, content);
        QVERIFY(doc.removeTableColumn(table, sid(doc.table(id)->columnIds.at(1))));
        QCOMPARE(cell(1, 1), anchor);
        QCOMPARE(cellAt(doc, id, 1, 1).columnSpan, 2);
        QCOMPARE(cellAt(doc, id, 1, 1).paragraphs, content);
        // A row or column inside the span shrinks it.
        QVERIFY(doc.removeTableRow(table, sid(doc.table(id)->rows.at(2).id)));
        QCOMPARE(cellAt(doc, id, 1, 1).rowSpan, 1);
        QVERIFY(doc.removeTableColumn(table, sid(doc.table(id)->columnIds.at(2))));
        QCOMPARE(cellAt(doc, id, 1, 1).columnSpan, 1);
        QCOMPARE(cellAt(doc, id, 1, 1).paragraphs, content);
        // Each edit is one undo step back to the plain grid.
        for (int i = 0; i < 7; ++i) QVERIFY(doc.undo());
        QCOMPARE(cellAt(doc, id, 1, 1).rowSpan, 1);
        QCOMPARE(cellAt(doc, id, 1, 2).paragraphs.first()->text, QStringLiteral("r1c2"));
    }

    void mergedCellsPersist()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 3, 3);
        auto cell = [&](int r, int c) { return cellId(doc, id, r, c); };
        QVERIFY(doc.mergeTableCells(sid(id), {cell(1, 0), cell(1, 1), cell(2, 0), cell(2, 1)}));
        const QByteArray bytes = RichTextCodec::encode(doc);
        QCOMPARE(QJsonDocument::fromJson(bytes).object().value(QStringLiteral("version")).toInt(), 2);
        RichTextDocument reopened;
        QVERIFY(RichTextCodec::decode(bytes, reopened));
        QCOMPARE(RichTextCodec::encode(reopened), bytes);
        QCOMPARE(reopened.tableSnapshot(sid(id)), doc.tableSnapshot(sid(id)));
        RichTextSessionAdapter adapter;
        adapter.setDocument(&doc);
        RichTextDocument session;
        QVERIFY(RichTextCodec::fromJsonObject(adapter.liveJson(), session));
        QCOMPARE(RichTextCodec::encode(session), bytes);
        // Invalid spans are refused with the table's rows as the field.
        const QJsonObject paragraph{{QStringLiteral("id"), sid(QUuid::createUuid())}, {QStringLiteral("text"), QString()},
                                    {QStringLiteral("spans"), QJsonArray{}}, {QStringLiteral("mark"), 0}};
        const QVector<QJsonObject> broken{
            withTable(bytes, [&](QJsonArray &rows) { // a covered slot with content
                editCell(rows, 2, 1, [&](QJsonObject &c) { c.insert(QStringLiteral("paragraphs"), QJsonArray{paragraph}); });
            }),
            withTable(bytes, [](QJsonArray &rows) { // a span leaving the table
                editCell(rows, 1, 0, [](QJsonObject &c) { c.insert(QStringLiteral("rowSpan"), 3); });
            }),
            withTable(bytes, [](QJsonArray &rows) { // covered slots without their merged cell
                editCell(rows, 1, 0, [](QJsonObject &c) { c.remove(QStringLiteral("rowSpan")); c.remove(QStringLiteral("columnSpan")); });
            }),
        };
        for (const QJsonObject &root : broken) {
            RichTextDocument target;
            OperationError error;
            QVERIFY(!RichTextCodec::fromJsonObject(root, target, &error));
            QCOMPARE(error.fieldPath, QStringLiteral("blocks[1].table.rows"));
        }
    }

    void htmlKeepsMergedCells()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 3, 3);
        QVERIFY(doc.mergeTableCells(sid(id), {cellId(doc, id, 1, 0), cellId(doc, id, 1, 1)}));
        const QString html = RichTextHtml::fromFragment(doc.extractFragment(doc.startPosition(), doc.endPosition()));
        QVERIFY(html.contains(QStringLiteral("<td colspan=\"2\">")));
        QVERIFY(!html.contains(QStringLiteral("rowspan")));
        const Fragment imported = RichTextHtml::toFragment(QStringLiteral(
            "<table><tr><td rowspan=\"2\">a</td><td>b</td></tr><tr><td>c</td></tr></table>"));
        TablePtr table;
        for (const Block &block : imported.blocks)
            if (block.table) table = block.table;
        QVERIFY(table);
        QCOMPARE(table->rows.at(0).cells.at(0).rowSpan, 2);
        QVERIFY(table->rows.at(1).cells.at(0).paragraphs.isEmpty());
        QCOMPARE(table->rows.at(1).cells.at(1).paragraphs.first()->text, QStringLiteral("c"));
        TableGridMap map;
        QCOMPARE(mapTableGrid(*table, &map), QString());
        RichTextDocument target;
        target.insertText(target.startPosition(), QStringLiteral("x"));
        QVERIFY(target.insertFragment(target.endPosition(), imported));
        bool found = false;
        for (const Block &block : target.blocks())
            found = found || (block.table && block.table->rows.at(0).cells.at(0).rowSpan == 2);
        QVERIFY(found);
    }

    void mergedCellsSplitAcrossPages()
    {
        RichTextDocument doc;
        doc.setPageSetup(fixturePage());
        QStringList before;
        for (int i = 0; i < 10; ++i) before.append(QStringLiteral("L%1").arg(i));
        doc.insertText(doc.startPosition(), before.join(u'\n'));
        const auto caret = doc.insertTable(doc.endPosition(), 6, 2, 1);
        const QUuid id = doc.paragraph(caret->paragraph)->table->id;
        const QString table = sid(id);
        QVERIFY(doc.mergeTableCells(table, {cellId(doc, id, 1, 0), cellId(doc, id, 2, 0), cellId(doc, id, 3, 0),
                                            cellId(doc, id, 4, 0), cellId(doc, id, 5, 0)}));
        // Thirty one-line paragraphs in the merged cell: taller than a page.
        QStringList lines;
        for (int i = 0; i < 30; ++i) lines.append(QStringLiteral("m%1").arg(i));
        QVERIFY(doc.insertText({cellAt(doc, id, 1, 0).paragraphs.first()->id, 0}, lines.join(u'\n')));
        for (int r = 0; r < 6; ++r) QVERIFY(doc.setTableCellText(table, cellId(doc, id, r, 1), QStringLiteral("side %1").arg(r)));
        fixLineBoxes(doc);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), 3);
        QCOMPARE(coverageProblem(layout, doc), QString());
        // Page 0: ten lines, the header and rows 1-4; the merged cell shows the
        // five lines that fit in those rows and continues under a repeated header.
        const PageItem &rows = layout.page(0).items.last();
        QCOMPARE(rows.kind, PageItem::TableRows);
        QCOMPARE(rows.first, 0);
        QCOMPARE(rows.count, 5);
        QVERIFY(layout.page(1).items.first().headerRepeat);
        QCOMPARE(layout.page(1).items.at(1).first, 5);
        QCOMPARE(layout.page(1).items.at(1).cellFirstLine.value(0), 5);
        const TableCell &merged = cellAt(doc, id, 1, 0);
        QCOMPARE(layout.pageOfPosition({merged.paragraphs.at(4)->id, 0}), 0);
        QCOMPARE(layout.pageOfPosition({merged.paragraphs.at(5)->id, 0}), 1);
        QCOMPARE(layout.pageOfPosition({merged.paragraphs.last()->id, 0}), 2);
        // Every position maps to its page and back.
        for (const Paragraph *p : doc.paragraphsInOrder())
            for (int offset = 0; offset <= p->text.size(); ++offset) {
                const CaretBox box = layout.caretBox({p->id, offset});
                QVERIFY2(box.page >= 0, qPrintable(p->text));
                const Position hit = layout.hitTest(box.page, box.rect.center());
                const CaretBox back = layout.caretBox(hit);
                QCOMPARE(back.page, box.page);
                QVERIFY2(qAbs(back.rect.x() - box.rect.x()) < 0.5, qPrintable(QStringLiteral("%1 @%2").arg(p->text).arg(offset)));
            }
        // Splitting the merged cell and undoing restores the pagination.
        QVERIFY(doc.splitTableCell(table, cellId(doc, id, 1, 0)));
        layout.paginateAll();
        QCOMPARE(coverageProblem(layout, doc), QString());
        QVERIFY(doc.undo());
        layout.paginateAll();
        QCOMPARE(layout.pageCount(), 3);
        QCOMPARE(layout.page(1).items.at(1).cellFirstLine.value(0), 5);
    }

    void tabSkipsCoveredSlots()
    {
        Scene scene;
        QVERIFY(scene.create(true));
        RichTextDocument &doc = scene.document;
        const QUuid id = buildTable(doc, 3, 3);
        QVERIFY(doc.mergeTableCells(sid(id), {cellId(doc, id, 1, 0), cellId(doc, id, 1, 1)}));
        QVERIFY(scene.area->goToParagraph(sid(cellAt(doc, id, 0, 2).paragraphs.first()->id), 0));
        QTest::keyClick(scene.window.get(), Qt::Key_Tab);
        QCOMPARE(scene.area->caretParagraphId(), sid(cellAt(doc, id, 1, 0).paragraphs.last()->id));
        QCOMPARE(scene.area->caretContext(), QStringLiteral("Table row 2, column 1 (merged: 1 rows, 2 columns)"));
        QTest::keyClick(scene.window.get(), Qt::Key_Tab);
        QCOMPARE(scene.area->caretParagraphId(), sid(cellAt(doc, id, 1, 2).paragraphs.first()->id));
        QTest::keyClick(scene.window.get(), Qt::Key_Backtab, Qt::ShiftModifier);
        QCOMPARE(scene.area->caretParagraphId(), sid(cellAt(doc, id, 1, 0).paragraphs.last()->id));
    }
};

QTEST_MAIN(RichTextMergedCellsTest)
#include "tst_rich_text_merged_cells.moc"
