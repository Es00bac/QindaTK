// SPDX-License-Identifier: LGPL-3.0-or-later
// QML snapshots of comments and tracked changes (WP-052) in the shape
// Tk.ReviewPane takes, and the review context at a position for assistive
// users (docs/references-review.md#review).
#include "rich_text_document.h"
#include "rich_text_spans.h"

#include <algorithm>
#include <limits>

namespace QindaTK {
using namespace RichText;

namespace {

constexpr int MaximumQuote = 200;

struct Extent {
    Position start;
    Position end;
    int order = -1;     // flat index of the first paragraph, for sorting
    QString text;       // the marked text, at most MaximumQuote characters
};

void extend(Extent &extent, const Paragraph *p, int start, int length, QStringView text)
{
    if (extent.order < 0) {
        extent.start = {p->id, start};
        extent.order = p->flatIndex;
    } else if (extent.end.paragraph != p->id || extent.end.offset != start) {
        extent.text += u' ';
    }
    extent.end = {p->id, start + length};
    if (extent.text.size() < MaximumQuote) extent.text += text.left(MaximumQuote - extent.text.size());
}

QHash<QString, Extent> scanReview(const RichTextDocument &doc)
{
    QHash<QString, Extent> extents;
    const Annotations &a = doc.annotations();
    if (a.threads.isEmpty() && a.changes.isEmpty()) return extents;
    // AGENT-NOTE: a comment moved under tracking is on the deleted source and
    // on the pasted text; it is anchored where its text is live.
    QHash<QString, Extent> deletedComments;
    for (const Paragraph *p : doc.paragraphsInOrder()) {
        int position = 0;
        for (const Span &span : p->spans) {
            const CharFormat &format = doc.charFormat(span.format);
            const QStringView text = QStringView(p->text).mid(position, span.length);
            for (const QString &id : format.commentIds)
                extend(format.deletionId.isEmpty() ? extents[id] : deletedComments[id], p, position, span.length, text);
            for (const QString *id : {&format.insertionId, &format.deletionId, &format.formatChangeId})
                if (!id->isEmpty()) extend(extents[*id], p, position, span.length, text);
            position += span.length;
        }
        // A tracked paragraph break reads as a pilcrow at the paragraph's end.
        const CharFormat &mark = doc.charFormat(p->markFormat);
        for (const QString *id : {&mark.insertionId, &mark.deletionId})
            if (!id->isEmpty()) extend(extents[*id], p, int(p->text.size()), 0, u"\u00b6");
    }
    for (auto it = deletedComments.cbegin(); it != deletedComments.cend(); ++it)
        if (!extents.contains(it.key())) extents.insert(it.key(), *it);
    // Paragraph format changes and deleted table structure name what they changed.
    for (const TrackedChange &change : a.changes) {
        if (extents.contains(change.id)) continue;
        QVector<const Paragraph *> covered;
        for (const ParagraphFormatChange &entry : change.paragraphs)
            if (const Paragraph *p = doc.paragraph(entry.paragraph)) covered.append(p);
        if (const Table *table = doc.table(change.table)) {
            const int column = int(table->columnIds.indexOf(change.column));
            for (const TableRow &row : table->rows)
                for (int c = 0; c < row.cells.size(); ++c)
                    if (change.row.isNull() ? c == column : row.id == change.row)
                        for (const ParagraphPtr &p : row.cells.at(c).paragraphs) covered.append(p.get());
        }
        std::sort(covered.begin(), covered.end(), [](const Paragraph *x, const Paragraph *y) { return x->flatIndex < y->flatIndex; });
        for (const Paragraph *p : std::as_const(covered))
            extend(extents[change.id], p, 0, int(p->text.size()), p->text);
    }
    return extents;
}

QString quoted(QString text, int length)
{
    text.replace(QChar(ObjectChar), QChar(0x25a1)).replace(QChar(LineBreakChar), u' ');
    text = text.simplified();
    return text.size() > length ? text.left(length - 1) + QChar(0x2026) : text;
}

QVariant locationOf(const Extent *extent)
{
    if (!extent) return {};
    return QVariantMap{{QStringLiteral("paragraphId"), extent->start.paragraph.toString(QUuid::WithoutBraces)},
                       {QStringLiteral("offset"), extent->start.offset},
                       {QStringLiteral("endParagraphId"), extent->end.paragraph.toString(QUuid::WithoutBraces)},
                       {QStringLiteral("endOffset"), extent->end.offset}};
}

} // namespace

QVariantList RichTextDocument::reviewItems() const
{
    const QHash<QString, Extent> extents = scanReview(*this);
    struct Row {
        int order;
        int offset;
        QString time;
        QString id;
        QVariantMap item;
    };
    QVector<Row> rows;
    for (const CommentThread &thread : m_annotations.threads) {
        const auto found = extents.constFind(thread.id);
        const Extent *extent = found == extents.constEnd() ? nullptr : &found.value();
        QVariantList replies;
        for (const CommentReply &reply : thread.replies)
            replies.append(QVariantMap{{QStringLiteral("id"), reply.id}, {QStringLiteral("author"), reply.author},
                                       {QStringLiteral("time"), reply.time}, {QStringLiteral("body"), reply.body}});
        const QString status = !extent ? QStringLiteral("orphaned")
            : thread.resolved ? QStringLiteral("resolved") : QStringLiteral("open");
        QVariantMap item{{QStringLiteral("id"), thread.id},
                         {QStringLiteral("kind"), QStringLiteral("comment")},
                         {QStringLiteral("threadId"), thread.id},
                         {QStringLiteral("author"), thread.author},
                         {QStringLiteral("time"), thread.time},
                         {QStringLiteral("summary"), quoted(thread.body.section(u'\n', 0, 0), 60)},
                         {QStringLiteral("body"), thread.body},
                         {QStringLiteral("quote"), extent ? quoted(extent->text, 80) : QString()},
                         {QStringLiteral("status"), status},
                         {QStringLiteral("resolved"), thread.resolved},
                         {QStringLiteral("replies"), replies},
                         {QStringLiteral("location"), locationOf(extent)},
                         {QStringLiteral("canAccept"), false},
                         {QStringLiteral("canReject"), false}};
        if (!extent) item.insert(QStringLiteral("error"), tr("The commented text was deleted. Undo or paste it back to anchor the comment again."));
        rows.append({extent ? extent->order : std::numeric_limits<int>::max(), extent ? extent->start.offset : 0,
                     thread.time, thread.id, item});
    }
    for (const TrackedChange &change : m_annotations.changes) {
        const auto found = extents.constFind(change.id);
        const Extent *extent = found == extents.constEnd() ? nullptr : &found.value();
        const QString text = extent ? quoted(extent->text, 40) : QString();
        QString summary = change.kind == ChangeKind::Insertion ? tr("Inserted “%1”").arg(text)
                        : change.kind == ChangeKind::Deletion ? tr("Deleted “%1”").arg(text)
                        : change.formats.isEmpty() ? tr("Paragraph format of “%1”").arg(text)
                                                   : tr("Formatted “%1”").arg(text);
        if (!change.row.isNull()) summary = tr("Deleted table row “%1”").arg(text);
        if (!change.column.isNull()) summary = tr("Deleted table column “%1”").arg(text);
        QVariantMap item{{QStringLiteral("id"), change.id},
                         {QStringLiteral("kind"), QStringLiteral("change")},
                         {QStringLiteral("changeKind"), changeKindName(change.kind)},
                         {QStringLiteral("author"), change.author},
                         {QStringLiteral("time"), change.time},
                         {QStringLiteral("summary"), summary},
                         {QStringLiteral("body"), extent ? quoted(extent->text, MaximumQuote) : QString()},
                         {QStringLiteral("status"), extent ? QStringLiteral("open") : QStringLiteral("orphaned")},
                         {QStringLiteral("location"), locationOf(extent)},
                         {QStringLiteral("canAccept"), true},
                         {QStringLiteral("canReject"), true}};
        if (!extent) item.insert(QStringLiteral("error"), tr("The changed text is no longer in the document."));
        rows.append({extent ? extent->order : std::numeric_limits<int>::max(), extent ? extent->start.offset : 0,
                     change.time, change.id, item});
    }
    // Document order; orphaned items last, oldest first.
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.offset != b.offset) return a.offset < b.offset;
        return a.time != b.time ? a.time < b.time : a.id < b.id;
    });
    QVariantList list;
    for (const Row &row : std::as_const(rows)) list.append(row.item);
    return list;
}

QString RichTextDocument::reviewContextAt(const QString &paragraphId, int offset) const
{
    const Paragraph *p = paragraph(QUuid(paragraphId));
    if (!p || p->text.isEmpty()) return {};
    const int at = qBound(0, offset < p->text.size() ? offset : offset - 1, int(p->text.size()) - 1);
    const CharFormat &format = charFormat(Spans::formatAt(p->spans, at));
    QStringList parts;
    if (const auto field = m_annotations.fields.constFind(format.fieldId); field != m_annotations.fields.constEnd())
        parts.append(tr("Reference field"));
    if (const auto change = m_annotations.changes.constFind(format.insertionId); change != m_annotations.changes.constEnd())
        parts.append(tr("Inserted by %1").arg(change->author.isEmpty() ? tr("unknown author") : change->author));
    if (const auto change = m_annotations.changes.constFind(format.deletionId); change != m_annotations.changes.constEnd())
        parts.append(tr("Deleted by %1").arg(change->author.isEmpty() ? tr("unknown author") : change->author));
    if (const auto change = m_annotations.changes.constFind(format.formatChangeId); change != m_annotations.changes.constEnd())
        parts.append(tr("Formatted by %1").arg(change->author.isEmpty() ? tr("unknown author") : change->author));
    for (const QString &id : format.commentIds)
        if (const auto thread = m_annotations.threads.constFind(id); thread != m_annotations.threads.constEnd())
            parts.append(tr("Comment by %1: %2").arg(thread->author.isEmpty() ? tr("unknown author") : thread->author,
                                                     quoted(thread->body, 60)));
    return parts.join(QStringLiteral(", "));
}

} // namespace QindaTK
