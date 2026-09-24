// SPDX-License-Identifier: LGPL-3.0-or-later
// Structured table edits behind Tk.TableEditor (WP-051): stable identities,
// exact undo/redo, anchors, objects, persistence and pagination.
#include "document_session.h"
#include "rich_text_codec.h"
#include "rich_text_fixtures.h"
#include "rich_text_html.h"
#include "rich_text_layout.h"
#include "rich_text_session_adapter.h"
#include "rich_text_spans.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;

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

QUuid cellParagraph(const RichTextDocument &doc, const QUuid &table, int row, int column)
{
    return doc.table(table)->rows.at(row).cells.at(column).paragraphs.first()->id;
}

TablePtr liveTable(const RichTextDocument &doc, const QUuid &id)
{
    for (const Block &block : doc.blocks())
        if (block.table && block.table->id == id) return block.table;
    return {};
}

// Page keys, first positions and item shapes: equal summaries mean equal pagination.
QStringList pageSummary(RichTextLayout &layout)
{
    QStringList pages;
    for (int i = 0; i < layout.pageCount(); ++i) {
        const Page &page = layout.page(i);
        QString line = sid(page.key) + u' ' + sid(page.first.paragraph) + u':' + QString::number(page.first.offset);
        for (const PageItem &item : page.items)
            line += QStringLiteral(" %1/%2+%3").arg(int(item.kind)).arg(item.first).arg(item.count);
        pages.append(line);
    }
    return pages;
}

} // namespace

class RichTextTablesTest : public QObject {
    Q_OBJECT
private slots:
    void snapshotReportsStableIdentities()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 3, 2);
        const Table *t = doc.table(id);
        const QVariantMap snapshot = doc.tableSnapshot(sid(id));
        QCOMPARE(snapshot.value(QStringLiteral("id")).toString(), sid(id));
        QCOMPARE(snapshot.value(QStringLiteral("headerRows")).toInt(), 1);
        QCOMPARE(snapshot.value(QStringLiteral("canMerge")).toBool(), false);
        const QVariantList rows = snapshot.value(QStringLiteral("rows")).toList();
        const QVariantList columns = snapshot.value(QStringLiteral("columns")).toList();
        const QVariantList cells = snapshot.value(QStringLiteral("cells")).toList();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(columns.size(), 2);
        QCOMPARE(cells.size(), 6);
        QVERIFY(rows.at(0).toMap().value(QStringLiteral("header")).toBool());
        QVERIFY(!rows.at(1).toMap().value(QStringLiteral("header")).toBool());
        for (int c = 0; c < 2; ++c) {
            QCOMPARE(columns.at(c).toMap().value(QStringLiteral("id")).toString(), sid(t->columnIds.at(c)));
            QCOMPARE(columns.at(c).toMap().value(QStringLiteral("width")).toDouble(), 1.0);
        }
        const QVariantMap cell = cells.at(3).toMap(); // row 1, column 1
        QCOMPARE(cell.value(QStringLiteral("id")).toString(), sid(t->rows.at(1).cells.at(1).id));
        QCOMPARE(cell.value(QStringLiteral("rowId")).toString(), sid(t->rows.at(1).id));
        QCOMPARE(cell.value(QStringLiteral("columnId")).toString(), sid(t->columnIds.at(1)));
        QCOMPARE(cell.value(QStringLiteral("rowIndex")).toInt(), 1);
        QCOMPARE(cell.value(QStringLiteral("columnIndex")).toInt(), 1);
        QCOMPARE(cell.value(QStringLiteral("rowSpan")).toInt(), 1);
        QCOMPARE(cell.value(QStringLiteral("columnSpan")).toInt(), 1);
        QCOMPARE(cell.value(QStringLiteral("text")).toString(), QStringLiteral("r1c1"));
        QCOMPARE(cell.value(QStringLiteral("paragraphIds")).toStringList(),
                 QStringList{sid(cellParagraph(doc, id, 1, 1))});
        const QVariantList tables = doc.property("tables").toList();
        QCOMPARE(tables.size(), 1);
        QCOMPARE(tables.first().toMap(), snapshot);
        QVERIFY(doc.tableSnapshot(sid(QUuid::createUuid())).isEmpty());
    }

    void gridEditsUndoAndRedoExactly()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 3, 3);
        const QString tableId = sid(id);
        auto row = [&](int r) { return sid(doc.table(id)->rows.at(r).id); };
        auto column = [&](int c) { return sid(doc.table(id)->columnIds.at(c)); };
        QSignalSpy changes(&doc, &RichTextDocument::contentChanged);
        QVector<QByteArray> states{RichTextCodec::encode(doc)};
        QVERIFY(doc.insertTableRow(tableId, 1));
        const ChangeSet inserted = changes.last().first().value<ChangeSet>();
        QVERIFY(inserted.tables.contains(id) && inserted.structure);
        QCOMPARE(inserted.paragraphs.size(), 3);
        QVERIFY(inserted.removedParagraphs.isEmpty());
        states.append(RichTextCodec::encode(doc));
        QVERIFY(doc.insertTableColumn(tableId, 0));
        states.append(RichTextCodec::encode(doc));
        QVERIFY(doc.setTableColumnWidth(tableId, column(2), 2.5));
        states.append(RichTextCodec::encode(doc));
        QVERIFY(doc.removeTableRow(tableId, row(3)));
        QCOMPARE(changes.last().first().value<ChangeSet>().removedParagraphs.size(), 4);
        states.append(RichTextCodec::encode(doc));
        QVERIFY(doc.removeTableColumn(tableId, column(1)));
        states.append(RichTextCodec::encode(doc));
        QVERIFY(doc.removeTableRow(tableId, row(0))); // the only header row
        QCOMPARE(doc.table(id)->headerRows, 0);       // (the row inserted at 1 was a body row)
        QCOMPARE(doc.undoText(), QStringLiteral("Delete table row"));
        states.append(RichTextCodec::encode(doc));
        for (int i = int(states.size()) - 2; i >= 0; --i) {
            QVERIFY(doc.undo());
            QCOMPARE(RichTextCodec::encode(doc), states.at(i));
        }
        for (int i = 1; i < states.size(); ++i) {
            QVERIFY(doc.redo());
            QCOMPARE(RichTextCodec::encode(doc), states.at(i));
        }
        for (const QByteArray &state : std::as_const(states)) {
            RichTextDocument copy;
            QVERIFY(RichTextCodec::decode(state, copy));
            QCOMPARE(RichTextCodec::encode(copy), state);
        }
    }

    void unchangedCellsKeepTheirParagraphs()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 3, 2);
        QHash<QUuid, QPair<const Paragraph *, quint64>> before;
        QSet<QUuid> rowOne;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c) {
                const Paragraph *p = doc.paragraph(cellParagraph(doc, id, r, c));
                before.insert(p->id, {p, p->revision});
                if (r == 1) rowOne.insert(p->id);
            }
        const QUuid moved = cellParagraph(doc, id, 2, 1);
        QVERIFY(doc.insertTableColumn(sid(id), 1));
        QVERIFY(doc.removeTableRow(sid(id), sid(doc.table(id)->rows.at(1).id)));
        for (auto it = before.cbegin(); it != before.cend(); ++it) {
            if (rowOne.contains(it.key())) {
                QVERIFY(!doc.paragraph(it.key()));
                continue;
            }
            QCOMPARE(doc.paragraph(it.key()), it->first); // the same object,
            QCOMPARE(it->first->revision, it->second);     // never touched
        }
        QCOMPARE(doc.paragraph(moved)->table, doc.table(id));
        QCOMPARE(doc.paragraph(moved)->row, 1);
        QCOMPARE(doc.paragraph(moved)->cell, 2);
        QVERIFY(doc.undo());
        for (const QUuid &p : std::as_const(rowOne)) {
            QCOMPARE(doc.paragraph(p), before.value(p).first);
            QCOMPARE(doc.paragraph(p)->row, 1);
        }
    }

    void anchorsInRemovedCellsMoveToTheNearestCell()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 4, 3);
        PositionAnchor removedRow{{cellParagraph(doc, id, 2, 1), 2}};
        PositionAnchor lastRow{{cellParagraph(doc, id, 3, 2), 1}};
        PositionAnchor removedColumn{{cellParagraph(doc, id, 1, 1), 1}};
        PositionAnchor untouched{{cellParagraph(doc, id, 1, 0), 3}};
        for (PositionAnchor *anchor : {&removedRow, &lastRow, &removedColumn, &untouched}) doc.registerAnchor(anchor);
        const Position below{cellParagraph(doc, id, 3, 1), 0};
        const Position above{cellParagraph(doc, id, 1, 2), 0};
        const Position untouchedAt = untouched.position;
        QVERIFY(doc.removeTableRow(sid(id), sid(doc.table(id)->rows.at(2).id)));
        QCOMPARE(removedRow.position, below); // the next row, same column
        QVERIFY(doc.removeTableRow(sid(id), sid(doc.table(id)->rows.at(2).id))); // now the last row
        QCOMPARE(lastRow.position, above);    // the previous row at the end
        QCOMPARE(removedRow.position, (Position{cellParagraph(doc, id, 1, 1), 0}));
        QVERIFY(doc.removeTableColumn(sid(id), sid(doc.table(id)->columnIds.at(1))));
        QCOMPARE(removedColumn.position, above); // same row, the next column
        QCOMPARE(removedRow.position, above);
        QCOMPARE(untouched.position, untouchedAt);
        for (PositionAnchor *anchor : {&removedRow, &lastRow, &removedColumn, &untouched}) doc.unregisterAnchor(anchor);
    }

    void removedObjectsReturnOnUndo()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 2, 2);
        RichObject image;
        image.id = QStringLiteral("cell-image");
        image.widthPt = image.heightPt = 12;
        QVERIFY(doc.insertObject({cellParagraph(doc, id, 1, 0), 1}, image));
        QVERIFY(doc.removeTableRow(sid(id), sid(doc.table(id)->rows.at(1).id)));
        QVERIFY(!doc.object(image.id));
        RichTextDocument saved; // no object is left without its anchor
        QVERIFY(RichTextCodec::decode(RichTextCodec::encode(doc), saved));
        QVERIFY(doc.undo());
        QVERIFY(doc.object(image.id) && *doc.object(image.id) == image);
        QVERIFY(doc.redo());
        QVERIFY(!doc.object(image.id));
    }

    void staleBlockOrderNeverCorruptsUndo()
    {
        // Undoing a split merges paragraphs and leaves Table::blockIndex
        // stale; the table step must still replace its own block.
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 2, 2);
        const QUuid intro = doc.blocks().first().paragraph->id;
        const QByteArray start = RichTextCodec::encode(doc);
        QVERIFY(doc.insertTableRow(sid(id), 2));
        const QByteArray withRow = RichTextCodec::encode(doc);
        QVERIFY(doc.splitParagraph({intro, 2}));
        QVERIFY(doc.undo());
        QVERIFY(doc.undo());
        QCOMPARE(doc.blocks().size(), 3);
        QVERIFY(doc.blocks().last().paragraph);
        QCOMPARE(RichTextCodec::encode(doc), start);
        QVERIFY(doc.redo());
        QCOMPARE(RichTextCodec::encode(doc), withRow);
        QVERIFY(doc.redo());
        QVERIFY(doc.undo());
        // A request made while the order is stale still finds its table.
        QVERIFY(doc.insertTableColumn(sid(id), 2));
        QCOMPARE(doc.table(id)->columnWeights.size(), 3);
        QCOMPARE(doc.blocks().size(), 3);
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), withRow);
    }

    void newRowsAndColumnsFollowTheirNeighbours()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 3, 2);
        const QString tableId = sid(id);
        CharFormatPatch bold;
        bold.weight = 700;
        QVERIFY(doc.applyCharFormat({cellParagraph(doc, id, 0, 0), 0}, {cellParagraph(doc, id, 0, 1), 4}, bold));
        auto markWeight = [&](int r, int c) {
            return doc.charFormat(doc.paragraph(cellParagraph(doc, id, r, c))->markFormat).weight;
        };
        QVERIFY(doc.insertTableRow(tableId, 0)); // above the header: joins the header block
        QCOMPARE(doc.table(id)->headerRows, 2);
        QCOMPARE(markWeight(0, 1), 700);
        QVERIFY(doc.insertTableRow(tableId, 2)); // below the header: a body row
        QCOMPARE(doc.table(id)->headerRows, 2);
        QCOMPARE(markWeight(2, 1), 0);
        QVERIFY(doc.setTableCellText(tableId, sid(doc.table(id)->rows.at(0).cells.at(1).id), QStringLiteral("Head")));
        const Paragraph *head = doc.paragraph(cellParagraph(doc, id, 0, 1));
        QCOMPARE(doc.charFormat(Spans::formatAt(head->spans, 0)).weight, 700);
        QVERIFY(doc.removeTableRow(tableId, sid(doc.table(id)->rows.at(1).id)));
        QCOMPARE(doc.table(id)->headerRows, 1);
        QVERIFY(doc.setTableColumnWidth(tableId, sid(doc.table(id)->columnIds.at(0)), 3));
        QVERIFY(doc.insertTableColumn(tableId, 1));
        QCOMPARE(doc.table(id)->columnWeights, (QVector<double>{3, 3, 1}));
        QCOMPARE(markWeight(0, 1), 700);
        QCOMPARE(markWeight(2, 1), 0);
        // Deleting every body row leaves a table without header rows.
        while (doc.table(id)->rows.size() > 1)
            QVERIFY(doc.removeTableRow(tableId, sid(doc.table(id)->rows.last().id)));
        QCOMPARE(doc.table(id)->headerRows, 0);
        RichTextDocument saved;
        QVERIFY(RichTextCodec::decode(RichTextCodec::encode(doc), saved));
    }

    void invalidRequestsChangeNothing()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 1, 1);
        const QString tableId = sid(id);
        const QString rowId = sid(doc.table(id)->rows.first().id);
        const QString columnId = sid(doc.table(id)->columnIds.first());
        const QString cellId = sid(doc.table(id)->rows.first().cells.first().id);
        const QString unknown = sid(QUuid::createUuid());
        const QString revision = doc.revisionString();
        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(!doc.insertTableRow(unknown, 0));
        QVERIFY(!doc.insertTableRow(tableId, -1));
        QVERIFY(!doc.insertTableRow(tableId, 2));
        QVERIFY(!doc.insertTableColumn(tableId, 2));
        QVERIFY(!doc.removeTableRow(tableId, rowId));       // the last row stays
        QVERIFY(!doc.removeTableColumn(tableId, columnId)); // the last column stays
        QVERIFY(!doc.removeTableRow(tableId, unknown));
        QVERIFY(!doc.removeTableColumn(tableId, unknown));
        for (double width : {0.0, 0.5, 2e6, qQNaN(), qInf()})
            QVERIFY(!doc.setTableColumnWidth(tableId, columnId, width));
        QVERIFY(!doc.setTableColumnWidth(tableId, unknown, 2));
        const QString separator(QChar(QChar::ParagraphSeparator));
        for (const QString &text : {QStringLiteral("a\nb"), QStringLiteral("a\rb"), QStringLiteral("a") + separator,
                                    QString(QChar(ObjectChar))})
            QVERIFY(!doc.setTableCellText(tableId, cellId, text));
        QVERIFY(!doc.setTableCellText(tableId, unknown, QStringLiteral("x")));
        QVERIFY(!doc.lastError().isEmpty());
        QCOMPARE(doc.revisionString(), revision);
        QCOMPARE(RichTextCodec::encode(doc), before);
        // A malformed host-built step is rejected before it changes anything.
        const TablePtr live = liveTable(doc, id);
        auto header = std::make_shared<Table>(*live);
        header->headerRows = 1;
        auto duplicate = std::make_shared<Table>(*live);
        duplicate->rows.append(duplicate->rows.first());
        QVERIFY(doc.beginTransaction(QStringLiteral("Host step")));
        QVERIFY(bool(doc.apply(StepTable{id, live, header, {}})));
        QVERIFY(bool(doc.apply(StepTable{id, live, duplicate, {}})));
        QVERIFY(bool(doc.apply(StepTable{id, header, live, {}}))); // not the live grid
        doc.cancelTransaction();
        QCOMPARE(RichTextCodec::encode(doc), before);
        // Read-only documents and multi-paragraph cells are refused.
        doc.setReadOnly(true);
        QVERIFY(!doc.insertTableColumn(tableId, 1));
        doc.setReadOnly(false);
        QVERIFY(doc.splitParagraph({cellParagraph(doc, id, 0, 0), 1}));
        QVERIFY(!doc.setTableCellText(tableId, cellId, QStringLiteral("x")));
    }

    void cellTextIsOneStepInTheReplacedFormat()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 2, 2);
        const QString cellId = sid(doc.table(id)->rows.at(1).cells.at(0).id);
        const Paragraph *p = doc.paragraph(cellParagraph(doc, id, 1, 0));
        CharFormatPatch italic;
        italic.italic = 1;
        QVERIFY(doc.applyCharFormat({p->id, 0}, {p->id, int(p->text.size())}, italic));
        const QByteArray formatted = RichTextCodec::encode(doc);
        PositionAnchor end{{p->id, int(p->text.size())}};
        doc.registerAnchor(&end);
        QVERIFY(doc.setTableCellText(sid(id), cellId, QStringLiteral("Replaced\ttext")));
        QCOMPARE(p->text, QStringLiteral("Replaced\ttext"));
        QCOMPARE(p->spans.size(), 1);
        QCOMPARE(doc.charFormat(p->spans.first().format).italic, qint8(1));
        QCOMPARE(end.position, (Position{p->id, 13}));
        QCOMPARE(doc.undoText(), QStringLiteral("Edit table cell"));
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), formatted);
        const QString revision = doc.revisionString();
        QVERIFY(doc.setTableCellText(sid(id), cellId, p->text)); // unchanged: no step
        QCOMPARE(doc.revisionString(), revision);
        QVERIFY(doc.setTableCellText(sid(id), cellId, QString()));
        QVERIFY(p->text.isEmpty());
        doc.unregisterAnchor(&end);
    }

    void columnIdentitiesPersist()
    {
        RichTextDocument doc;
        const QUuid id = buildTable(doc, 2, 3);
        // Native files written before columnIds load with derived identities.
        QJsonObject root = RichTextCodec::toJsonObject(doc);
        QJsonArray blocks = root.value(QStringLiteral("blocks")).toArray();
        QJsonObject block = blocks.at(1).toObject();
        QJsonObject table = block.value(QStringLiteral("table")).toObject();
        QVERIFY(table.contains(QStringLiteral("columnIds")));
        table.remove(QStringLiteral("columnIds"));
        block.insert(QStringLiteral("table"), table);
        blocks.replace(1, block);
        root.insert(QStringLiteral("blocks"), blocks);
        RichTextDocument old;
        QStringList warnings;
        QVERIFY(RichTextCodec::fromJsonObject(root, old, nullptr, &warnings));
        QVERIFY(warnings.isEmpty());
        for (int c = 0; c < 3; ++c) QCOMPARE(old.table(id)->columnIds.at(c), derivedColumnId(id, c));
        RichTextDocument reopened; // the next save stores them
        QVERIFY(RichTextCodec::decode(RichTextCodec::encode(old), reopened));
        QCOMPARE(reopened.table(id)->columnIds, old.table(id)->columnIds);
        // Session saves carry the same identities as the canonical codec.
        QVERIFY(doc.insertTableColumn(sid(id), 0));
        RichTextSessionAdapter adapter;
        adapter.setDocument(&doc);
        RichTextDocument session;
        QVERIFY(RichTextCodec::fromJsonObject(adapter.liveJson(), session));
        QCOMPARE(session.table(id)->columnIds, doc.table(id)->columnIds);
        // HTML import and paste give every column a fresh identity.
        const Fragment html = RichTextHtml::toFragment(QStringLiteral("<table><tr><td>a</td><td>b</td></tr></table>"));
        TablePtr imported;
        for (const Block &item : html.blocks)
            if (item.table) imported = item.table;
        QVERIFY(imported);
        QCOMPARE(imported->columnIds.size(), 2);
        QVERIFY(!imported->columnIds.contains(QUuid()) && imported->columnIds.at(0) != imported->columnIds.at(1));
        Fragment copied = doc.extractFragment(doc.startPosition(), doc.endPosition());
        for (Block &item : copied.blocks)
            if (item.table) {
                item.table = std::make_shared<Table>(*item.table);
                item.table->columnIds.clear(); // a host-built fragment without identities
            }
        QVERIFY(doc.insertFragment(doc.endPosition(), copied));
        QVector<const Table *> tables;
        for (const Block &item : doc.blocks())
            if (item.table) tables.append(item.table.get());
        QCOMPARE(tables.size(), 2);
        QCOMPARE(tables.last()->columnIds.size(), 4);
        for (const QUuid &column : tables.last()->columnIds)
            QVERIFY(!column.isNull() && !tables.first()->columnIds.contains(column));
    }

    void paginationFollowsGridEdits()
    {
        RichTextDocument doc;
        const HardFixture fixture = buildHardFixture(doc);
        const QString tableId = sid(fixture.table);
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        const QStringList baseline = pageSummary(layout);
        // Page 1 repeats the header and holds body rows 3..9.
        QCOMPARE(layout.page(1).items.at(1).first, 3);
        QCOMPARE(layout.page(1).items.at(1).count, 7);
        QVERIFY(doc.insertTableRow(tableId, 10)); // new cells copy the row's 18 pt line box
        layout.paginateAll();
        QCOMPARE(layout.page(1).items.at(1).count, 8);
        QCOMPARE(layout.page(1).items.at(1).height, 8 * RowPx + BorderPx);
        // A wider first column re-lays its cells at the new width.
        const QUuid first = cellParagraph(doc, fixture.table, 5, 0);
        const QUuid second = cellParagraph(doc, fixture.table, 5, 1);
        const qreal firstWidth = layout.metrics(first)->width;
        const qreal secondWidth = layout.metrics(second)->width;
        QVERIFY(doc.setTableColumnWidth(tableId, sid(doc.table(fixture.table)->columnIds.at(0)), 2));
        layout.paginateAll();
        QVERIFY(layout.metrics(first)->width > firstWidth + 50);
        QVERIFY(layout.metrics(second)->width < secondWidth - 25);
        // Removed cells leave the layout; undo restores the pagination exactly.
        const QUuid removed = cellParagraph(doc, fixture.table, 5, 2);
        QVERIFY(doc.removeTableColumn(tableId, sid(doc.table(fixture.table)->columnIds.at(2))));
        layout.paginateAll();
        QVERIFY(!layout.metrics(removed));
        QVERIFY(doc.undo() && doc.undo() && doc.undo());
        layout.paginateAll();
        QCOMPARE(pageSummary(layout), baseline);
    }
};

QTEST_MAIN(RichTextTablesTest)
#include "tst_rich_text_tables.moc"
