// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_mixer_strip_model.h"

#include <QSet>

namespace QindaTK {

QList<QByteArray> AudioMixerStripModel::roles()
{
    // AGENT-CONTRACT: MixerView's delegate declares these as required
    // properties; AudioMixerModel::stripRow() fills them in this order.
    return {"channelId", "channelName", "channelKind", "layout", "output", "outputLabel",
            "outputAvailable", "muted", "soloed", "armed", "soloSafe", "implicitlyMuted",
            "effectiveMuted", "groupNames", "sends", "held", "hidden", "color"};
}

AudioMixerStripModel::AudioMixerStripModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int AudioMixerStripModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant AudioMixerStripModel::data(const QModelIndex &index, int role) const
{
    const int column = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= m_rows.size() || column < 0) return {};
    const QVariantList &row = m_rows.at(index.row());
    return column < row.size() ? row.at(column) : QVariant();
}

QHash<int, QByteArray> AudioMixerStripModel::roleNames() const
{
    QHash<int, QByteArray> names;
    const QList<QByteArray> list = roles();
    for (int i = 0; i < list.size(); ++i) names.insert(Qt::UserRole + i, list.at(i));
    return names;
}

void AudioMixerStripModel::update(const QVector<QVariantList> &next)
{
    const int before = int(m_rows.size());
    QSet<QString> keep;
    for (const QVariantList &row : next) keep.insert(row.value(0).toString());
    for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
        if (keep.contains(m_rows.at(i).value(0).toString())) continue;
        beginRemoveRows({}, i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }
    for (int i = 0; i < int(next.size()); ++i) {
        const QString key = next.at(i).value(0).toString();
        int at = -1;
        for (int j = i; j < int(m_rows.size()); ++j)
            if (m_rows.at(j).value(0).toString() == key) {
                at = j;
                break;
            }
        if (at < 0) {
            beginInsertRows({}, i, i);
            m_rows.insert(i, next.at(i));
            endInsertRows();
            continue;
        }
        if (at != i) {
            // AGENT-GUARD: a move, never remove+insert, so the delegate (and a
            // gesture it holds) survives a track reorder.
            beginMoveRows({}, at, at, {}, i);
            m_rows.move(at, i);
            endMoveRows();
        }
        QList<int> changed;
        const QVariantList &target = next.at(i);
        for (int column = 0; column < int(target.size()); ++column)
            if (m_rows.at(i).value(column) != target.at(column)) changed.push_back(Qt::UserRole + column);
        if (!changed.isEmpty()) {
            m_rows[i] = target;
            emit dataChanged(index(i), index(i), changed);
        }
    }
    if (before != m_rows.size()) emit countChanged();
}

int AudioMixerStripModel::indexOf(const QString &channelId) const
{
    for (int i = 0; i < int(m_rows.size()); ++i)
        if (m_rows.at(i).value(0).toString() == channelId) return i;
    return -1;
}

QString AudioMixerStripModel::channelAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).value(0).toString() : QString();
}

QVariantMap AudioMixerStripModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size()) return map;
    const QList<QByteArray> names = roles();
    const QVariantList &values = m_rows.at(row);
    for (int i = 0; i < names.size() && i < values.size(); ++i) map.insert(QString::fromLatin1(names.at(i)), values.at(i));
    return map;
}

QStringList AudioMixerStripModel::channelIds() const
{
    QStringList ids;
    for (const QVariantList &row : m_rows) ids.push_back(row.value(0).toString());
    return ids;
}

} // namespace QindaTK
