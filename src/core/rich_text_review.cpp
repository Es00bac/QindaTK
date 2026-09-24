// SPDX-License-Identifier: LGPL-3.0-or-later
// Annotation records of the rich-text model (WP-052) and their result-text rules.
#include "rich_text_review.h"

#include <QCoreApplication>
#include <QSet>
#include <QStringList>

namespace QindaTK::RichText {

bool Annotations::contains(const QString &id) const
{
    return fields.contains(id) || sources.contains(id) || threads.contains(id) || changes.contains(id);
}

std::optional<Annotation> Annotations::find(const QString &id) const
{
    if (const auto it = fields.constFind(id); it != fields.constEnd()) return Annotation(*it);
    if (const auto it = sources.constFind(id); it != sources.constEnd()) return Annotation(*it);
    if (const auto it = threads.constFind(id); it != threads.constEnd()) return Annotation(*it);
    if (const auto it = changes.constFind(id); it != changes.constEnd()) return Annotation(*it);
    return std::nullopt;
}

void Annotations::set(const QString &id, const std::optional<Annotation> &value)
{
    fields.remove(id);
    sources.remove(id);
    threads.remove(id);
    changes.remove(id);
    if (!value) return;
    std::visit([&](const auto &record) {
        using T = std::decay_t<decltype(record)>;
        if constexpr (std::is_same_v<T, ReferenceField>) fields.insert(id, record);
        else if constexpr (std::is_same_v<T, ReferenceSource>) sources.insert(id, record);
        else if constexpr (std::is_same_v<T, CommentThread>) threads.insert(id, record);
        else changes.insert(id, record);
    }, *value);
}

QString annotationId(const Annotation &annotation)
{
    return std::visit([](const auto &record) { return record.id; }, annotation);
}

namespace {
const QStringList &fieldKindNames()
{
    static const QStringList names{QStringLiteral("sequence"), QStringLiteral("crossReference"),
                                   QStringLiteral("citation"), QStringLiteral("bibliography")};
    return names;
}
const QStringList &changeKindNames()
{
    static const QStringList names{QStringLiteral("insertion"), QStringLiteral("deletion")};
    return names;
}
QString tr(const char *text) { return QCoreApplication::translate("QindaTK::RichTextDocument", text); }
} // namespace

QString fieldKindName(FieldKind kind) { return fieldKindNames().at(int(kind)); }

std::optional<FieldKind> fieldKindFromName(const QString &name)
{
    const qsizetype index = fieldKindNames().indexOf(name);
    return index < 0 ? std::nullopt : std::optional<FieldKind>(FieldKind(index));
}

QString changeKindName(ChangeKind kind) { return changeKindNames().at(int(kind)); }

std::optional<ChangeKind> changeKindFromName(const QString &name)
{
    const qsizetype index = changeKindNames().indexOf(name);
    return index < 0 ? std::nullopt : std::optional<ChangeKind>(ChangeKind(index));
}

bool validSequenceName(const QString &name)
{
    if (name.isEmpty() || name.size() > 32 || !name.at(0).isLetter() || name.endsWith(u' ')) return false;
    for (const QChar c : name)
        if (!c.isLetterOrNumber() && c != u' ') return false;
    return true;
}

QString sequenceResult(const QString &sequence, int number)
{
    // AGENT-NOTE: equations are numbered "(n)" at the right of their line,
    // as in print; every other series reads "<Name> n".
    if (sequence == u"Equation") return QStringLiteral("(%1)").arg(number);
    return QStringLiteral("%1 %2").arg(sequence).arg(number);
}

QString brokenReferenceResult() { return QStringLiteral("[?]"); }

QString citationResult(int number) { return QStringLiteral("[%1]").arg(number); }

QString bibliographyEntry(const ReferenceSource &source, int number)
{
    QStringList parts;
    if (!source.author.trimmed().isEmpty()) parts.append(source.author.trimmed() + u'.');
    if (!source.title.trimmed().isEmpty()) parts.append(source.title.trimmed() + u'.');
    QString where = source.publisher.trimmed();
    if (!source.year.trimmed().isEmpty()) where += (where.isEmpty() ? QString() : QStringLiteral(", ")) + source.year.trimmed();
    if (!where.isEmpty()) parts.append(where + u'.');
    if (parts.isEmpty()) parts.append(tr("Untitled source."));
    return QStringLiteral("[%1] %2").arg(number).arg(parts.join(u' '));
}

QString emptyBibliographyResult() { return tr("No sources cited."); }

QString annotationsProblem(const Annotations &a)
{
    if (a.size() > MaximumAnnotations) return QStringLiteral("Too many annotations");
    QSet<QString> seen;
    auto unique = [&seen](const QString &id) {
        if (id.isEmpty() || seen.contains(id)) return false;
        seen.insert(id);
        return true;
    };
    for (auto it = a.fields.cbegin(); it != a.fields.cend(); ++it) {
        if (it.key() != it->id || !unique(it->id)) return QStringLiteral("Invalid or duplicate field identifier");
        if (it->kind == FieldKind::Sequence && !validSequenceName(it->sequence))
            return QStringLiteral("Invalid sequence name");
    }
    for (auto it = a.sources.cbegin(); it != a.sources.cend(); ++it)
        if (it.key() != it->id || !unique(it->id)) return QStringLiteral("Invalid or duplicate source identifier");
    for (auto it = a.threads.cbegin(); it != a.threads.cend(); ++it) {
        if (it.key() != it->id || !unique(it->id)) return QStringLiteral("Invalid or duplicate comment identifier");
        for (const CommentReply &reply : it->replies)
            if (!unique(reply.id)) return QStringLiteral("Invalid or duplicate reply identifier");
    }
    for (auto it = a.changes.cbegin(); it != a.changes.cend(); ++it)
        if (it.key() != it->id || !unique(it->id)) return QStringLiteral("Invalid or duplicate change identifier");
    return {};
}

} // namespace QindaTK::RichText
