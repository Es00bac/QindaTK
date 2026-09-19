// SPDX-License-Identifier: LGPL-3.0-or-later
#include "graph_mesh.h"

#include "graph.h"
#include "theme_ramp.h"

#include <QSGGeometry>

#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace GraphMesh {

namespace {

using Vertex = QSGGeometry::ColoredPoint2D;

// AGENT-GUARD: QSGVertexColorMaterial blends premultiplied vertices. Writing
// a QColor's channels unscaled makes every translucent fill read brighter
// than its alpha asks for, worst at the fade to baseline.
void put(Vertex *v, qreal x, qreal y, const QColor &color, qreal alphaScale)
{
    const qreal a = std::clamp(color.alphaF() * alphaScale, qreal(0), qreal(1));
    v->set(float(x), float(y), uchar(std::lround(color.red() * a)),
           uchar(std::lround(color.green() * a)), uchar(std::lround(color.blue() * a)),
           uchar(std::lround(a * 255.0)));
}

} // namespace

qreal normalised(const Plot &plot, qreal value)
{
    const qreal span = plot.maxValue - plot.minValue;
    if (!(span > 0) || !std::isfinite(value)) {
        return 0;
    }
    return std::clamp((value - plot.minValue) / span, qreal(0), qreal(1));
}

qreal sampleX(const Plot &plot, int index, int samples)
{
    if (samples < 2) {
        return plot.width;
    }
    return plot.width * qreal(index) / qreal(samples - 1);
}

// Upright graphs grow towards the top edge; mirrored ones hang from it.
qreal sampleY(const Plot &plot, qreal t)
{
    return plot.mirrored ? plot.height * t : plot.height * (1.0 - t);
}

qreal baseline(const Plot &plot)
{
    return plot.mirrored ? 0.0 : plot.height;
}

QColor sampleColor(const GraphSeries &series, qreal t)
{
    if (series.ramp() != nullptr) {
        const QColor ramped = series.ramp()->at(t);
        if (ramped.isValid()) {
            return ramped;
        }
    }
    return series.color();
}

int fillVertexCount(int samples)
{
    return samples >= 2 ? samples * 2 : 0;
}

int lineVertexCount(int samples)
{
    return samples >= 2 ? samples * 2 : 0;
}

int gridVertexCount(int rows, int columns)
{
    return (std::max(0, rows) + std::max(0, columns)) * 2;
}

void buildFill(QSGGeometry *geometry, const GraphSeries &series, const Plot &plot)
{
    const int samples = series.count();
    if (fillVertexCount(samples) == 0) {
        return;
    }
    Vertex *v = geometry->vertexDataAsColoredPoint2D();
    const qreal base = baseline(plot);
    for (int i = 0; i < samples; ++i) {
        const qreal t = normalised(plot, series.at(i));
        const QColor color = sampleColor(series, t);
        const qreal x = sampleX(plot, i, samples);
        // Pair order is (baseline, sample) so the strip stays a ribbon.
        put(v++, x, base, color, series.fillOpacity() * 0.12);
        put(v++, x, sampleY(plot, t), color, series.fillOpacity());
    }
}

void buildLine(QSGGeometry *geometry, const GraphSeries &series, const Plot &plot)
{
    const int samples = series.count();
    if (lineVertexCount(samples) == 0) {
        return;
    }
    Vertex *v = geometry->vertexDataAsColoredPoint2D();
    const qreal half = std::max(qreal(0.5), series.lineWidth() / 2.0);

    const std::size_t points = std::size_t(samples);
    std::vector<qreal> xs(points, 0.0);
    std::vector<qreal> ys(points, 0.0);
    std::vector<qreal> ts(points, 0.0);
    for (int i = 0; i < samples; ++i) {
        ts[std::size_t(i)] = normalised(plot, series.at(i));
        xs[std::size_t(i)] = sampleX(plot, i, samples);
        ys[std::size_t(i)] = sampleY(plot, ts[std::size_t(i)]);
    }

    for (int i = 0; i < samples; ++i) {
        // Average the normals of the segments meeting at this point so a
        // corner keeps its weight instead of pinching.
        qreal nx = 0;
        qreal ny = 0;
        if (i > 0) {
            const qreal dx = xs[std::size_t(i)] - xs[std::size_t(i - 1)];
            const qreal dy = ys[std::size_t(i)] - ys[std::size_t(i - 1)];
            const qreal len = std::hypot(dx, dy);
            if (len > 0) {
                nx += -dy / len;
                ny += dx / len;
            }
        }
        if (i + 1 < samples) {
            const qreal dx = xs[std::size_t(i + 1)] - xs[std::size_t(i)];
            const qreal dy = ys[std::size_t(i + 1)] - ys[std::size_t(i)];
            const qreal len = std::hypot(dx, dy);
            if (len > 0) {
                nx += -dy / len;
                ny += dx / len;
            }
        }
        const qreal len = std::hypot(nx, ny);
        if (len > 0) {
            nx /= len;
            ny /= len;
        } else {
            nx = 0;
            ny = 1;
        }
        const QColor color = sampleColor(series, ts[std::size_t(i)]);
        put(v++, xs[std::size_t(i)] - nx * half, ys[std::size_t(i)] - ny * half, color, 1.0);
        put(v++, xs[std::size_t(i)] + nx * half, ys[std::size_t(i)] + ny * half, color, 1.0);
    }
}

void buildGrid(QSGGeometry *geometry, const Plot &plot, int rows, int columns,
               const QColor &color)
{
    if (gridVertexCount(rows, columns) == 0) {
        return;
    }
    Vertex *v = geometry->vertexDataAsColoredPoint2D();
    for (int r = 1; r <= rows; ++r) {
        const qreal y = plot.height * qreal(r) / qreal(rows + 1);
        put(v++, 0, y, color, 1.0);
        put(v++, plot.width, y, color, 1.0);
    }
    for (int c = 1; c <= columns; ++c) {
        const qreal x = plot.width * qreal(c) / qreal(columns + 1);
        put(v++, x, 0, color, 1.0);
        put(v++, x, plot.height, color, 1.0);
    }
}

void buildBaseline(QSGGeometry *geometry, const Plot &plot, const QColor &color)
{
    Vertex *v = geometry->vertexDataAsColoredPoint2D();
    const qreal y = baseline(plot);
    // Half a pixel in keeps the rule inside the item at either edge.
    const qreal inset = plot.mirrored ? 0.5 : -0.5;
    put(v++, 0, y + inset, color, 1.0);
    put(v++, plot.width, y + inset, color, 1.0);
}

} // namespace GraphMesh
} // namespace QindaTK
