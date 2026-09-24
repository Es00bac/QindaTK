// SPDX-License-Identifier: LGPL-3.0-or-later
// Review operations of RichTextDocument (WP-052): annotation records,
// comment threads, tracked insertions and deletions, accept and reject.
// Marks live in character formats, so every operation here is ordinary
// StepSpans/StepText steps plus one StepAnnotation per record, and undo is
// exact (docs/references-review.md#review).
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

#include <QDateTime>

namespace QindaTK {
using namespace RichText;

namespace RichText::Detail {

bool rewriteFormats(RichTextDocument &doc, const Paragraph &p, int from, int to,
                    const std::function<void(CharFormat &)> &edit)
{
    if (to <= from) return true;
    StepSpans step;
    step.paragraph = p.id;
    step.offset = from;
    step.before = Spans::slice(p.spans, from, to - from);
    step.after = Spans::mapped(step.before, [&](int index) {
        CharFormat format = doc.charFormat(index); // a copy: interning may grow the table
        edit(format);
        return doc.internFormat(format);
    });
    if (step.before == step.after) return true;
    return !doc.apply(step);
}

QString newAnnotationId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString reviewTime() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

} // namespace RichText::Detail

using namespace RichText::Detail;

namespace {

QString fieldAt(const RichTextDocument &doc, const Paragraph &p, int offset)
{
    if (offset < 0 || offset >= p.text.size()) return {};
    return doc.charFormat(Spans::formatAt(p.spans, offset)).fieldId;
}

struct Piece {
    int start = 0;
    int length = 0;
    int format = 0;
};

// The runs of [from, to) in `p`, left to right.
QVector<Piece> pieces(const Paragraph &p, int from, int to)
{
    QVector<Piece> result;
    int position = 0;
    for (const Span &span : p.spans) {
        const int start = qMax(position, from);
        const int end = qMin(position + span.length, to);
        if (end > start) result.append({start, end - start, span.format});
        position += span.length;
    }
    return result;
}

// Every change ID any character carries.
QSet<QString> placedChanges(const RichTextDocument &doc)
{
    QSet<QString> ids;
    for (const Paragraph *p : doc.paragraphsInOrder())
        for (const Span &span : p->spans) {
            const CharFormat &format = doc.charFormat(span.format);
            if (!format.insertionId.isEmpty()) ids.insert(format.insertionId);
            if (!format.deletionId.isEmpty()) ids.insert(format.deletionId);
        }
    return ids;
}

} // namespace

OperationError RichTextDocument::applyAnnotation(const StepAnnotation &step, bool forward, ChangeSet &change)
{
    const std::optional<Annotation> &from = forward ? step.before : step.after;
    const std::optional<Annotation> &to = forward ? step.after : step.before;
    if (m_annotations.find(step.id) != from)
        return reject(ErrorCode::Conflict, QStringLiteral("Annotation changed since the edit"));
    if (to && annotationId(*to) != step.id)
        return reject(ErrorCode::InvalidArgument, QStringLiteral("Annotation identity mismatch"));
    if (!from && m_annotations.size() >= MaximumAnnotations)
        return reject(ErrorCode::Validation, QStringLiteral("Too many annotations"));
    m_annotations.set(step.id, to);
    change.annotations = true;
    // Resolved threads are drawn without their highlight: repaint their text.
    const auto *before = from ? std::get_if<CommentThread>(&*from) : nullptr;
    const auto *after = to ? std::get_if<CommentThread>(&*to) : nullptr;
    if (before && after && before->resolved != after->resolved)
        for (Paragraph *p : paragraphsInOrder())
            for (const Span &span : p->spans)
                if (charFormat(span.format).commentIds.contains(step.id)) {
                    touch(p, change);
                    break;
                }
    return {};
}

Position RichTextDocument::outsideField(const Position &position) const
{
    const Paragraph *p = paragraph(position.paragraph);
    if (!p) return position;
    const QString id = fieldAt(*this, *p, position.offset - 1);
    if (id.isEmpty() || fieldAt(*this, *p, position.offset) != id) return position;
    Position end = position;
    while (end.offset < p->text.size() && fieldAt(*this, *p, end.offset) == id) ++end.offset;
    return end;
}

void RichTextDocument::expandOverFields(Position *start, Position *end) const
{
    if (const Paragraph *p = paragraph(start->paragraph)) {
        const QString id = fieldAt(*this, *p, start->offset - 1);
        if (!id.isEmpty() && fieldAt(*this, *p, start->offset) == id)
            while (start->offset > 0 && fieldAt(*this, *p, start->offset - 1) == id) --start->offset;
    }
    *end = outsideField(*end);
}

Position RichTextDocument::afterTrackedDeletion(const Position &position) const
{
    const Paragraph *p = paragraph(position.paragraph);
    if (!p) return position;
    Position result = position;
    while (result.offset < p->text.size()
           && !charFormat(Spans::formatAt(p->spans, result.offset)).deletionId.isEmpty())
        ++result.offset;
    return result;
}

std::optional<QString> RichTextDocument::trackedInsertionId(const Position &at)
{
    if (!m_trackChanges) return QString();
    // Typing next to one's own insertion continues it.
    if (const Paragraph *p = paragraph(at.paragraph))
        for (const int offset : {at.offset - 1, at.offset}) {
            if (offset < 0 || offset >= p->text.size()) continue;
            const QString id = charFormat(Spans::formatAt(p->spans, offset)).insertionId;
            const auto change = m_annotations.changes.constFind(id);
            if (!id.isEmpty() && change != m_annotations.changes.constEnd() && change->author == m_reviewAuthor)
                return id;
        }
    const TrackedChange change{newAnnotationId(), ChangeKind::Insertion, m_reviewAuthor, reviewTime()};
    if (apply(StepAnnotation{change.id, std::nullopt, Annotation(change)})) return std::nullopt;
    return change.id;
}

bool RichTextDocument::markDeleted(const Position &start, const Position &end)
{
    const QVector<Paragraph *> flat = paragraphsInOrder();
    const Paragraph *first = paragraph(start.paragraph);
    const Paragraph *last = paragraph(end.paragraph);
    auto ownChange = [this](const QString &id, ChangeKind kind) {
        const auto change = m_annotations.changes.constFind(id);
        return !id.isEmpty() && change != m_annotations.changes.constEnd() && change->kind == kind
            && change->author == m_reviewAuthor;
    };
    // Deleting next to one's own deletion extends it.
    QString deletion;
    const QString before = start.offset > 0
        ? charFormat(Spans::formatAt(first->spans, start.offset - 1)).deletionId : QString();
    const QString after = end.offset < last->text.size()
        ? charFormat(Spans::formatAt(last->spans, end.offset)).deletionId : QString();
    if (ownChange(before, ChangeKind::Deletion)) deletion = before;
    else if (ownChange(after, ChangeKind::Deletion)) deletion = after;
    QSet<QString> removedInsertions;
    for (int i = last->flatIndex; i >= first->flatIndex; --i) {
        Paragraph *p = flat.at(i);
        const int from = p == first ? start.offset : 0;
        const int to = p == last ? end.offset : int(p->text.size());
        const QVector<Piece> list = pieces(*p, from, to);
        // Right to left, so the offsets of the pieces still to come hold.
        for (qsizetype k = list.size() - 1; k >= 0; --k) {
            const Piece &piece = list.at(k);
            const CharFormat format = charFormat(piece.format);
            if (!format.deletionId.isEmpty()) continue;
            if (ownChange(format.insertionId, ChangeKind::Insertion)) {
                // One's own tracked insertion simply goes.
                removedInsertions.insert(format.insertionId);
                if (apply(textStep(*this, *p, piece.start, piece.length, {}, {}))) return false;
                continue;
            }
            if (deletion.isEmpty()) {
                const TrackedChange change{newAnnotationId(), ChangeKind::Deletion, m_reviewAuthor, reviewTime()};
                if (apply(StepAnnotation{change.id, std::nullopt, Annotation(change)})) return false;
                deletion = change.id;
            }
            if (!rewriteFormats(*this, *p, piece.start, piece.start + piece.length,
                                [&deletion](CharFormat &f) { f.deletionId = deletion; }))
                return false;
        }
    }
    if (removedInsertions.isEmpty()) return true;
    const QSet<QString> placed = placedChanges(*this);
    for (const QString &id : std::as_const(removedInsertions))
        if (!placed.contains(id) && apply(StepAnnotation{id, Annotation(m_annotations.changes.value(id)), std::nullopt}))
            return false;
    return true;
}

bool RichTextDocument::resolveChange(const QString &changeId, bool accept)
{
    const auto found = m_annotations.changes.constFind(changeId);
    if (found == m_annotations.changes.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown tracked change"));
    const TrackedChange change = *found;
    // Accepting a deletion or rejecting an insertion removes the text;
    // the other two keep it and drop the mark.
    const bool removeText = accept == (change.kind == ChangeKind::Deletion);
    AutoTransaction transaction(this, accept ? tr("Accept change") : tr("Reject change"));
    if (!transaction.valid) return false;
    const QVector<Paragraph *> flat = paragraphsInOrder();
    QSet<QString> nested;
    bool ok = true;
    for (qsizetype i = flat.size() - 1; ok && i >= 0; --i) {
        Paragraph *p = flat.at(i);
        const QVector<Piece> list = pieces(*p, 0, int(p->text.size()));
        for (qsizetype k = list.size() - 1; ok && k >= 0; --k) {
            const CharFormat format = charFormat(list.at(k).format);
            if (format.insertionId != changeId && format.deletionId != changeId) continue;
            const int start = list.at(k).start;
            const int length = list.at(k).length;
            if (!removeText) {
                ok = rewriteFormats(*this, *p, start, start + length, [&changeId](CharFormat &f) {
                    if (f.insertionId == changeId) f.insertionId.clear();
                    if (f.deletionId == changeId) f.deletionId.clear();
                });
                continue;
            }
            for (const QString &other : {format.insertionId, format.deletionId})
                if (!other.isEmpty() && other != changeId) nested.insert(other);
            ok = !apply(textStep(*this, *p, start, length, {}, {}));
        }
    }
    ok = ok && !apply(StepAnnotation{changeId, Annotation(change), std::nullopt});
    // Another reviewer's change inside removed text has nothing left to review;
    // their changes elsewhere keep their text and marks.
    if (ok && !nested.isEmpty()) {
        const QSet<QString> placed = placedChanges(*this);
        for (const QString &id : std::as_const(nested)) {
            const auto other = m_annotations.changes.constFind(id);
            if (ok && !placed.contains(id) && other != m_annotations.changes.constEnd())
                ok = !apply(StepAnnotation{id, Annotation(*other), std::nullopt});
        }
    }
    return transaction.finish(ok, {});
}

bool RichTextDocument::acceptChange(const QString &changeId) { return resolveChange(changeId, true); }

bool RichTextDocument::rejectChange(const QString &changeId) { return resolveChange(changeId, false); }

QString RichTextDocument::addComment(const QString &startParagraphId, int startOffset,
                                     const QString &endParagraphId, int endOffset, const QString &body)
{
    return addCommentAt({QUuid(startParagraphId), startOffset}, {QUuid(endParagraphId), endOffset}, body);
}

QString RichTextDocument::addCommentAt(const Position &a, const Position &b, const QString &body)
{
    if (!isValidPosition(a) || !isValidPosition(b)) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid comment range"));
        return {};
    }
    const QString text = body.trimmed();
    if (text.isEmpty()) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("A comment needs text"));
        return {};
    }
    const bool ordered = compare(a, b) <= 0;
    const Position start = ordered ? a : b;
    const Position end = ordered ? b : a;
    AutoTransaction transaction(this, tr("Add comment"));
    if (!transaction.valid) return {};
    const CommentThread thread{newAnnotationId(), m_reviewAuthor, reviewTime(), text, false, {}};
    bool ok = !apply(StepAnnotation{thread.id, std::nullopt, Annotation(thread)});
    const QVector<Paragraph *> flat = paragraphsInOrder();
    const int first = paragraph(start.paragraph)->flatIndex;
    const int last = paragraph(end.paragraph)->flatIndex;
    int marked = 0;
    for (int i = first; ok && i <= last; ++i) {
        const Paragraph *p = flat.at(i);
        const int from = i == first ? start.offset : 0;
        const int to = i == last ? end.offset : int(p->text.size());
        marked += qMax(0, to - from);
        ok = rewriteFormats(*this, *p, from, to, [&thread](CharFormat &f) {
            f.commentIds.append(thread.id);
            f.commentIds.sort();
        });
    }
    if (ok && marked == 0) ok = !reject(ErrorCode::InvalidArgument, QStringLiteral("Select the text to comment on"));
    if (!transaction.finish(ok, {})) return {};
    return thread.id;
}

bool RichTextDocument::replyToComment(const QString &threadId, const QString &body)
{
    const auto found = m_annotations.threads.constFind(threadId);
    if (found == m_annotations.threads.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown comment thread"));
    if (body.trimmed().isEmpty()) return !reject(ErrorCode::InvalidArgument, QStringLiteral("A reply needs text"));
    CommentThread next = *found;
    next.replies.append({newAnnotationId(), m_reviewAuthor, reviewTime(), body.trimmed()});
    AutoTransaction transaction(this, tr("Reply"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepAnnotation{threadId, Annotation(*found), Annotation(next)}), {});
}

bool RichTextDocument::resolveComment(const QString &threadId, bool resolved)
{
    const auto found = m_annotations.threads.constFind(threadId);
    if (found == m_annotations.threads.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown comment thread"));
    if (found->resolved == resolved) return true;
    CommentThread next = *found;
    next.resolved = resolved;
    AutoTransaction transaction(this, resolved ? tr("Resolve comment") : tr("Reopen comment"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepAnnotation{threadId, Annotation(*found), Annotation(next)}), {});
}

bool RichTextDocument::removeComment(const QString &threadId)
{
    const auto found = m_annotations.threads.constFind(threadId);
    if (found == m_annotations.threads.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown comment thread"));
    const CommentThread thread = *found;
    AutoTransaction transaction(this, tr("Delete comment"));
    if (!transaction.valid) return false;
    bool ok = true;
    const QVector<Paragraph *> flat = paragraphsInOrder();
    for (const Paragraph *p : flat) {
        if (!ok) break;
        ok = rewriteFormats(*this, *p, 0, int(p->text.size()),
                            [&threadId](CharFormat &f) { f.commentIds.removeAll(threadId); });
    }
    ok = ok && !apply(StepAnnotation{threadId, Annotation(thread), std::nullopt});
    return transaction.finish(ok, {});
}

} // namespace QindaTK
