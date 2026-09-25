// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-NOTE (WP-062, D-113): the MixerView delegate source (and, with its
// own roles, RoutingMatrix's input rows). update() diffs
// the next rows against the current ones by channel ID: departed rows are
// removed, retained rows MOVE (beginMoveRows) and emit dataChanged only for
// changed roles, entering rows are inserted in place. A reorder, a hidden
// channel or a mute therefore never rebuilds the strip under a gesture; rows
// carry no engine state (AudioMixerModel and the automation lanes do).
class AudioMixerStripModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AudioMixerStripModel is owned by AudioMixerModel")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    // Roles in row order; channelId is the key.
    static QList<QByteArray> roles();
    explicit AudioMixerStripModel(QObject *parent = nullptr);
    // Another row shape (RoutingMatrix inputs); the first role is the key.
    AudioMixerStripModel(const QList<QByteArray> &roles, QObject *parent);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }
    void update(const QVector<QVariantList> &next);
    Q_INVOKABLE int indexOf(const QString &channelId) const;
    Q_INVOKABLE QString channelAt(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QStringList channelIds() const;

signals:
    void countChanged();

private:
    QList<QByteArray> m_roles;
    QVector<QVariantList> m_rows;
};

} // namespace QindaTK
