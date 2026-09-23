// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "chart_axis.h"
#include "chart_series.h"

#include <QHash>
#include <QVector>

namespace QindaTK {

struct ChartProjectedPoint {
    QString seriesId;
    QString pointId;
    QString category;
    double x = 0;       // normalized position
    double y = 0;
    double baselineY = 0;
    double endX = 0;    // vector endpoint; otherwise x
    double endY = 0;    // vector endpoint; otherwise y
    double size = 0;    // unchanged data-unit bubble area
    double barWidth = 0; // normalized category-axis width
    bool gap = false;
    bool invalid = false; // e.g. non-positive value on a log axis
    bool total = false;
};

struct ChartProjectedSeries {
    QString id;
    ChartSeries::Kind kind = ChartSeries::Line;
    QVector<ChartProjectedPoint> points;
};

struct ChartPlot {
    QVector<ChartProjectedSeries> series;
    QString error;
    bool valid() const { return error.isEmpty(); }
};

struct ChartDecimation {
    QVector<ChartPoint> points;
    QString error;
    bool valid() const { return error.isEmpty(); }
};

struct ChartTableRow {
    QString seriesId;
    QString seriesName;
    QString pointId;
    QString x; // exact decimal UTC milliseconds for date axes
    QString y;
    QString size;
    QString dx;
    QString dy;
    QString category;
    QString xUnit;
    QString yUnit;
    bool gap = false;
};

// Pure model algorithms: input order is series and point order. None of these
// mutate source data, so table/export can always use full resolution.
class ChartData {
public:
    static ChartPlot project(const QVector<ChartSeries *> &series,
                             const ChartAxis &xAxis, const ChartAxis &yAxis);
    static ChartDecimation decimate(const QVector<ChartPoint> &points, int maxPoints);
    static QVector<ChartTableRow> table(const QVector<ChartSeries *> &series,
                                        const ChartAxis &xAxis);
    static QByteArray csv(const QVector<ChartTableRow> &rows);
};

// Grid index over projected points. Selection resolves both stable IDs.
class ChartHitIndex {
public:
    void rebuild(const ChartPlot &plot);
    ChartProjectedPoint nearest(double x, double y, double radius) const;
    const ChartProjectedPoint *find(const QString &seriesId, const QString &pointId) const;
    int count() const { return m_points.size(); }
private:
    static quint64 cell(int x, int y);
    QVector<ChartProjectedPoint> m_points;
    QHash<quint64, QVector<int>> m_cells;
    QHash<QString, int> m_ids;
};

} // namespace QindaTK
