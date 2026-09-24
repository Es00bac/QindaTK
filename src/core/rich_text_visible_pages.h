// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QRectF>
#include <QUuid>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// The pages (or continuous-flow slice) a RichTextArea currently shows: only
// visible entries exist, so delegates stay bounded by the viewport.
// Rows are keyed by page identity; scrolling updates geometry in place.
class RichTextVisiblePages : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { PageIndexRole = Qt::UserRole + 1, PageKeyRole, RevisionRole, TileRectRole,
                SourceRectRole, PageRectRole };
    struct Entry {
        int page = 0;
        QUuid key;
        quint64 revision = 0;
        QRectF tile;    // item coordinates of the painted tile
        QRectF source;  // page coordinates the tile shows
        QRectF pageRect; // item coordinates of the whole page
        friend bool operator==(const Entry &, const Entry &) = default;
    };
    explicit RichTextVisiblePages(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_entries.size()); }
    const QVector<Entry> &entries() const { return m_entries; }
    void update(const QVector<Entry> &entries);
signals:
    void countChanged();
private:
    QVector<Entry> m_entries;
};

} // namespace QindaTK
