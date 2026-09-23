// SPDX-License-Identifier: LGPL-3.0-or-later
#include "selection_model.h"

#include <algorithm>
#include <QSet>
#include <QThread>

namespace QindaTK {

SelectionModel::SelectionModel(QObject *parent) : QObject(parent) {}

std::optional<EntityId> SelectionModel::parseId(const QString &text)
{
    const QUuid uuid(text);
    if (uuid.isNull()) return std::nullopt;
    return EntityId{uuid};
}

std::optional<QVector<EntityId>> SelectionModel::parseUniqueIds(const QStringList &ids)
{
    QVector<EntityId> parsed;
    parsed.reserve(ids.size());
    QSet<QUuid> seen;
    for (const auto &text : ids) {
        const auto id = parseId(text);
        if (!id || seen.contains(id->value)) return std::nullopt;
        seen.insert(id->value);
        parsed.push_back(*id);
    }
    return parsed;
}

QStringList SelectionModel::selectedIds() const
{
    QStringList result;
    result.reserve(m_selection.size());
    for (const auto &id : m_selection) result.push_back(id.toString());
    return result;
}

bool SelectionModel::onOwnerThread() const
{
    return QThread::currentThread() == thread();
}

void SelectionModel::publishModelChange(const QVector<EntityId> &selection,
                                        const EntityId &current, const EntityId &anchor,
                                        quint64 generationValue, bool notifyGeneration)
{
    const bool selectionDidChange = m_selection != selection;
    const bool currentDidChange = m_current != current;
    const bool anchorDidChange = m_anchor != anchor;
    const bool generationDidChange = m_generation != generationValue;
    m_selection = selection;
    m_current = current;
    m_anchor = anchor;
    m_generation = generationValue;
    if (selectionDidChange) emit selectionChanged();
    if (currentDidChange) emit currentChanged();
    if (anchorDidChange) emit anchorChanged();
    if (notifyGeneration && generationDidChange) emit generationChanged();
}

bool SelectionModel::publishUserChange(const QVector<EntityId> &selection,
                                       const EntityId &current, const EntityId &anchor)
{
    if (!onOwnerThread()) return false;
    const bool changed = m_selection != selection || m_current != current || m_anchor != anchor;
    if (!changed) return true;
    publishModelChange(selection, current, anchor, m_generation, false);
    emit selectionModified(selectedIds(), currentId(), anchorId());
    return true;
}

bool SelectionModel::select(const QString &text)
{
    if (!onOwnerThread()) return false;
    const auto id = parseId(text);
    if (!id) return false;
    return publishUserChange({*id}, *id, *id);
}

bool SelectionModel::toggle(const QString &text)
{
    if (!onOwnerThread()) return false;
    const auto id = parseId(text);
    if (!id) return false;
    QVector<EntityId> selection = m_selection;
    EntityId current = m_current;
    EntityId anchor = m_anchor;
    const auto it = std::find(selection.cbegin(), selection.cend(), *id);
    if (it == selection.cend()) {
        selection.push_back(*id);
        current = *id;
        if (!anchor.isValid()) anchor = *id;
    } else {
        const qsizetype removedIndex = std::distance(selection.cbegin(), it);
        selection.removeAt(removedIndex);
        if (current == *id) current = selection.isEmpty() ? EntityId{}
                                                          : selection.last();
        if (anchor == *id) anchor = current;
    }
    return publishUserChange(selection, current, anchor);
}

bool SelectionModel::extendTo(const QString &text, const QStringList &visibleIds)
{
    if (!onOwnerThread()) return false;
    const auto target = parseId(text);
    const auto visible = parseUniqueIds(visibleIds);
    if (!target || !visible || !m_anchor.isValid()) return false;
    const auto anchorIt = std::find(visible->cbegin(), visible->cend(), m_anchor);
    const auto targetIt = std::find(visible->cbegin(), visible->cend(), *target);
    if (anchorIt == visible->cend() || targetIt == visible->cend()) return false;
    const qsizetype a = std::distance(visible->cbegin(), anchorIt);
    const qsizetype b = std::distance(visible->cbegin(), targetIt);
    QVector<EntityId> selection;
    const qsizetype first = std::min(a, b);
    const qsizetype last = std::max(a, b);
    selection.reserve(last - first + 1);
    for (qsizetype i = first; i <= last; ++i) selection.push_back((*visible)[i]);
    return publishUserChange(selection, *target, m_anchor);
}

bool SelectionModel::clear()
{
    if (!onOwnerThread()) return false;
    return publishUserChange({}, {}, {});
}

bool SelectionModel::reconcileLiveIds(const QStringList &allModelIds)
{
    if (!onOwnerThread()) return false;
    const auto live = parseUniqueIds(allModelIds);
    if (!live) return false;
    QSet<QUuid> liveSet;
    for (const auto &id : *live) liveSet.insert(id.value);
    QVector<EntityId> selection;
    selection.reserve(m_selection.size());
    for (const auto &id : m_selection) if (liveSet.contains(id.value)) selection.push_back(id);
    const EntityId current = m_current.isValid() && liveSet.contains(m_current.value)
                                 ? m_current : EntityId{};
    const EntityId anchor = m_anchor.isValid() && liveSet.contains(m_anchor.value)
                                ? m_anchor : EntityId{};
    publishModelChange(selection, current, anchor, m_generation, false);
    return true;
}

bool SelectionModel::resetModel(const QStringList &allModelIds,
                                const QString &generationText)
{
    if (!onOwnerThread()) return false;
    const auto live = parseUniqueIds(allModelIds);
    bool ok = false;
    const quint64 nextGeneration = generationText.toULongLong(&ok, 10);
    bool digitsOnly = !generationText.isEmpty();
    for (const QChar ch : generationText)
        digitsOnly = digitsOnly && ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
    if (!live || !ok || !digitsOnly || nextGeneration <= m_generation)
        return false;
    // AGENT-GUARD: a reset invalidates row/delegate references, not stable
    // entity identity. Keep only IDs that the replacement model still owns.
    QSet<QUuid> liveSet;
    for (const auto &id : *live) liveSet.insert(id.value);
    QVector<EntityId> selection;
    for (const auto &id : m_selection)
        if (liveSet.contains(id.value)) selection.push_back(id);
    const EntityId current = m_current.isValid() && liveSet.contains(m_current.value)
                                 ? m_current : EntityId{};
    const EntityId anchor = m_anchor.isValid() && liveSet.contains(m_anchor.value)
                                ? m_anchor : EntityId{};
    publishModelChange(selection, current, anchor, nextGeneration, true);
    return true;
}

} // namespace QindaTK
