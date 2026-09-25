// SPDX-License-Identifier: LGPL-3.0-or-later
// Tracked formatting and tracked paragraph marks of RichTextDocument
// (WP-052), and navigation between review items. A formatting change keeps
// the former look in its record (TrackedChange::formats and ::paragraphs),
// so rejecting it restores that look; a paragraph break carries its review
// marks in the `markFormat` of the paragraph before it, so accepting a
// tracked deletion across paragraphs joins them exactly as the untracked
// deletion would (docs/references-review.md#tracked-changes).
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

bool RichTextDocument::refuseWhileTracking(const QString &operation)
{
    if (!m_trackChanges) return false;
    // AGENT-GUARD: every content edit under trackChanges is recorded or
    // refused here; an edit that applies untracked would be unreviewable.
    reject(ErrorCode::Unsupported,
           tr("%1 is not recorded as a tracked change. Turn off Track changes to do it.").arg(operation));
    return true;
}

bool RichTextDocument::applyFormatStep(StepSpans step, QString *changeId)
{
    const bool unchanged = step.before == step.after && step.markBefore == step.markAfter;
    if (!m_trackChanges || unchanged) return unchanged || !apply(step);
    std::optional<TrackedChange> record;
    if (!changeId->isEmpty()) record = m_annotations.changes.value(*changeId);
    const std::optional<TrackedChange> stored = record;
    QVector<Span> after;
    // Walk the before and after runs together; they cover the same length.
    qsizetype i = 0, j = 0;
    int usedBefore = 0, usedAfter = 0;
    while (i < step.before.size() && j < step.after.size()) {
        const Span &b = step.before.at(i);
        const Span &a = step.after.at(j);
        const int length = qMin(b.length - usedBefore, a.length - usedAfter);
        const CharFormat old = charFormat(b.format);
        CharFormat next = charFormat(a.format);
        const auto insertion = m_annotations.changes.constFind(old.insertionId);
        const bool ownInsertion = insertion != m_annotations.changes.constEnd()
            && insertion->author == m_reviewAuthor;
        // Deleted text and one's own insertion change without a record.
        if (b.format != a.format && old.deletionId.isEmpty() && !ownInsertion) {
            if (!old.formatChangeId.isEmpty()) {
                // A pending change keeps the look from before its first edit.
                next.formatChangeId = old.formatChangeId;
                next.formatSlot = old.formatSlot;
            } else {
                if (!record) record = TrackedChange{newAnnotationId(), ChangeKind::Format, m_reviewAuthor, reviewTime(),
                                                    {}, {}, {}, {}, {}};
                const CharFormat former = old.withoutMarks();
                qsizetype slot = record->formats.indexOf(former);
                if (slot < 0) {
                    if (record->formats.size() >= MaximumFormerFormats)
                        return !reject(ErrorCode::Validation, tr("Too many different formats in one tracked change"));
                    record->formats.append(former);
                    slot = record->formats.size() - 1;
                }
                next.formatChangeId = record->id;
                next.formatSlot = int(slot);
            }
        }
        after.append({length, internFormat(next)});
        usedBefore += length;
        usedAfter += length;
        if (usedBefore == b.length) ++i, usedBefore = 0;
        if (usedAfter == a.length) ++j, usedAfter = 0;
    }
    step.after = Spans::normalized(after);
    if (record && record != stored) {
        if (apply(StepAnnotation{record->id, stored ? std::optional<Annotation>(*stored) : std::nullopt, Annotation(*record)}))
            return false;
        *changeId = record->id;
    }
    return !apply(step);
}

bool RichTextDocument::applyParagraphFormatStep(const StepParagraphFormat &step, QString *changeId)
{
    if (step.before == step.after) return true;
    if (m_trackChanges) {
        // A paragraph with a pending format change keeps its first former look.
        bool pending = false;
        for (const TrackedChange &change : std::as_const(m_annotations.changes))
            for (const ParagraphFormatChange &entry : change.paragraphs)
                pending = pending || entry.paragraph == step.paragraph;
        if (!pending) {
            std::optional<TrackedChange> stored;
            if (!changeId->isEmpty()) stored = m_annotations.changes.value(*changeId);
            TrackedChange record = stored ? *stored
                : TrackedChange{newAnnotationId(), ChangeKind::Format, m_reviewAuthor, reviewTime(), {}, {}, {}, {}, {}};
            record.paragraphs.append({step.paragraph, step.before});
            if (apply(StepAnnotation{record.id, stored ? std::optional<Annotation>(*stored) : std::nullopt,
                                     Annotation(record)}))
                return false;
            *changeId = record.id;
        }
    }
    return !apply(step);
}

bool RichTextDocument::restoreFormats(const TrackedChange &change)
{
    for (Paragraph *p : paragraphsInOrder()) {
        const bool ok = rewriteFormats(*this, *p, 0, int(p->text.size()), [this, &change](CharFormat &f) {
            if (f.formatChangeId != change.id) return;
            CharFormat former = change.formats.value(f.formatSlot, f.withoutMarks());
            const NamedStyle *named = former.styleId.isEmpty() ? nullptr : style(former.styleId);
            if (!former.styleId.isEmpty() && (!named || named->kind != StyleKind::Character)) former.styleId.clear();
            former.objectId = f.objectId;
            former.fieldId = f.fieldId;
            former.commentIds = f.commentIds;
            former.insertionId = f.insertionId;
            former.deletionId = f.deletionId;
            f = former;
        });
        if (!ok) return false;
    }
    for (const ParagraphFormatChange &entry : change.paragraphs) {
        Paragraph *p = paragraph(entry.paragraph);
        if (!p) continue; // the paragraph was removed since
        ParagraphFormat before = entry.before;
        // The former look may name a list or style removed since; those go.
        if (!before.listId.isEmpty() && !list(before.listId)) before.listId.clear(), before.listLevel = 0;
        if (!before.styleId.isEmpty() && !style(before.styleId)) before.styleId.clear(), before.overrides = 0;
        if (p->table) before.pageBreakBefore = false;
        if (!(before == p->format) && apply(StepParagraphFormat{p->id, p->format, before})) return false;
    }
    return true;
}

bool RichTextDocument::setMarkFormat(Paragraph *p, int format)
{
    if (p->markFormat == format) return true;
    StepSpans step;
    step.paragraph = p->id;
    step.markBefore = p->markFormat;
    step.markAfter = format;
    return !apply(step);
}

bool RichTextDocument::joinWithNext(Paragraph *p)
{
    Paragraph *next = nextParagraph(p);
    if (!next || !(containerOf(next) == containerOf(p))) {
        // The last paragraph of a body or cell has no break to remove.
        CharFormat mark = charFormat(p->markFormat);
        mark.insertionId.clear();
        mark.deletionId.clear();
        return setMarkFormat(p, internFormat(mark));
    }
    // The joined paragraph ends with the next paragraph's break.
    return setMarkFormat(p, next->markFormat) && !apply(StepMerge{p->id, paragraphPtr(next), -1});
}

bool RichTextDocument::markParagraphBreak(Paragraph *p, const QString &insertionId)
{
    CharFormat mark = charFormat(p->markFormat);
    mark.insertionId = insertionId;
    mark.deletionId.clear();
    return setMarkFormat(p, internFormat(mark));
}

namespace {

QVariantMap locationOf(const QVariant &item)
{
    return item.toMap().value(QStringLiteral("location")).toMap();
}

} // namespace

QVariantMap RichTextDocument::reviewItemLocation(const QString &itemId) const
{
    for (const QVariant &item : reviewItems())
        if (item.toMap().value(QStringLiteral("id")).toString() == itemId) return locationOf(item);
    return {};
}

QString RichTextDocument::adjacentReviewItem(const QString &paragraphId, int offset, bool forward,
                                             const QString &kind) const
{
    const Position at{QUuid(paragraphId), offset};
    if (!isValidPosition(at)) return {};
    // reviewItems is in document order with orphans last; orphans have no place to go.
    QStringList ids;
    QVector<Position> starts;
    for (const QVariant &value : reviewItems()) {
        const QVariantMap item = value.toMap();
        const QVariantMap location = locationOf(value);
        if (location.isEmpty() || (!kind.isEmpty() && item.value(QStringLiteral("kind")).toString() != kind)) continue;
        const Position start{QUuid(location.value(QStringLiteral("paragraphId")).toString()),
                             location.value(QStringLiteral("offset")).toInt()};
        if (!isValidPosition(start)) continue;
        ids.append(item.value(QStringLiteral("id")).toString());
        starts.append(start);
    }
    if (ids.isEmpty()) return {};
    if (forward) {
        for (qsizetype i = 0; i < ids.size(); ++i)
            if (compare(starts.at(i), at) > 0) return ids.at(i);
        return ids.first(); // wraps around
    }
    for (qsizetype i = ids.size() - 1; i >= 0; --i)
        if (compare(starts.at(i), at) < 0) return ids.at(i);
    return ids.last();
}

} // namespace QindaTK
