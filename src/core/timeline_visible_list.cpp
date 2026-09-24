// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_visible_list.h"

#include <QSet>

namespace QindaTK {

TimelineVisibleList::TimelineVisibleList(const QList<QByteArray> &roles, QObject *parent)
    : QAbstractListModel(parent), m_roles(roles)
{
}

int TimelineVisibleList::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant TimelineVisibleList::data(const QModelIndex &index, int role) const
{
    const int column = role - Qt::UserRole;
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || column < 0
        || column >= m_roles.size())
        return {};
    return m_rows.at(index.row()).at(column);
}

QHash<int, QByteArray> TimelineVisibleList::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_roles.size(); ++i) names.insert(Qt::UserRole + i, m_roles.at(i));
    return names;
}

void TimelineVisibleList::update(QVector<QVariantList> next)
{
    QHash<QString, int> nextIndex;
    nextIndex.reserve(next.size());
    for (int i = 0; i < next.size(); ++i) {
        next[i].resize(m_roles.size());
        nextIndex.insert(next.at(i).first().toString(), i);
    }
    const qsizetype before = m_rows.size();
    // Remove departed rows from the end, one contiguous run at a time.
    int row = int(m_rows.size()) - 1;
    while (row >= 0) {
        if (nextIndex.contains(m_rows.at(row).first().toString())) {
            --row;
            continue;
        }
        int first = row;
        while (first > 0 && !nextIndex.contains(m_rows.at(first - 1).first().toString())) --first;
        beginRemoveRows({}, first, row);
        m_rows.remove(first, row - first + 1);
        endRemoveRows();
        m_removed += quint64(row - first + 1);
        row = first - 1;
    }
    // Update retained rows in place, naming only the roles that changed.
    QSet<QString> retained;
    for (int i = 0; i < m_rows.size(); ++i) {
        const QString key = m_rows.at(i).first().toString();
        retained.insert(key);
        const QVariantList &incoming = next.at(nextIndex.value(key));
        QList<int> changed;
        for (int column = 1; column < m_roles.size(); ++column) {
            if (m_rows.at(i).at(column) == incoming.at(column)) continue;
            m_rows[i][column] = incoming.at(column);
            changed.push_back(Qt::UserRole + column);
        }
        if (!changed.isEmpty()) emit dataChanged(index(i), index(i), changed);
    }
    // Append entering rows in the caller's order.
    QVector<QVariantList> entering;
    for (const auto &candidate : next)
        if (!retained.contains(candidate.first().toString())) entering.push_back(candidate);
    if (!entering.isEmpty()) {
        const int first = int(m_rows.size());
        beginInsertRows({}, first, first + int(entering.size()) - 1);
        m_rows += entering;
        endInsertRows();
        m_inserted += quint64(entering.size());
    }
    m_index.clear();
    for (int i = 0; i < m_rows.size(); ++i) m_index.insert(m_rows.at(i).first().toString(), i);
    if (before != m_rows.size()) emit countChanged();
    if (!entering.isEmpty() || before != m_rows.size() - entering.size()) emit statisticsChanged();
}

QVariantMap TimelineVisibleList::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size()) return map;
    for (int column = 0; column < m_roles.size(); ++column)
        map.insert(QString::fromLatin1(m_roles.at(column)), m_rows.at(row).at(column));
    return map;
}

int TimelineVisibleList::indexOf(const QString &key) const
{
    return m_index.value(key, -1);
}

QStringList TimelineVisibleList::keys() const
{
    QStringList result;
    result.reserve(m_rows.size());
    for (const auto &row : m_rows) result.push_back(row.first().toString());
    return result;
}

} // namespace QindaTK
