// SPDX-License-Identifier: LGPL-3.0-or-later
// Reference fields of RichTextDocument (WP-052): captions (numbered series),
// cross-references, citations and bibliographies. A field is a run of text
// whose format names it; its record says what it refers to. The result text
// is regenerated inside every committing transaction (reconcileFields), so a
// renumbering is part of the edit that caused it and undoes with it (D-115).
#include "rich_text_document.h"
#include "rich_text_fields_p.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

#include <algorithm>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

namespace RichText::Fields {

Scan scan(const RichTextDocument &doc)
{
    Scan out;
    const Annotations &a = doc.annotations();
    if (a.fields.isEmpty()) return out;
    for (const Paragraph *p : doc.paragraphsInOrder()) {
        int position = 0;
        int start = 0;
        QString current;
        auto close = [&](int end) {
            if (current.isEmpty()) return;
            const Placement placement{p, start, end - start};
            if (out.placed.contains(current)) {
                out.duplicates.append(placement);
            } else {
                out.placed.insert(current, placement);
                out.order.append(current);
            }
            current.clear();
        };
        for (const Span &span : p->spans) {
            const QString &id = doc.charFormat(span.format).fieldId;
            // A mark naming no record is plain text (the codec drops such marks).
            const QString known = a.fields.contains(id) ? id : QString();
            if (known != current) {
                close(position);
                current = known;
                start = position;
            }
            position += span.length;
        }
        close(position);
    }
    return out;
}

QHash<QString, Result> results(const RichTextDocument &doc, const Scan &scan)
{
    const Annotations &a = doc.annotations();
    QHash<QString, Result> out;
    QHash<QString, int> counters;
    QHash<QString, int> ranks;
    QStringList cited;
    auto broken = [](const QString &error) { return Result{brokenReferenceResult(), error, 0, true}; };
    // Series number in document order; sources ranked by their first citation.
    for (const QString &id : scan.order) {
        const ReferenceField field = a.fields.value(id);
        if (field.kind == FieldKind::Sequence) {
            const int number = ++counters[field.sequence];
            out.insert(id, Result{sequenceResult(field.sequence, number), {}, number, false});
        } else if (field.kind == FieldKind::Citation && a.sources.contains(field.target)
                   && !ranks.contains(field.target)) {
            cited.append(field.target);
            ranks.insert(field.target, int(cited.size()));
        }
    }
    for (const QString &id : scan.order) {
        const ReferenceField field = a.fields.value(id);
        Result result;
        switch (field.kind) {
        case FieldKind::Sequence:
            continue;
        case FieldKind::CrossReference: {
            // AGENT-GUARD (OFF-10): a reference whose target is gone reports
            // it; it never falls back to another caption.
            const auto target = a.fields.constFind(field.target);
            if (target == a.fields.constEnd())
                result = broken(RichTextDocument::tr("The target was deleted."));
            else if (target->kind != FieldKind::Sequence)
                result = broken(RichTextDocument::tr("The target is not a numbered caption."));
            else if (!scan.placed.contains(field.target))
                result = broken(RichTextDocument::tr("The target caption was removed from the text."));
            else
                result = out.value(field.target);
            break;
        }
        case FieldKind::Citation:
            if (!a.sources.contains(field.target)) {
                result = broken(RichTextDocument::tr("The cited source was removed."));
            } else {
                result.number = ranks.value(field.target);
                result.text = citationResult(result.number);
            }
            break;
        case FieldKind::Bibliography: {
            QStringList entries;
            for (int i = 0; i < cited.size(); ++i) entries.append(bibliographyEntry(a.sources.value(cited.at(i)), i + 1));
            result.text = entries.isEmpty() ? emptyBibliographyResult() : entries.join(QChar(LineBreakChar));
            break;
        }
        }
        out.insert(id, result);
    }
    return out;
}

const QStringList &typeNames()
{
    static const QStringList names{QStringLiteral("caption"), QStringLiteral("cross-reference"),
                                   QStringLiteral("citation"), QStringLiteral("bibliography")};
    return names;
}

std::optional<FieldKind> kindFromType(const QString &type)
{
    const qsizetype index = typeNames().indexOf(type);
    return index < 0 ? std::nullopt : std::optional<FieldKind>(FieldKind(index));
}

QString targetProblem(const RichTextDocument &doc, ReferenceField *field, const QString &targetId)
{
    field->sequence.clear();
    field->target.clear();
    switch (field->kind) {
    case FieldKind::Sequence: {
        const QString name = (targetId.startsWith(u"sequence:") ? targetId.mid(9) : targetId).trimmed();
        if (!validSequenceName(name)) return RichTextDocument::tr("A caption needs a series name such as Figure.");
        field->sequence = name;
        return {};
    }
    case FieldKind::CrossReference: {
        const auto target = doc.annotations().fields.constFind(targetId);
        if (target == doc.annotations().fields.constEnd() || target->kind != FieldKind::Sequence)
            return RichTextDocument::tr("A cross-reference points at a caption number.");
        field->target = targetId;
        return {};
    }
    case FieldKind::Citation:
        if (!doc.annotations().sources.contains(targetId)) return RichTextDocument::tr("A citation points at a source.");
        field->target = targetId;
        return {};
    case FieldKind::Bibliography:
        return {};
    }
    return {};
}

QString targetIdOf(const ReferenceField &field)
{
    return field.kind == FieldKind::Sequence ? QStringLiteral("sequence:") + field.sequence : field.target;
}

QString sourceLabel(const ReferenceSource &source)
{
    QStringList parts;
    for (const QString &part : {source.author, source.year, source.title})
        if (!part.trimmed().isEmpty()) parts.append(part.trimmed());
    return parts.join(QStringLiteral(" · "));
}

} // namespace RichText::Fields

bool RichTextDocument::reconcileFields()
{
    if (m_annotations.fields.isEmpty()) return true;
    const Fields::Scan found = Fields::scan(*this);
    if (found.order.isEmpty()) return true;
    const QHash<QString, Fields::Result> expected = Fields::results(*this, found);
    struct Edit {
        const Paragraph *paragraph;
        int offset;
        int length;
        QString text;
        bool strip;
    };
    QVector<Edit> edits;
    // A copied run of a placed field is plain text: one field, one place.
    for (const Fields::Placement &copy : found.duplicates) edits.append({copy.paragraph, copy.offset, copy.length, {}, true});
    for (const QString &id : found.order) {
        const Fields::Placement at = found.placed.value(id);
        const QString text = expected.value(id).text;
        if (QStringView(at.paragraph->text).mid(at.offset, at.length) != text)
            edits.append({at.paragraph, at.offset, at.length, text, false});
    }
    // Last first, so the offsets of the edits still to come hold.
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) {
        return a.paragraph->flatIndex != b.paragraph->flatIndex ? a.paragraph->flatIndex > b.paragraph->flatIndex
                                                                : a.offset > b.offset;
    });
    for (const Edit &edit : std::as_const(edits)) {
        if (edit.strip) {
            if (!rewriteFormats(*this, *edit.paragraph, edit.offset, edit.offset + edit.length,
                                [](CharFormat &f) { f.fieldId.clear(); }))
                return false;
            continue;
        }
        // The result keeps the look of the field's first character.
        const int format = Spans::formatAt(edit.paragraph->spans, edit.offset);
        if (apply(textStep(*this, *edit.paragraph, edit.offset, edit.length, edit.text,
                           {{int(edit.text.size()), format}})))
            return false;
    }
    return true;
}

std::optional<Position> RichTextDocument::insertField(const Position &requested, ReferenceField field)
{
    if (!isValidPosition(requested)) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid reference position"));
        return std::nullopt;
    }
    const Position at = outsideField(requested);
    if (field.id.isEmpty()) field.id = newAnnotationId();
    AutoTransaction transaction(this, tr("Insert reference"));
    if (!transaction.valid) return std::nullopt;
    const bool owned = transaction.owned;
    CharFormat format = charFormat(formatIndexAt(at));
    const std::optional<QString> insertion = trackedInsertionId(at);
    bool ok = insertion.has_value() && !apply(StepAnnotation{field.id, std::nullopt, Annotation(field)});
    if (ok) {
        format.fieldId = field.id;
        format.insertionId = *insertion;
        // A placeholder: the commit writes the field's result text.
        ok = !apply(textStep(*this, *paragraph(at.paragraph), at.offset, 0, QStringLiteral("?"),
                             {{1, internFormat(format)}}));
    }
    const Position caret{at.paragraph, at.offset + 1};
    if (!transaction.finish(ok, caret)) return std::nullopt;
    if (!owned) return caret; // the owner's commit maps registered anchors past the result
    const Fields::Scan found = Fields::scan(*this);
    const auto placed = found.placed.constFind(field.id);
    if (placed == found.placed.constEnd()) return caret;
    return Position{placed->paragraph->id, placed->offset + placed->length};
}

QString RichTextDocument::insertReference(const QString &paragraphId, int offset, const QString &type,
                                          const QString &targetId)
{
    const std::optional<FieldKind> kind = Fields::kindFromType(type);
    if (!kind) {
        // AGENT-NOTE: footnotes need a note area at the page foot, which the
        // paged layout does not model (docs/references-review.md#scope).
        reject(ErrorCode::Unsupported, type == u"footnote"
                   ? tr("Footnotes are not supported: pages have no note area.")
                   : tr("Unsupported reference type: %1").arg(type));
        return {};
    }
    ReferenceField field;
    field.id = newAnnotationId();
    field.kind = *kind;
    if (const QString problem = Fields::targetProblem(*this, &field, targetId); !problem.isEmpty()) {
        reject(ErrorCode::InvalidArgument, problem);
        return {};
    }
    return insertField({QUuid(paragraphId), offset}, field) ? field.id : QString();
}

bool RichTextDocument::editReference(const QString &referenceId, const QVariantMap &patch)
{
    const auto found = m_annotations.fields.constFind(referenceId);
    if (found == m_annotations.fields.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown reference"));
    ReferenceField next = *found;
    if (patch.contains(QStringLiteral("label"))) next.label = patch.value(QStringLiteral("label")).toString().trimmed();
    if (patch.contains(QStringLiteral("type"))) {
        const std::optional<FieldKind> kind = Fields::kindFromType(patch.value(QStringLiteral("type")).toString());
        if (!kind) return !reject(ErrorCode::Unsupported, tr("Unsupported reference type: %1").arg(patch.value(QStringLiteral("type")).toString()));
        if (*kind != next.kind && (*kind == FieldKind::Bibliography || next.kind == FieldKind::Bibliography))
            return !reject(ErrorCode::Unsupported, tr("A bibliography cannot become another kind of reference."));
        next.kind = *kind;
    }
    if (patch.contains(QStringLiteral("targetId")) || next.kind != found->kind) {
        const QString target = patch.value(QStringLiteral("targetId"), Fields::targetIdOf(*found)).toString();
        if (const QString problem = Fields::targetProblem(*this, &next, target); !problem.isEmpty())
            return !reject(ErrorCode::InvalidArgument, problem);
    }
    if (next == *found) return true;
    AutoTransaction transaction(this, tr("Edit reference"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepAnnotation{referenceId, Annotation(*found), Annotation(next)}), {});
}

bool RichTextDocument::removeReference(const QString &referenceId)
{
    const Fields::Scan found = Fields::scan(*this);
    const auto placed = found.placed.constFind(referenceId);
    if (placed == found.placed.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("The reference is not in the text"));
    const Position start{placed->paragraph->id, placed->offset};
    const Position end{placed->paragraph->id, placed->offset + placed->length};
    AutoTransaction transaction(this, tr("Remove reference"));
    if (!transaction.valid) return false;
    // Through deleteRange, so a tracked document marks it deleted instead.
    return transaction.finish(deleteRange(start, end).has_value(), start);
}

namespace {

QString sourceProblem(const ReferenceSource &source)
{
    if (source.author.trimmed().isEmpty() && source.title.trimmed().isEmpty())
        return RichTextDocument::tr("A source needs an author or a title.");
    for (const QString &value : {source.author, source.title, source.year, source.publisher})
        if (value.size() > 1000) return RichTextDocument::tr("A source member is longer than 1000 characters.");
    return {};
}

void patchSource(ReferenceSource *source, const QVariantMap &patch)
{
    const QList<std::pair<const char *, QString *>> members{
        {"author", &source->author}, {"title", &source->title},
        {"year", &source->year}, {"publisher", &source->publisher}};
    for (const auto &[key, value] : members)
        if (patch.contains(QLatin1String(key))) *value = patch.value(QLatin1String(key)).toString().trimmed();
}

} // namespace

QString RichTextDocument::addSource(const QVariantMap &source)
{
    ReferenceSource created;
    const QString requested = source.value(QStringLiteral("id")).toString();
    created.id = requested.isEmpty() || m_annotations.contains(requested) ? newAnnotationId() : requested;
    patchSource(&created, source);
    if (const QString problem = sourceProblem(created); !problem.isEmpty()) {
        reject(ErrorCode::InvalidArgument, problem);
        return {};
    }
    AutoTransaction transaction(this, tr("Add source"));
    if (!transaction.valid) return {};
    if (!transaction.finish(!apply(StepAnnotation{created.id, std::nullopt, Annotation(created)}), {})) return {};
    return created.id;
}

bool RichTextDocument::updateSource(const QString &sourceId, const QVariantMap &patch)
{
    const auto found = m_annotations.sources.constFind(sourceId);
    if (found == m_annotations.sources.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown source"));
    ReferenceSource next = *found;
    patchSource(&next, patch);
    if (const QString problem = sourceProblem(next); !problem.isEmpty())
        return !reject(ErrorCode::InvalidArgument, problem);
    if (next == *found) return true;
    AutoTransaction transaction(this, tr("Edit source"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepAnnotation{sourceId, Annotation(*found), Annotation(next)}), {});
}

bool RichTextDocument::removeSource(const QString &sourceId)
{
    const auto found = m_annotations.sources.constFind(sourceId);
    if (found == m_annotations.sources.constEnd())
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown source"));
    // Its citations become broken ("[?]") in the same transaction; undo restores them.
    AutoTransaction transaction(this, tr("Remove source"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepAnnotation{sourceId, Annotation(*found), std::nullopt}), {});
}

bool RichTextDocument::updateFields()
{
    AutoTransaction transaction(this, tr("Update fields"));
    if (!transaction.valid) return false;
    return transaction.finish(true, {}); // the commit rewrites every stale result
}

} // namespace QindaTK
