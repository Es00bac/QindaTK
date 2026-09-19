// SPDX-License-Identifier: LGPL-3.0-or-later
#include "graph_raster.h"

#include "graph.h"

#include <QPainter>
#include <QPen>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>

namespace QindaTK {
namespace GraphRaster {

namespace {

using GraphMesh::Plot;

void paintGrid(QPainter *painter, const Graph &graph, const Plot &plot)
{
    const QColor color = graph.gridColor();
    if (!color.isValid() || color.alpha() == 0) {
        return;
    }
    painter->setPen(QPen(color, 1.0));
    const int rows = graph.gridRows();
    for (int r = 1; r <= rows; ++r) {
        const qreal y = plot.height * qreal(r) / qreal(rows + 1);
        painter->drawLine(QPointF(0, y), QPointF(plot.width, y));
    }
    const int columns = graph.gridColumns();
    for (int c = 1; c <= columns; ++c) {
        const qreal x = plot.width * qreal(c) / qreal(columns + 1);
        painter->drawLine(QPointF(x, 0), QPointF(x, plot.height));
    }
}

// AGENT-GUARD: the columns are painted with antialiasing OFF and the
// silhouette comes from a clip, not from the column edges. Antialiased
// adjacent polygons blend at every shared edge, which showed up as vertical
// banding across the whole fill; abutting aliased rects do not.
void paintFill(QPainter *painter, const GraphSeries &series, const Plot &plot)
{
    const int samples = series.count();
    const qreal base = GraphMesh::baseline(plot);

    QPolygonF silhouette;
    silhouette.reserve(samples + 2);
    silhouette << QPointF(GraphMesh::sampleX(plot, 0, samples), base);
    for (int i = 0; i < samples; ++i) {
        const qreal t = GraphMesh::normalised(plot, series.at(i));
        silhouette << QPointF(GraphMesh::sampleX(plot, i, samples), GraphMesh::sampleY(plot, t));
    }
    silhouette << QPointF(GraphMesh::sampleX(plot, samples - 1, samples), base);

    painter->save();
    painter->setClipPath([&silhouette] {
        QPainterPath path;
        path.addPolygon(silhouette);
        path.closeSubpath();
        return path;
    }());
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(Qt::NoPen);

    for (int i = 0; i + 1 < samples; ++i) {
        const qreal t0 = GraphMesh::normalised(plot, series.at(i));
        const qreal t1 = GraphMesh::normalised(plot, series.at(i + 1));
        const qreal x0 = GraphMesh::sampleX(plot, i, samples);
        const qreal x1 = GraphMesh::sampleX(plot, i + 1, samples);
        const qreal peak = GraphMesh::sampleY(plot, std::max(t0, t1));

        QColor top = GraphMesh::sampleColor(series, std::max(t0, t1));
        QColor bottom = top;
        top.setAlphaF(float(top.alphaF() * series.fillOpacity()));
        bottom.setAlphaF(float(bottom.alphaF() * series.fillOpacity() * 0.12));

        QLinearGradient gradient(0, peak, 0, base);
        gradient.setColorAt(0, top);
        gradient.setColorAt(1, bottom);
        painter->setBrush(gradient);
        // Full-height columns: the clip supplies the trace's outline, so the
        // rects only have to carry colour.
        painter->drawRect(QRectF(QPointF(x0, std::min(base, qreal(0))),
                                 QPointF(x1, std::max(base, plot.height))));
    }
    painter->restore();
}

void paintLine(QPainter *painter, const GraphSeries &series, const Plot &plot)
{
    const int samples = series.count();
    if (series.lineWidth() <= 0) {
        return;
    }
    painter->setBrush(Qt::NoBrush);
    for (int i = 0; i + 1 < samples; ++i) {
        const qreal t0 = GraphMesh::normalised(plot, series.at(i));
        const qreal t1 = GraphMesh::normalised(plot, series.at(i + 1));
        QPen pen(GraphMesh::sampleColor(series, (t0 + t1) / 2.0), series.lineWidth());
        pen.setCapStyle(Qt::RoundCap);
        painter->setPen(pen);
        painter->drawLine(QPointF(GraphMesh::sampleX(plot, i, samples), GraphMesh::sampleY(plot, t0)),
                          QPointF(GraphMesh::sampleX(plot, i + 1, samples),
                                  GraphMesh::sampleY(plot, t1)));
    }
}

} // namespace

void paint(QPainter *painter, const Graph &graph, const Plot &plot)
{
    if (plot.width <= 0 || plot.height <= 0) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    paintGrid(painter, graph, plot);

    for (const GraphSeries *series : graph.seriesItems()) {
        if (!series->visible() || series->count() < 2) {
            continue;
        }
        if (series->fill() && series->fillOpacity() > 0) {
            paintFill(painter, *series, plot);
        }
        paintLine(painter, *series, plot);
    }

    const QColor baselineColor = graph.baselineColor();
    if (baselineColor.isValid() && baselineColor.alpha() > 0) {
        const qreal y = GraphMesh::baseline(plot);
        const qreal inset = plot.mirrored ? 0.5 : -0.5;
        painter->setPen(QPen(baselineColor, 1.0));
        painter->drawLine(QPointF(0, y + inset), QPointF(plot.width, y + inset));
    }
}

} // namespace GraphRaster
} // namespace QindaTK
