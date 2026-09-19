// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "graph_mesh.h"

class QPainter;

namespace QindaTK {

class Graph;

// AGENT-CONTRACT: the QPainter twin of GraphMesh, used only when the scene
// graph is running Qt's software adaptation, which draws none of the custom
// QSGGeometryNodes GraphMesh builds. Both paths consume the same Plot and the
// same sample mapping, so a graph looks the same either way.
//
// AGENT-GUARD: a change to the look of a graph must land in BOTH
// graph_mesh.cpp and graph_raster.cpp. Touching one alone makes the app
// disagree with `qtk-preview --grab`, which renders through this path.
namespace GraphRaster {

// Draws the whole graph -- grid, every visible series, then the baseline --
// into `painter`, whose origin is the graph's top-left corner.
void paint(QPainter *painter, const Graph &graph, const GraphMesh::Plot &plot);

} // namespace GraphRaster

} // namespace QindaTK
