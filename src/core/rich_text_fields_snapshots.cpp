// SPDX-License-Identifier: LGPL-3.0-or-later
// QML snapshots of the reference fields and sources (WP-052), in the shapes
// Tk.ReferenceManager takes (docs/controls.md, docs/references-review.md).
#include "rich_text_document.h"
#include "rich_text_fields_p.h"

#include <algorithm>

namespace QindaTK {
using namespace RichText;

namespace {

QVariantMap locationMap(const Fields::Placement &at)
{
    return {{QStringLiteral("paragraphId"), at.paragraph->id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("offset"), at.offset},
            {QStringLiteral("length"), at.length}};
}

// The caption's own words: its paragraph without the number, trimmed.
QString captionExcerpt(const Fields::Placement &at)
{
    QString text = at.paragraph->text.left(at.offset) + u' ' + at.paragraph->text.mid(at.offset + at.length);
    text.replace(QChar(ObjectChar), u' ');
    text = text.simplified();
    while (!text.isEmpty() && (text.front() == u':' || text.front() == u'.' || text.front() == u'-'))
        text = text.mid(1).trimmed();
    return text.size() > 48 ? text.left(47) + QChar(0x2026) : text;
}

QList<ReferenceSource> sortedSources(const Annotations &a)
{
    QList<ReferenceSource> sources = a.sources.values();
    std::sort(sources.begin(), sources.end(), [](const ReferenceSource &x, const ReferenceSource &y) {
        const int author = QString::compare(x.author, y.author, Qt::CaseInsensitive);
        if (author != 0) return author < 0;
        const int title = QString::compare(x.title, y.title, Qt::CaseInsensitive);
        return title != 0 ? title < 0 : x.id < y.id;
    });
    return sources;
}

} // namespace

QStringList RichTextDocument::referenceTypes() const
{
    return Fields::typeNames();
}

QVariantList RichTextDocument::referenceSnapshots() const
{
    const Fields::Scan found = Fields::scan(*this);
    const QHash<QString, Fields::Result> expected = Fields::results(*this, found);
    QVariantList list;
    for (const QString &id : found.order) {
        const ReferenceField field = m_annotations.fields.value(id);
        const Fields::Placement at = found.placed.value(id);
        const Fields::Result result = expected.value(id);
        const QString current = at.paragraph->text.mid(at.offset, at.length);
        QString label = field.label;
        if (label.isEmpty()) {
            switch (field.kind) {
            case FieldKind::Sequence: label = field.sequence; break;
            case FieldKind::CrossReference: label = tr("Reference to %1").arg(result.text); break;
            case FieldKind::Citation:
                label = m_annotations.sources.contains(field.target)
                    ? Fields::sourceLabel(m_annotations.sources.value(field.target)) : tr("Missing source");
                break;
            case FieldKind::Bibliography: label = tr("Bibliography"); break;
            case FieldKind::Footnote:
            case FieldKind::Endnote: {
                const QString kind = field.kind == FieldKind::Footnote ? tr("Footnote") : tr("Endnote");
                const QString text = field.text.simplified();
                label = kind + QStringLiteral(" · ") + (text.size() > 40 ? text.left(39) + QChar(0x2026) : text);
                break;
            }
            }
        }
        // "stale" only after loading a file whose results were edited by hand;
        // any committed edit rewrites them.
        const QString status = result.broken ? QStringLiteral("broken")
            : current != result.text ? QStringLiteral("stale") : QStringLiteral("resolved");
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("type"), Fields::typeNames().at(int(field.kind))},
                                {QStringLiteral("label"), label},
                                {QStringLiteral("targetId"), Fields::targetIdOf(field)},
                                {QStringLiteral("sequence"), field.sequence},
                                {QStringLiteral("number"), result.number},
                                {QStringLiteral("status"), status},
                                {QStringLiteral("formattedText"), current},
                                {QStringLiteral("expectedText"), result.text},
                                {QStringLiteral("error"), result.error},
                                {QStringLiteral("noteText"), field.text},
                                {QStringLiteral("location"), locationMap(at)}});
    }
    return list;
}

QVariantList RichTextDocument::referenceTargets() const
{
    const Fields::Scan found = Fields::scan(*this);
    const QHash<QString, Fields::Result> expected = Fields::results(*this, found);
    QVariantList list;
    QStringList series{QStringLiteral("Figure"), QStringLiteral("Table"), QStringLiteral("Equation")};
    for (const QString &id : found.order) {
        const ReferenceField field = m_annotations.fields.value(id);
        if (field.kind != FieldKind::Sequence) continue;
        if (!series.contains(field.sequence)) series.append(field.sequence);
        const Fields::Placement at = found.placed.value(id);
        const QString excerpt = captionExcerpt(at);
        const QString number = expected.value(id).text;
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("type"), QStringLiteral("caption")},
                                {QStringLiteral("label"), excerpt.isEmpty() ? number : number + QStringLiteral(" · ") + excerpt},
                                {QStringLiteral("location"), locationMap(at)}});
    }
    for (const ReferenceSource &source : sortedSources(m_annotations))
        list.append(QVariantMap{{QStringLiteral("id"), source.id},
                                {QStringLiteral("type"), QStringLiteral("source")},
                                {QStringLiteral("label"), Fields::sourceLabel(source)}});
    for (const QString &name : std::as_const(series))
        list.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("sequence:") + name},
                                {QStringLiteral("type"), QStringLiteral("sequence")},
                                {QStringLiteral("label"), tr("New %1 number").arg(name)}});
    return list;
}

QVariantList RichTextDocument::sourceSnapshots() const
{
    const Fields::Scan found = Fields::scan(*this);
    const QHash<QString, Fields::Result> expected = Fields::results(*this, found);
    QHash<QString, int> numbers;
    for (const QString &id : found.order) {
        const ReferenceField field = m_annotations.fields.value(id);
        if (field.kind == FieldKind::Citation && !numbers.contains(field.target))
            numbers.insert(field.target, expected.value(id).number);
    }
    QVariantList list;
    for (const ReferenceSource &source : sortedSources(m_annotations))
        list.append(QVariantMap{{QStringLiteral("id"), source.id},
                                {QStringLiteral("author"), source.author},
                                {QStringLiteral("title"), source.title},
                                {QStringLiteral("year"), source.year},
                                {QStringLiteral("publisher"), source.publisher},
                                {QStringLiteral("label"), Fields::sourceLabel(source)},
                                {QStringLiteral("number"), numbers.value(source.id)},
                                {QStringLiteral("cited"), numbers.contains(source.id)}});
    return list;
}

} // namespace QindaTK
