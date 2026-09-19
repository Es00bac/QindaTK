// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QtGlobal>

class QSGGeometry;

namespace QindaTK {

class GraphSeries;

// AGENT-CONTRACT: the geometry half of Graph, split out so graph.cpp stays
// about properties and node lifetime. Every builder writes
// QSGGeometry::ColoredPoint2D vertices with PREMULTIPLIED colour, because
// QSGVertexColorMaterial expects that; a straight copy of a QColor's bytes
// makes translucent fills glow.
namespace GraphMesh {

// The mapping from sample space to item space for one draw.
struct Plot final {
    qreal width = 0;
    qreal height = 0;
    qreal minValue = 0;
    qreal maxValue = 1;
    // Mirrored graphs hang from the top edge, for a send series drawn under
    // a receive series.
    bool mirrored = false;
};

// ---- the sample mapping, shared with the raster twin --------------------

// Magnitude of `value` as a fraction of the plotted band, clamped to [0, 1].
[[nodiscard]] qreal normalised(const Plot &plot, qreal value);
// Item-space x of sample `index` of `samples`; oldest sits at the left edge.
[[nodiscard]] qreal sampleX(const Plot &plot, int index, int samples);
// Item-space y of a normalised magnitude.
[[nodiscard]] qreal sampleY(const Plot &plot, qreal t);
// Item-space y of the graph's zero line.
[[nodiscard]] qreal baseline(const Plot &plot);
// The series' colour at that magnitude: its ramp when set, else its colour.
[[nodiscard]] QColor sampleColor(const GraphSeries &series, qreal t);

// Vertices needed for the given sample count; 0 when there is nothing to
// draw. Callers must allocate exactly this before the matching build call.
[[nodiscard]] int fillVertexCount(int samples);
[[nodiscard]] int lineVertexCount(int samples);
[[nodiscard]] int gridVertexCount(int rows, int columns);

// Triangle strip from the baseline to each sample, coloured by the series'
// ramp at that sample's own magnitude and fading towards the baseline.
void buildFill(QSGGeometry *geometry, const GraphSeries &series, const Plot &plot);

// Triangle strip tracing the sample line at `series.lineWidth()`, offset
// along the true segment normals so steep slopes keep an even weight.
void buildLine(QSGGeometry *geometry, const GraphSeries &series, const Plot &plot);

// Evenly spaced rules; `rows` horizontal and `columns` vertical, excluding
// the four borders.
void buildGrid(QSGGeometry *geometry, const Plot &plot, int rows, int columns,
               const QColor &color);

// A single horizontal rule along the graph's zero line.
void buildBaseline(QSGGeometry *geometry, const Plot &plot, const QColor &color);

} // namespace GraphMesh

} // namespace QindaTK
