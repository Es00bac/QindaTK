// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_render.h"

#include <QColor>
#include <QSizeF>

class QQuickWindow;
class QSGNode;

namespace QindaTK::CanvasSceneGraph {

// Render-thread half of the rendering boundary. Builds or updates the node
// tree for `frame`. Every GPU handle (texture, geometry) is owned by a node
// in the returned tree, so Qt's own teardown releases it on the right thread.
// A frame whose resourceGeneration or backend differs from the tree's
// replaces the whole tree: no handle survives a reset.
QSGNode *sync(QSGNode *oldNode, const CanvasRenderFrame *frame, QQuickWindow *window,
              const QColor &canvasColor, const QSizeF &viewSize);

// Live root nodes and live tile textures in the process, for lifetime tests.
int liveRootNodes();
int liveTextures();

} // namespace QindaTK::CanvasSceneGraph
