// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_data.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
QString keyFor(const ChartPoint &p, const ChartAxis &axis)
{
    if (axis.kind() == ChartAxis::Category) return p.category;
    if (axis.kind() == ChartAxis::DateTime) return QString::number(p.utcMs);
    return QString::number(p.x, 'g', 17);
}
double xFor(const ChartPoint &p, const ChartAxis &axis)
{
    if (axis.kind() == ChartAxis::DateTime) return axis.mapUtc(p.utcMs);
    if (axis.kind() == ChartAxis::Category) return axis.map(axis.categoryIndex(p.category));
    return axis.map(p.x);
}
bool stacked(ChartSeries::Kind kind)
{
    return kind == ChartSeries::StackedArea || kind == ChartSeries::StackedBar;
}
bool bar(ChartSeries::Kind kind)
{
    return kind == ChartSeries::Bar || kind == ChartSeries::StackedBar || kind == ChartSeries::Waterfall;
}
QString csvField(const QString &value)
{
    QString copy = value;
    copy.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"") + copy + QStringLiteral("\"");
}
}

ChartPlot ChartData::project(const QVector<ChartSeries *> &series,
                             const ChartAxis &xAxis, const ChartAxis &yAxis)
{
    ChartPlot plot;
    if (!xAxis.isValid()) { plot.error = QStringLiteral("X axis: ") + xAxis.error(); return plot; }
    if (!yAxis.isValid()) { plot.error = QStringLiteral("Y axis: ") + yAxis.error(); return plot; }
    if (yAxis.kind() == ChartAxis::Category || yAxis.kind() == ChartAxis::DateTime) {
        plot.error = QStringLiteral("Y axis must be numeric or logarithmic"); return plot;
    }
    QSet<QString> ids;
    QHash<QString, double> stackBaselines;
    int groupedBars = 0;
    for (const ChartSeries *s : series) {
        if (!s || !s->isVisible()) continue;
        if (s->id().isEmpty() || ids.contains(s->id())) {
            plot.error = QStringLiteral("Series IDs must be nonempty and unique"); return plot;
        }
        ids.insert(s->id());
        if (s->xUnit() != xAxis.unit() || s->yUnit() != yAxis.unit()) {
            plot.error = QStringLiteral("Series %1 units do not match axes").arg(s->id()); return plot;
        }
        if (bar(s->kind()) && xAxis.kind() != ChartAxis::Category) {
            plot.error = QStringLiteral("Bar and waterfall series require a category axis"); return plot;
        }
        if (s->kind() == ChartSeries::Vector && xAxis.kind() != ChartAxis::Numeric) {
            plot.error = QStringLiteral("Vector series require a numeric X axis"); return plot;
        }
        if (!std::isfinite(s->baseline())) {
            plot.error = QStringLiteral("Series baseline must be finite"); return plot;
        }
        if (stacked(s->kind())) {
            if (s->stackKey().isEmpty()) {
                plot.error = QStringLiteral("Stacked series require a stack key"); return plot;
            }
            const QString key = QString::number(int(s->kind())) + QChar(0x1f) + s->stackKey();
            if (stackBaselines.contains(key) && stackBaselines.value(key) != s->baseline()) {
                plot.error = QStringLiteral("Stack %1 has inconsistent baselines").arg(s->stackKey()); return plot;
            }
            stackBaselines.insert(key, s->baseline());
        }
        if (s->kind() == ChartSeries::Bar) ++groupedBars;
    }
    // Positive and negative accumulators are independent for each stack,
    // category/x value, and baseline. A gap never contributes to either.
    QHash<QString, double> positives;
    QHash<QString, double> negatives;
    int barIndex = 0;
    for (const ChartSeries *s : series) {
        if (!s || !s->isVisible()) continue;
        ChartProjectedSeries output;
        output.id = s->id(); output.kind = s->kind();
        double running = s->baseline();
        const int currentBarIndex = s->kind() == ChartSeries::Bar ? barIndex++ : 0;
        for (const ChartPoint &p : s->points()) {
            ChartProjectedPoint v;
            v.seriesId = s->id(); v.pointId = p.id; v.category = p.category;
            v.total = p.total; v.size = p.size; v.gap = p.gap;
            if (!p.gap) {
                v.x = xFor(p, xAxis);
                double base = s->baseline();
                double top = p.y;
                QString stackId;
                if (stacked(s->kind())) {
                    stackId = s->stackKey() + QChar(0x1f) + keyFor(p, xAxis);
                    // A stack's declared baseline is shared. Different
                    // baselines cannot silently be combined.
                    const bool positive = p.y >= 0;
                    auto &sum = positive ? positives : negatives;
                    const QString fullKey = QString::number(int(s->kind())) + QChar(0x1f) + stackId;
                    base = sum.value(fullKey, s->baseline());
                    top = base + p.y;
                    // Commit to the stack only when the plot coordinates
                    // remain valid (notably with logarithmic Y domains).
                    if (std::isfinite(yAxis.map(top)) && std::isfinite(yAxis.map(base)))
                        sum.insert(fullKey, top);
                } else if (s->kind() == ChartSeries::Waterfall) {
                    base = p.total ? s->baseline() : running;
                    top = p.total ? p.y : running + p.y;
                    running = top;
                }
                if (s->kind() == ChartSeries::Bar) {
                    const double band = 0.8 / xAxis.categories().size();
                    v.barWidth = band / groupedBars;
                    v.x += (currentBarIndex - (groupedBars - 1) / 2.0) * v.barWidth;
                } else if (bar(s->kind())) {
                    v.barWidth = 0.8 / xAxis.categories().size();
                }
                v.y = yAxis.map(top);
                const bool needsBaseline = s->kind() == ChartSeries::Area || stacked(s->kind()) || bar(s->kind());
                v.baselineY = needsBaseline ? yAxis.map(base) : v.y;
                v.endX = s->kind() == ChartSeries::Vector ? xAxis.map(p.x + p.dx) : v.x;
                v.endY = s->kind() == ChartSeries::Vector ? yAxis.map(p.y + p.dy) : v.y;
                if (!std::isfinite(v.x) || !std::isfinite(v.y)
                    || !std::isfinite(v.baselineY) || !std::isfinite(v.endX)
                    || !std::isfinite(v.endY)) {
                    v.gap = true; v.invalid = true;
                }
            }
            output.points.append(v);
        }
        plot.series.append(output);
    }
    return plot;
}

ChartDecimation ChartData::decimate(const QVector<ChartPoint> &points, int maxPoints)
{
    ChartDecimation result;
    if (maxPoints < 1) { result.error = QStringLiteral("Point budget must be positive"); return result; }
    if (points.size() <= maxPoints) { result.points = points; return result; }
    QSet<int> keep;
    keep.insert(0); keep.insert(points.size() - 1);
    int begin = 0;
    while (begin < points.size()) {
        if (points.at(begin).gap) { keep.insert(begin++); continue; }
        int end = begin;
        int low = begin, high = begin;
        while (end + 1 < points.size() && !points.at(end + 1).gap) {
            ++end;
            if (points.at(end).y < points.at(low).y) low = end;
            if (points.at(end).y > points.at(high).y) high = end;
        }
        keep.insert(begin); keep.insert(end); keep.insert(low); keep.insert(high);
        begin = end + 1;
    }
    if (keep.size() > maxPoints) {
        result.error = QStringLiteral("Budget cannot preserve gaps and run extrema");
        return result;
    }
    const int extra = maxPoints - keep.size();
    if (extra > 0) {
        for (int k = 1; k <= extra; ++k) {
            const int index = qBound(0, int((qint64(k) * (points.size() - 1)) / (extra + 1)), points.size() - 1);
            keep.insert(index);
        }
    }
    for (int i = 0; i < points.size(); ++i) if (keep.contains(i)) result.points.append(points.at(i));
    return result;
}

QVector<ChartTableRow> ChartData::table(const QVector<ChartSeries *> &series, const ChartAxis &xAxis)
{
    QVector<ChartTableRow> rows;
    for (const ChartSeries *s : series) {
        if (!s) continue;
        for (const ChartPoint &p : s->points()) {
            ChartTableRow row;
            row.seriesId = s->id(); row.seriesName = s->name(); row.pointId = p.id;
            row.x = xAxis.kind() == ChartAxis::DateTime ? QString::number(p.utcMs)
                    : xAxis.kind() == ChartAxis::Category ? p.category
                    : QString::number(p.x, 'g', 17);
            row.y = p.gap ? QString() : QString::number(p.y, 'g', 17);
            row.category = p.category; row.xUnit = s->xUnit(); row.yUnit = s->yUnit();
            row.gap = p.gap; rows.append(row);
        }
    }
    return rows;
}
QByteArray ChartData::csv(const QVector<ChartTableRow> &rows)
{
    QString output = QStringLiteral("series_id,series_name,point_id,x,y,category,x_unit,y_unit,gap\r\n");
    for (const ChartTableRow &r : rows) {
        const QStringList fields = {r.seriesId, r.seriesName, r.pointId, r.x, r.y,
                                    r.category, r.xUnit, r.yUnit, r.gap ? QStringLiteral("true") : QStringLiteral("false")};
        QStringList escaped;
        for (const QString &field : fields) escaped.append(csvField(field));
        output += escaped.join(',') + QStringLiteral("\r\n");
    }
    return output.toUtf8();
}

quint64 ChartHitIndex::cell(int x, int y)
{
    return (quint64(quint32(x)) << 32) | quint32(y);
}
void ChartHitIndex::rebuild(const ChartPlot &plot)
{
    m_points.clear(); m_cells.clear(); m_ids.clear();
    for (const ChartProjectedSeries &s : plot.series) {
        for (const ChartProjectedPoint &p : s.points) {
            if (p.gap) continue;
            const int index = m_points.size();
            m_points.append(p);
            m_ids.insert(p.seriesId + QChar(0x1f) + p.pointId, index);
            const auto boundedCell = [](double value) {
                return int(std::floor(std::clamp(value * 64, -1000000000.0, 1000000000.0)));
            };
            m_cells[cell(boundedCell(p.x), boundedCell(p.y))].append(index);
        }
    }
}
const ChartProjectedPoint *ChartHitIndex::find(const QString &seriesId, const QString &pointId) const
{
    const auto it = m_ids.constFind(seriesId + QChar(0x1f) + pointId);
    return it == m_ids.cend() ? nullptr : &m_points.at(*it);
}
ChartProjectedPoint ChartHitIndex::nearest(double x, double y, double radius) const
{
    ChartProjectedPoint empty;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || radius < 0) return empty;
    double best = radius * radius;
    bool found = false;
    if (radius > 2 || std::abs(x) > 1000000 || std::abs(y) > 1000000) {
        for (const ChartProjectedPoint &p : m_points) {
            const double d = std::pow(p.x - x, 2) + std::pow(p.y - y, 2);
            if (d <= best) { best = d; empty = p; found = true; }
        }
        if (!found) empty.pointId.clear();
        return empty;
    }
    const int x0 = int(std::floor((x - radius) * 64));
    const int x1 = int(std::floor((x + radius) * 64));
    const int y0 = int(std::floor((y - radius) * 64));
    const int y1 = int(std::floor((y + radius) * 64));
    // A broad inspection radius must not turn a sparse index query into an
    // unbounded grid walk. Scan the bounded retained points in that case.
    if (qint64(x1) - x0 > 128 || qint64(y1) - y0 > 128) {
        for (const ChartProjectedPoint &p : m_points) {
            const double d = std::pow(p.x - x, 2) + std::pow(p.y - y, 2);
            if (d <= best) { best = d; empty = p; found = true; }
        }
        if (!found) empty.pointId.clear();
        return empty;
    }
    for (int cx = x0; cx <= x1; ++cx) for (int cy = y0; cy <= y1; ++cy) {
        for (int index : m_cells.value(cell(cx, cy))) {
            const ChartProjectedPoint &p = m_points.at(index);
            const double d = std::pow(p.x - x, 2) + std::pow(p.y - y, 2);
            if (d <= best) { best = d; empty = p; found = true; }
        }
    }
    if (!found) empty.pointId.clear();
    return empty;
}
} // namespace QindaTK
