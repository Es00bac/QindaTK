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

QHash<QString, Extent> scanReview(const RichTextDocument &doc)
{
    QHash<QString, Extent> extents;
    const Annotations &a = doc.annotations();
    if (a.threads.isEmpty() && a.changes.isEmpty()) return extents;
    for (const Paragraph *p : doc.paragraphsInOrder()) {
        int position = 0;
        for (const Span &span : p->spans) {
            const CharFormat &format = doc.charFormat(span.format);
            QStringList ids = format.commentIds;
            if (!format.insertionId.isEmpty()) ids.append(format.insertionId);
            if (!format.deletionId.isEmpty()) ids.append(format.deletionId);
            for (const QString &id : std::as_const(ids)) {
                Extent &extent = extents[id];
                if (extent.order < 0) {
                    extent.start = {p->id, position};
                    extent.order = p->flatIndex;
                } else if (extent.end.paragraph != p->id || extent.end.offset != position) {
                    extent.text += u' ';
                }
                extent.end = {p->id, position + span.length};
                if (extent.text.size() < MaximumQuote)
                    extent.text += QStringView(p->text).mid(position, span.length).left(MaximumQuote - extent.text.size());
            }
            position += span.length;
        }
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
        const QString summary = change.kind == ChangeKind::Insertion ? tr("Inserted “%1”").arg(text)
                                                                     : tr("Deleted “%1”").arg(text);
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
    for (const QString &id : format.commentIds)
        if (const auto thread = m_annotations.threads.constFind(id); thread != m_annotations.threads.constEnd())
            parts.append(tr("Comment by %1: %2").arg(thread->author.isEmpty() ? tr("unknown author") : thread->author,
                                                     quoted(thread->body, 60)));
    return parts.join(QStringLiteral(", "));
}

} // namespace QindaTK
