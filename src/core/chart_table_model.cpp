// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_table_model.h"

namespace QindaTK {

QHash<int, QByteArray> ChartTableModel::roleNames() const
{
    return {{SeriesIdRole, "seriesId"}, {SeriesNameRole, "seriesName"},
        {PointIdRole, "pointId"}, {XRole, "x"}, {YRole, "y"},
        {SizeRole, "size"}, {DxRole, "dx"}, {DyRole, "dy"},
        {CategoryRole, "category"}, {XUnitRole, "xUnit"},
        {YUnitRole, "yUnit"}, {GapRole, "gap"}};
}

QVariantMap ChartTableModel::get(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    const auto &r = m_rows.at(row);
    return {{QStringLiteral("seriesId"), r.seriesId}, {QStringLiteral("seriesName"), r.seriesName},
        {QStringLiteral("pointId"), r.pointId}, {QStringLiteral("x"), r.x},
        {QStringLiteral("y"), r.y}, {QStringLiteral("size"), r.size},
        {QStringLiteral("dx"), r.dx}, {QStringLiteral("dy"), r.dy},
        {QStringLiteral("category"), r.category},
        {QStringLiteral("xUnit"), r.xUnit}, {QStringLiteral("yUnit"), r.yUnit},
        {QStringLiteral("gap"), r.gap}};
}

QVariant ChartTableModel::data(const QModelIndex &index, int role) const
{
    const auto row = get(index.row());
    if (!index.isValid() || row.isEmpty()) return {};
    const auto names = roleNames();
    const auto it = names.constFind(role);
    return it == names.cend() ? QVariant() : row.value(QString::fromLatin1(*it));
}

void ChartTableModel::reset(const QVector<ChartSeries *> &series, const ChartAxis &xAxis,
                            const ChartHistogramResult *histogram)
{
    beginResetModel(); m_rows = ChartData::table(series, xAxis);
    if (histogram && histogram->valid()) {
        const QString yUnit = histogram->normalization == int(ChartHistogram::Probability)
            ? QStringLiteral("probability") : histogram->normalization == int(ChartHistogram::Density)
            ? QStringLiteral("density") : QStringLiteral("count");
        for (qsizetype i = 0; i < histogram->bins.size(); ++i) {
            const auto &bin = histogram->bins.at(i);
            ChartTableRow row;
            row.seriesId = QStringLiteral("histogram"); row.seriesName = histogram->channel;
            row.pointId = QStringLiteral("bin-%1").arg(i);
            row.x = QString::number(bin.lower, 'g', 17); row.category = QString::number(bin.upper, 'g', 17);
            row.y = QString::number(bin.displayed, 'g', 17); row.xUnit = histogram->unit; row.yUnit = yUnit;
            m_rows.append(std::move(row));
        }
    }
    endResetModel(); emit countChanged();
}

void ChartTableModel::clear()
{
    if (m_rows.isEmpty()) return;
    beginResetModel(); m_rows.clear(); endResetModel(); emit countChanged();
}

} // namespace QindaTK
