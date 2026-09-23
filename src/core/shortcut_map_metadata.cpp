// SPDX-License-Identifier: LGPL-3.0-or-later
#include "shortcut_map.h"

namespace QindaTK {
namespace {
QKeySequence proposedSequence(const QString &text)
{
    const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    return sequence.isEmpty() || sequence.toString(QKeySequence::PortableText).isEmpty()
        ? QKeySequence{} : sequence;
}

bool overlaps(const QKeySequence &first, const QKeySequence &second)
{
    return first.matches(second) != QKeySequence::NoMatch
        || second.matches(first) != QKeySequence::NoMatch;
}
} // namespace

QVariantList ShortcutMap::bindings() const
{
    QVariantList rows;
    for (const auto &entry : m_bindings) {
        rows.append(QVariantMap{
            {QStringLiteral("commandId"), entry.commandId},
            {QStringLiteral("scopeToken"), entry.scopeToken},
            {QStringLiteral("active"), active(entry)},
            {QStringLiteral("defaultSequence"), entry.defaultSequence.toString(QKeySequence::PortableText)},
            {QStringLiteral("overrideSequence"), entry.hasOverride
                 ? entry.overrideSequence.toString(QKeySequence::PortableText) : QString{}},
            {QStringLiteral("effectiveSequence"), entry.effective().toString(QKeySequence::PortableText)},
            {QStringLiteral("allowInTextInput"), entry.allowInTextInput}});
    }
    return rows;
}

QVariantMap ShortcutMap::validateOverrides(const QVariantMap &values) const
{
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        bool known = false;
        for (const auto &entry : m_bindings) {
            if (entry.commandId == it.key()) { known = true; break; }
        }
        if (!known || it.value().metaType().id() != QMetaType::QString
            || (!it.value().toString().isEmpty()
                && proposedSequence(it.value().toString()).isEmpty())) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("conflicts"), QVariantList{}},
                    {QStringLiteral("error"), QStringLiteral("Unknown command or invalid portable shortcut: %1").arg(it.key())}};
        }
    }

    QVariantList conflicts;
    for (qsizetype i = 0; i < m_bindings.size(); ++i) {
        const auto &first = m_bindings[i];
        if (!active(first)) continue;
        const auto firstText = values.value(first.commandId).toString();
        const auto firstSequence = values.contains(first.commandId)
            ? (firstText.isEmpty() ? QKeySequence{} : proposedSequence(firstText))
            : first.defaultSequence;
        if (firstSequence.isEmpty()) continue;
        for (qsizetype j = i + 1; j < m_bindings.size(); ++j) {
            const auto &second = m_bindings[j];
            if (!active(second) || rank(first) != rank(second)) continue;
            const auto secondText = values.value(second.commandId).toString();
            const auto secondSequence = values.contains(second.commandId)
                ? (secondText.isEmpty() ? QKeySequence{} : proposedSequence(secondText))
                : second.defaultSequence;
            if (secondSequence.isEmpty() || !overlaps(firstSequence, secondSequence)) continue;
            conflicts.append(QVariantMap{
                {QStringLiteral("first"), first.commandId},
                {QStringLiteral("second"), second.commandId},
                {QStringLiteral("sequence"), firstSequence.toString(QKeySequence::PortableText)},
                {QStringLiteral("otherSequence"), secondSequence.toString(QKeySequence::PortableText)}});
        }
    }
    return {{QStringLiteral("ok"), conflicts.isEmpty()},
            {QStringLiteral("conflicts"), conflicts},
            {QStringLiteral("error"), conflicts.isEmpty() ? QString{}
                : QStringLiteral("Shortcut conflict in the active scope")}};
}
} // namespace QindaTK
