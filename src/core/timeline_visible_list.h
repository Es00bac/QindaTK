// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-NOTE: the timeline's delegate source. update() diffs the next visible
// set against the current rows by `key` (the first role): departed rows are
// removed, retained rows keep their row and only emit dataChanged for roles
// whose value changed, and entering rows are appended. A Repeater over this
// model therefore creates delegates only for clips that scroll into view and
// never rebuilds the ones that stay; rows never carry document authority.
class TimelineVisibleList : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TimelineVisibleList is owned by TimelineViewport")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString insertedTotal READ insertedTotalText NOTIFY statisticsChanged)
    Q_PROPERTY(QString removedTotal READ removedTotalText NOTIFY statisticsChanged)
public:
    explicit TimelineVisibleList(const QList<QByteArray> &roles, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }
    QString insertedTotalText() const { return QString::number(m_inserted); }
    QString removedTotalText() const { return QString::number(m_removed); }
    void update(QVector<QVariantList> next);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString &key) const;
    Q_INVOKABLE QStringList keys() const;

signals:
    void countChanged();
    void statisticsChanged();

private:
    QList<QByteArray> m_roles;
    QVector<QVariantList> m_rows;
    QHash<QString, int> m_index;
    quint64 m_inserted = 0;
    quint64 m_removed = 0;
};

} // namespace QindaTK
