// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "chart_data.h"
#include "chart_histogram.h"
#include <QAbstractListModel>

namespace QindaTK {

class ChartTableModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { SeriesIdRole = Qt::UserRole + 1, SeriesNameRole, PointIdRole,
                XRole, YRole, SizeRole, DxRole, DyRole, CategoryRole, XUnitRole, YUnitRole, GapRole };
    explicit ChartTableModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_rows.size(); }
    int count() const { return m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    void clear();
    void reset(const QVector<ChartSeries *> &series, const ChartAxis &xAxis,
               const ChartHistogramResult *histogram = nullptr);
signals:
    void countChanged();
private:
    QVector<ChartTableRow> m_rows;
};

} // namespace QindaTK
