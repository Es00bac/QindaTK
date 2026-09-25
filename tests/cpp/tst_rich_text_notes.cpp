// SPDX-License-Identifier: LGPL-3.0-or-later
// Footnotes and endnotes (WP-052, OFF-10): numbered reference fields whose
// text pagination sets at the foot of the reference's page, or after the
// last block for endnotes; renumbering, undo, typed refusals, persistence,
// and the note area on pages, in continuous layouts and in painted output.
#include "rich_text_codec.h"
#include "rich_text_layout.h"
#include "rich_text_spans.h"
#include <QImage>
#include <QPainter>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QVariantMap reference(const RichTextDocument &doc, const QString &id)
{
    for (const QVariant &value : doc.referenceSnapshots())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

QString number(const RichTextDocument &doc, const QString &id)
{
    return reference(doc, id).value(QStringLiteral("formattedText")).toString();
}

Position referencePosition(const RichTextDocument &doc, const QString &id)
{
    const QVariantMap at = reference(doc, id).value(QStringLiteral("location")).toMap();
    return {QUuid(at.value(QStringLiteral("paragraphId")).toString()), at.value(QStringLiteral("offset")).toInt()};
}

const PageItem *noteItem(const Page &page, const QString &id)
{
    for (const PageItem &item : page.items)
        if (item.kind == PageItem::Note && item.objectId == id) return &item;
    return nullptr;
}

qreal linesBottom(const Page &page)
{
    qreal bottom = 0;
    for (const PageItem &item : page.items)
        if (item.kind == PageItem::Lines || item.kind == PageItem::TableRows) bottom = qMax(bottom, item.y + item.height);
    return bottom;
}

} // namespace

class RichTextNotesTest : public QObject {
    Q_OBJECT
private slots:
    void notesNumberUndoAndPersist()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Alpha\nBeta\nGamma"));
        const auto &flat = doc.paragraphsInOrder();
        const QString p0 = sid(flat.at(0)->id), p1 = sid(flat.at(1)->id), p2 = sid(flat.at(2)->id);
        const QString first = doc.insertNote(p0, 5, QStringLiteral("footnote"), QStringLiteral("First note."));
        const QString second = doc.insertNote(p2, 5, QStringLiteral("footnote"), QStringLiteral("Second note."));
        const QString end = doc.insertNote(p1, 4, QStringLiteral("endnote"), QStringLiteral("End note."));
        QVERIFY(!first.isEmpty() && !second.isEmpty() && !end.isEmpty());
        QCOMPARE(number(doc, first), QStringLiteral("1"));
        QCOMPARE(number(doc, second), QStringLiteral("2"));
        QCOMPARE(number(doc, end), QStringLiteral("i"));
        QCOMPARE(reference(doc, first).value(QStringLiteral("type")).toString(), QStringLiteral("footnote"));
        QCOMPARE(reference(doc, end).value(QStringLiteral("noteText")).toString(), QStringLiteral("End note."));
        QVERIFY(doc.referenceTypes().contains(QStringLiteral("footnote")));
        const Paragraph *alpha = doc.paragraphsInOrder().first();
        QCOMPARE(doc.charFormat(Spans::formatAt(alpha->spans, 5)).verticalAlign, VerticalAlign::Superscript);

        // A note in front renumbers the others in the same undo step.
        const QString front = doc.insertNote(p0, 0, QStringLiteral("footnote"), QStringLiteral("Front."));
        QCOMPARE(number(doc, front), QStringLiteral("1"));
        QCOMPARE(number(doc, first), QStringLiteral("2"));
        QCOMPARE(number(doc, second), QStringLiteral("3"));
        QVERIFY(doc.undo());
        QCOMPARE(number(doc, first), QStringLiteral("1"));
        QVERIFY(doc.setNoteText(first, QStringLiteral("Changed.")));
        QCOMPARE(reference(doc, first).value(QStringLiteral("noteText")).toString(), QStringLiteral("Changed."));
        QVERIFY(doc.undo());
        QCOMPARE(reference(doc, first).value(QStringLiteral("noteText")).toString(), QStringLiteral("First note."));
        // A footnote may become an endnote, never another kind of reference.
        QVERIFY(doc.editReference(first, {{QStringLiteral("type"), QStringLiteral("endnote")}}));
        QCOMPARE(number(doc, first), QStringLiteral("i"));
        QCOMPARE(number(doc, end), QStringLiteral("ii"));
        QCOMPARE(number(doc, second), QStringLiteral("1"));
        QVERIFY(!doc.editReference(first, {{QStringLiteral("type"), QStringLiteral("citation")}}));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(doc.undo());

        // Typed refusals.
        QVERIFY(doc.insertNote(p0, 0, QStringLiteral("footnote"), QStringLiteral("  ")).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("invalidArgument"));
        QVERIFY(doc.insertNote(p0, 0, QStringLiteral("sidenote"), QStringLiteral("x")).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("invalidArgument"));
        QVERIFY(doc.insertTable(doc.endPosition(), 1, 1));
        const Paragraph *cell = doc.paragraphsInOrder().at(3);
        QVERIFY(cell->table);
        QVERIFY(doc.insertNote(sid(cell->id), 0, QStringLiteral("footnote"), QStringLiteral("x")).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(!doc.insertNote(sid(cell->id), 0, QStringLiteral("endnote"), QStringLiteral("In a cell.")).isEmpty());

        // Notes persist with their text.
        const QByteArray json = RichTextCodec::encode(doc);
        RichTextDocument reopened;
        QVERIFY(reopened.loadJson(QString::fromUtf8(json)));
        QCOMPARE(RichTextCodec::encode(reopened), json);
        QCOMPARE(reopened.referenceSnapshots(), doc.referenceSnapshots());
    }

    void footnotesSitAtTheFootOfTheirPage()
    {
        RichTextDocument doc;
        QStringList lines;
        for (int i = 0; i < 140; ++i)
            lines.append(QStringLiteral("Paragraph %1 of the field report, long enough to wrap onto a second line of the page.").arg(i));
        doc.insertText(doc.startPosition(), lines.join(u'\n'));
        const auto &flat = doc.paragraphsInOrder();
        const QString early = doc.insertNote(sid(flat.at(3)->id), 9, QStringLiteral("footnote"), QStringLiteral("Measured at noon."));
        const QString late = doc.insertNote(sid(flat.at(70)->id), 9, QStringLiteral("footnote"),
                                            QStringLiteral("A longer note that wraps: the sensors were recalibrated before the second season began."));
        const QString end = doc.insertNote(sid(flat.at(10)->id), 9, QStringLiteral("endnote"), QStringLiteral("See the appendix."));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QVERIFY(layout.pageCount() >= 3);
        const qreal contentHeight = layout.contentRect().height();
        for (const QString &id : {early, late}) {
            const int pageIndex = layout.pageOfPosition(referencePosition(doc, id));
            const Page &page = layout.page(pageIndex);
            const PageItem *note = noteItem(page, id);
            QVERIFY2(note, qPrintable(id));
            QVERIFY(note->text.startsWith(number(doc, id) + u'\t'));
            QVERIFY(note->rule || id == late);
            // Below the text of its page, inside the content box.
            QVERIFY(note->y >= linesBottom(page));
            QVERIFY(note->y + note->height <= contentHeight + 0.5);
            for (int other = 0; other < layout.pageCount(); ++other)
                if (other != pageIndex) QVERIFY(!noteItem(layout.page(other), id));
        }
        // The endnote follows the last block, on the last page.
        const Page &last = layout.page(layout.pageCount() - 1);
        const PageItem *endNote = noteItem(last, end);
        QVERIFY(endNote);
        QVERIFY(endNote->y >= linesBottom(last));
        QVERIFY(endNote->text.endsWith(QStringLiteral("See the appendix.")));

        // Text inserted in front moves the reference, and its note goes with it.
        const int before = layout.pageOfPosition(referencePosition(doc, late));
        doc.insertText(doc.startPosition(), QString(12, u'\n'));
        const int after = layout.pageOfPosition(referencePosition(doc, late));
        layout.paginateAll();
        QVERIFY(noteItem(layout.page(after), late));
        QVERIFY(after >= before);

        // The note is painted.
        const int pageIndex = layout.pageOfPosition(referencePosition(doc, early));
        const PageItem *note = noteItem(layout.page(pageIndex), early);
        QImage image(layout.pageSize().toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        PaintContext context;
        context.ink = Qt::black;
        context.border = Qt::gray;
        layout.paintPage(&painter, pageIndex, context);
        painter.end();
        const QRect area = QRectF(layout.contentRect().x() + note->x, layout.contentRect().y() + note->y, note->width,
                                  note->height).toAlignedRect();
        int ink = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x) ink += qGray(image.pixel(x, y)) < 128;
        QVERIFY(ink > 20);
    }

    void continuousLayoutsListNotesAtTheEnd()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("One\nTwo"));
        const QString note = doc.insertNote(sid(doc.paragraphsInOrder().first()->id), 3, QStringLiteral("footnote"),
                                            QStringLiteral("Only note."));
        RichTextLayout layout;
        layout.setMode(RichTextLayout::Mode::Continuous);
        layout.setFlowWidth(300);
        layout.setDocument(&doc);
        layout.paginateAll();
        const PageItem *item = noteItem(layout.page(0), note);
        QVERIFY(item);
        QVERIFY(item->y >= linesBottom(layout.page(0)));
        // Editing the note text re-lays the note.
        QVERIFY(doc.setNoteText(note, QStringLiteral("Edited note.")));
        layout.paginateAll();
        QVERIFY(noteItem(layout.page(0), note)->text.endsWith(QStringLiteral("Edited note.")));
    }
};

QTEST_MAIN(RichTextNotesTest)
#include "tst_rich_text_notes.moc"
