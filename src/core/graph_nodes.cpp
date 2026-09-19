// SPDX-License-Identifier: LGPL-3.0-or-later
// The rendering half of Graph: scene-graph nodes under a real GPU backend,
// and a painted texture under Qt's software adaptation. Split from graph.cpp,
// which owns properties and the series list.
#include "graph.h"

#include "graph_mesh.h"
#include "graph_raster.h"

#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>

namespace QindaTK {

namespace {

// One geometry node, reusing its buffer when the vertex count is unchanged.
QSGGeometryNode *takeNode(QSGNode *parent, int &cursor, int vertexCount,
                          QSGGeometry::DrawingMode mode)
{
    QSGGeometryNode *node = nullptr;
    if (cursor < parent->childCount()) {
        node = static_cast<QSGGeometryNode *>(parent->childAtIndex(cursor));
    } else {
        node = new QSGGeometryNode;
        node->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0));
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        parent->appendChildNode(node);
    }
    ++cursor;
    QSGGeometry *geometry = node->geometry();
    if (geometry->vertexCount() != vertexCount) {
        geometry->allocate(vertexCount);
    }
    geometry->setDrawingMode(mode);
    return node;
}

// AGENT-NOTE: Qt's software adaptation draws only the node types it knows;
// a custom QSGGeometryNode is silently skipped, which showed up as entirely
// blank graphs under `qtk-preview --grab` and on any llvmpipe machine. There
// the graph is painted into an image and presented as a texture instead.
bool softwareBackend(QQuickWindow *window)
{
    if (window == nullptr) {
        return false;
    }
    QSGRendererInterface *renderer = window->rendererInterface();
    return renderer != nullptr
        && renderer->graphicsApi() == QSGRendererInterface::Software;
}

} // namespace

QSGNode *Graph::updateRasterNode(QSGNode *oldNode, const GraphMesh::Plot &plot)
{
    auto *node = static_cast<QSGImageNode *>(oldNode);
    if (node == nullptr) {
        node = window()->createImageNode();
    }
    const qreal dpr = window()->effectiveDevicePixelRatio();
    QImage image(int(std::ceil(plot.width * dpr)), int(std::ceil(plot.height * dpr)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        GraphRaster::paint(&painter, *this, plot);
    }
    node->setTexture(window()->createTextureFromImage(image,
                                                      QQuickWindow::TextureHasAlphaChannel));
    node->setOwnsTexture(true);
    node->setRect(boundingRect());
    return node;
}

QSGNode *Graph::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    GraphMesh::Plot plot;
    plot.width = width();
    plot.height = height();
    plot.minValue = m_minValue;
    plot.maxValue = effectiveMax();
    plot.mirrored = m_mirrored;

    if (plot.width <= 0 || plot.height <= 0) {
        delete oldNode;
        return nullptr;
    }

    // The adaptation cannot change while a window is open, so the node built
    // for the other path is only ever discarded once.
    const bool raster = softwareBackend(window());
    if (raster != m_rasterNode) {
        delete oldNode;
        oldNode = nullptr;
        m_rasterNode = raster;
    }
    if (raster) {
        return updateRasterNode(oldNode, plot);
    }

    QSGNode *root = oldNode;
    if (root == nullptr) {
        root = new QSGNode;
    }

    int cursor = 0;
    const int gridVertices = GraphMesh::gridVertexCount(m_gridRows, m_gridColumns);
    if (gridVertices > 0 && m_gridColor.isValid() && m_gridColor.alpha() > 0) {
        QSGGeometryNode *node = takeNode(root, cursor, gridVertices, QSGGeometry::DrawLines);
        GraphMesh::buildGrid(node->geometry(), plot, m_gridRows, m_gridColumns, m_gridColor);
        node->markDirty(QSGNode::DirtyGeometry);
    }

    for (const GraphSeries *series : std::as_const(m_series)) {
        if (!series->visible() || series->count() < 2) {
            continue;
        }
        if (series->fill() && series->fillOpacity() > 0) {
            const int vertices = GraphMesh::fillVertexCount(series->count());
            QSGGeometryNode *node =
                takeNode(root, cursor, vertices, QSGGeometry::DrawTriangleStrip);
            GraphMesh::buildFill(node->geometry(), *series, plot);
            node->markDirty(QSGNode::DirtyGeometry);
        }
        if (series->lineWidth() > 0) {
            const int vertices = GraphMesh::lineVertexCount(series->count());
            QSGGeometryNode *node =
                takeNode(root, cursor, vertices, QSGGeometry::DrawTriangleStrip);
            GraphMesh::buildLine(node->geometry(), *series, plot);
            node->markDirty(QSGNode::DirtyGeometry);
        }
    }

    if (m_baselineColor.isValid() && m_baselineColor.alpha() > 0) {
        QSGGeometryNode *node = takeNode(root, cursor, 2, QSGGeometry::DrawLines);
        GraphMesh::buildBaseline(node->geometry(), plot, m_baselineColor);
        node->markDirty(QSGNode::DirtyGeometry);
    }

    // Nodes left from a previous frame with more series or more grid lines.
    while (root->childCount() > cursor) {
        delete root->childAtIndex(root->childCount() - 1);
    }
    return root;
}

} // namespace QindaTK
