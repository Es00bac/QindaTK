// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_surface_nodes.h"

#include <QHash>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace QindaTK::CanvasSceneGraph {

namespace {

std::atomic_int g_roots{0};
std::atomic_int g_textures{0};

static_assert(sizeof(CanvasVertex) == sizeof(QSGGeometry::ColoredPoint2D),
              "CanvasVertex must match the ColoredPoint2D attribute layout");

// AGENT-GUARD: RootNode and TileNode are identity QSGTransformNodes, not
// plain QSGNodes. Qt's software renderer records inherited transform state
// only for typed nodes; a subtree later added under a plain QSGNode starts
// from identity and drops the item's position (a view not at the window
// origin then drew its tiles at x = 0). tst_canvas_render::synchronizedViews.
//
// Owns the textures of one tile through its children; counts them so tests
// can prove that a reset leaves no texture behind.
class TileNode final : public QSGTransformNode {
public:
    ~TileNode() override { g_textures -= textures; }
    quint64 serial = 0;
    int textures = 0;
};

class RootNode final : public QSGTransformNode {
public:
    RootNode() { ++g_roots; }
    ~RootNode() override { --g_roots; }
    quint64 generation = 0;
    CanvasBackend backend = CanvasBackend::Software;
    QSGRectangleNode *background = nullptr;
    QSGTransformNode *transform = nullptr;
    QHash<CanvasTileKey, TileNode *> tiles;
};

QSGImageNode *imageNode(QQuickWindow *window, const QImage &image, const QRectF &rect,
                        TileNode *owner)
{
    QSGTexture *texture = window->createTextureFromImage(image, QQuickWindow::TextureHasAlphaChannel);
    if (!texture) {
        return nullptr;
    }
    QSGImageNode *node = window->createImageNode();
    node->setTexture(texture);
    node->setOwnsTexture(true);
    node->setFiltering(QSGTexture::Linear);
    node->setRect(rect);
    ++owner->textures;
    ++g_textures;
    return node;
}

TileNode *buildSoftware(const CanvasRenderItem &item, QQuickWindow *window)
{
    auto *tile = new TileNode;
    tile->serial = item.serial;
    if (QSGImageNode *node = imageNode(window, item.content->raster, item.documentRect, tile)) {
        tile->appendChildNode(node);
    }
    return tile;
}

TileNode *buildSceneGraph(const CanvasRenderItem &item, QQuickWindow *window)
{
    auto *tile = new TileNode;
    tile->serial = item.serial;
    // Objects spanning tiles are emitted into each; the clip keeps every tile
    // to its own rectangle so paint order stays correct across tile seams.
    auto *clip = new QSGClipNode;
    auto *clipGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
    QSGGeometry::updateRectGeometry(clipGeometry, item.documentRect);
    clip->setGeometry(clipGeometry);
    clip->setFlag(QSGNode::OwnsGeometry);
    clip->setIsRectangular(true);
    clip->setClipRect(item.documentRect);
    tile->appendChildNode(clip);
    for (const CanvasGeometryRun &run : item.content->runs) {
        if (run.kind == CanvasGeometryRun::Triangles) {
            // AGENT-GUARD: at most 65535 vertices per node. Larger nodes were
            // drawn with wrapped 16-bit indices by the batch renderer (streaks,
            // vanished shapes, stray page-colour bands at far zoom-out).
            constexpr qsizetype kChunk = 65535; // a multiple of 3
            for (qsizetype first = 0; first < run.vertices.size(); first += kChunk) {
                const qsizetype count = std::min(kChunk, run.vertices.size() - first);
                auto *node = new QSGGeometryNode;
                auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),
                                                 int(count));
                geometry->setDrawingMode(QSGGeometry::DrawTriangles);
                std::memcpy(geometry->vertexData(), run.vertices.constData() + first,
                            size_t(count) * sizeof(CanvasVertex));
                node->setGeometry(geometry);
                node->setMaterial(new QSGVertexColorMaterial);
                node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
                clip->appendChildNode(node);
            }
        } else if (QSGImageNode *image = imageNode(window, run.image, run.localRect, tile)) {
            auto *placed = new QSGTransformNode;
            placed->setMatrix(QMatrix4x4(run.toDocument));
            placed->appendChildNode(image);
            clip->appendChildNode(placed);
        }
    }
    return tile;
}

} // namespace

int liveRootNodes()
{
    return g_roots.load();
}

int liveTextures()
{
    return g_textures.load();
}

QSGNode *sync(QSGNode *oldNode, const CanvasRenderFrame *frame, QQuickWindow *window,
              const QColor &canvasColor, const QSizeF &viewSize)
{
    auto *root = static_cast<RootNode *>(oldNode);
    if (!frame || !window) {
        delete root;
        return nullptr;
    }
    if (root && (root->generation != frame->resourceGeneration || root->backend != frame->backend)) {
        delete root; // releases every texture and geometry of the old generation
        root = nullptr;
    }
    if (!root) {
        root = new RootNode;
        root->generation = frame->resourceGeneration;
        root->backend = frame->backend;
        root->background = window->createRectangleNode();
        root->transform = new QSGTransformNode;
        root->appendChildNode(root->background);
        root->appendChildNode(root->transform);
    }
    root->background->setRect(QRectF(QPointF(0, 0), viewSize));
    root->background->setColor(canvasColor);
    root->transform->setMatrix(QMatrix4x4(frame->documentToView));

    QSet<CanvasTileKey> keep;
    keep.reserve(frame->items.size());
    for (const CanvasRenderItem &item : frame->items) {
        keep.insert(item.key);
        TileNode *tile = root->tiles.value(item.key);
        if (tile && tile->serial != item.serial) {
            root->transform->removeChildNode(tile);
            delete tile;
            tile = nullptr;
        }
        if (!tile) {
            // Managed display tiles are composited and converted on the CPU,
            // then uploaded by either backend. Raw sRGB keeps the geometry path.
            tile = !item.content->raster.isNull() ? buildSoftware(item, window)
                                                  : buildSceneGraph(item, window);
            root->transform->appendChildNode(tile);
            root->tiles.insert(item.key, tile);
        }
    }
    for (auto it = root->tiles.begin(); it != root->tiles.end();) {
        if (!keep.contains(it.key())) {
            root->transform->removeChildNode(it.value());
            delete it.value();
            it = root->tiles.erase(it);
        } else {
            ++it;
        }
    }
    return root;
}

} // namespace QindaTK::CanvasSceneGraph
