// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_renderer.h"

namespace QindaTK {

QString ChartRenderer::csv() const
{
    QVector<ChartTableRow> rows;
    rows.reserve(m_tableModel.count());
    for (int i = 0; i < m_tableModel.count(); ++i) {
        const QVariantMap row = m_tableModel.get(i);
        ChartTableRow item;
        item.seriesId = row.value(QStringLiteral("seriesId")).toString();
        item.seriesName = row.value(QStringLiteral("seriesName")).toString();
        item.pointId = row.value(QStringLiteral("pointId")).toString();
        item.x = row.value(QStringLiteral("x")).toString();
        item.y = row.value(QStringLiteral("y")).toString();
        item.size = row.value(QStringLiteral("size")).toString();
        item.dx = row.value(QStringLiteral("dx")).toString();
        item.dy = row.value(QStringLiteral("dy")).toString();
        item.category = row.value(QStringLiteral("category")).toString();
        item.xUnit = row.value(QStringLiteral("xUnit")).toString();
        item.yUnit = row.value(QStringLiteral("yUnit")).toString();
        item.gap = row.value(QStringLiteral("gap")).toBool();
        rows.append(std::move(item));
    }
    return QString::fromUtf8(ChartData::csv(rows));
}

QVariantMap ChartRenderer::selectedPoint() const
{
    for (auto *d : m_datasets) {
        if (d && d->id() == m_selectedSeriesId) {
            for (int i = 0; i < d->rowCount(); ++i) {
                const QVariantMap row = d->get(i);
                if (row.value(QStringLiteral("pointId")).toString() == m_selectedPointId) return row;
            }
        }
    }
    return {};
}

} // namespace QindaTK
