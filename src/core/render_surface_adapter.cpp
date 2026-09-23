// SPDX-License-Identifier: LGPL-3.0-or-later
#include "render_surface_adapter.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace QindaTK {

QString CanvasColorContext::description() const
{
    auto name = [](const QColorSpace &space) {
        const QString d = space.description();
        return d.isEmpty() ? QStringLiteral("sRGB") : d;
    };
    return name(working) + QStringLiteral(" -> ") + name(display);
}

namespace CanvasTiles {

int levelOfDetail(qreal zoom, qreal devicePixelRatio)
{
    const qreal s = zoom * devicePixelRatio;
    if (!std::isfinite(s) || s <= 0) {
        return 0;
    }
    return std::clamp(int(std::ceil(std::log2(s) - 1e-9)), -24, 24);
}

qreal tileDocumentSize(int lod, int tilePixels)
{
    return std::ldexp(qreal(tilePixels), -lod);
}

QRectF tileRect(const CanvasTileKey &key, int tilePixels)
{
    const qreal size = tileDocumentSize(key.lod, tilePixels);
    return QRectF(key.x * size, key.y * size, size, size);
}

QVector<CanvasTileKey> visible(const CanvasViewPose &pose, const QSizeF &viewSize,
                               qreal devicePixelRatio, int tilePixels)
{
    QVector<CanvasTileKey> keys;
    if (viewSize.isEmpty() || tilePixels <= 0) {
        return keys;
    }
    const int lod = levelOfDetail(pose.zoom, devicePixelRatio);
    const qreal size = tileDocumentSize(lod, tilePixels);
    const QPolygonF view = CanvasTransform::visibleDocumentPolygon(pose, viewSize);
    const QRectF b = view.boundingRect();
    const int x0 = int(std::floor(b.left() / size));
    const int x1 = int(std::floor(b.right() / size));
    const int y0 = int(std::floor(b.top() / size));
    const int y1 = int(std::floor(b.bottom() / size));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const CanvasTileKey key{lod, x, y};
            const QPolygonF tile(tileRect(key, tilePixels));
            if (CanvasGeometry::outlineTouchesRegion(tile, true, view)) {
                keys << key;
            }
        }
    }
    const QPointF c = pose.center;
    std::sort(keys.begin(), keys.end(), [&](const CanvasTileKey &a, const CanvasTileKey &k) {
        const QPointF da = tileRect(a, tilePixels).center() - c;
        const QPointF db = tileRect(k, tilePixels).center() - c;
        const qreal la = da.x() * da.x() + da.y() * da.y();
        const qreal lb = db.x() * db.x() + db.y() * db.y();
        if (la != lb) {
            return la < lb;
        }
        return a.y != k.y ? a.y < k.y : a.x < k.x;
    });
    return keys;
}

int maxVisible(const QSizeF &viewSize, qreal devicePixelRatio, int tilePixels)
{
    // A tile spans more than tilePixels / 2 device pixels on screen, and any
    // rotation of the view fits in its diagonal.
    const qreal diagonal = std::hypot(viewSize.width(), viewSize.height()) * devicePixelRatio;
    const int n = int(std::ceil(diagonal / (tilePixels / 2.0))) + 1;
    return n * n;
}

} // namespace CanvasTiles

RenderSurfaceAdapter::RenderSurfaceAdapter(QObject *parent) : QObject(parent) {}

void RenderSurfaceAdapter::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) {
        return;
    }
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    clearCache();
    if (document) {
        connect(document, &CanvasDocumentAdapter::damaged, this, &RenderSurfaceAdapter::onDamaged);
        connect(document, &CanvasDocumentAdapter::reset, this, &RenderSurfaceAdapter::onReset);
        connect(document, &QObject::destroyed, this, [this] {
            clearCache();
            emit updateRequested();
        });
    }
    emit updateRequested();
}

QString RenderSurfaceAdapter::effectiveBackend() const
{
    return m_backend == CanvasBackend::SceneGraph ? QStringLiteral("scenegraph")
                                                  : QStringLiteral("software");
}

void RenderSurfaceAdapter::setRequestedBackend(const QString &name)
{
    const QString normalized = name == QLatin1String("software") || name == QLatin1String("scenegraph")
        ? name : QStringLiteral("auto");
    if (normalized == m_requested) {
        return;
    }
    m_requested = normalized;
    resolveBackend();
    emit backendChanged();
}

void RenderSurfaceAdapter::setSceneGraphAvailable(bool available, const QString &reason)
{
    if (available == m_sceneGraphAvailable && (available || reason == m_fallbackReason)) {
        return;
    }
    m_sceneGraphAvailable = available;
    m_fallbackReason = available ? QString{} : reason;
    resolveBackend();
    emit backendChanged();
}

void RenderSurfaceAdapter::resolveBackend()
{
    CanvasBackend next = CanvasBackend::Software;
    if (m_requested != QLatin1String("software") && m_sceneGraphAvailable) {
        next = CanvasBackend::SceneGraph;
    }
    if (next == m_backend) {
        return;
    }
    // Tile content differs per backend; the render side must drop every
    // node built for the old one, so this is also a resource reset.
    m_backend = next;
    clearCache();
    ++m_generation;
    m_resetReason = QStringLiteral("backend-switch");
    emit updateRequested();
}

void RenderSurfaceAdapter::setTilePixels(int pixels)
{
    pixels = std::clamp(pixels, 64, 2048);
    if (pixels != m_tilePixels) {
        m_tilePixels = pixels;
        clearCache();
        emit configurationChanged();
        emit updateRequested();
    }
}

void RenderSurfaceAdapter::setMaxTilesPerFrame(int tiles)
{
    tiles = std::max(1, tiles);
    if (tiles != m_budget) {
        m_budget = tiles;
        emit configurationChanged();
        emit updateRequested();
    }
}

void RenderSurfaceAdapter::setCacheLimitMiB(int mebibytes)
{
    const qsizetype limit = qsizetype(std::max(1, mebibytes)) * 1024 * 1024;
    if (limit != m_cacheLimit) {
        m_cacheLimit = limit;
        emit configurationChanged();
        emit updateRequested();
    }
}

void RenderSurfaceAdapter::setColorContext(const CanvasColorContext &context)
{
    const quint64 generation = m_color.generation + 1;
    m_color = context;
    m_color.generation = generation;
    emit configurationChanged();
    emit updateRequested();
}

void RenderSurfaceAdapter::clearCache()
{
    m_cache.clear();
    m_cacheBytes = 0;
}

void RenderSurfaceAdapter::onDamaged(const QVector<QRectF> &regions, quint64)
{
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->stale) {
            continue;
        }
        const QRectF tile = CanvasTiles::tileRect(it.key(), m_tilePixels);
        // Two device pixels of antialiasing bleed at this tile's scale.
        const qreal pad = 2.0 * tile.width() / m_tilePixels;
        for (const QRectF &r : regions) {
            if (tile.intersects(r.adjusted(-pad, -pad, pad, pad))) {
                it->stale = true;
                break;
            }
        }
    }
    emit updateRequested();
}

void RenderSurfaceAdapter::onReset(quint64)
{
    invalidateAll();
}

void RenderSurfaceAdapter::invalidateAll()
{
    for (Tile &tile : m_cache) {
        tile.stale = true;
    }
    emit updateRequested();
}

void RenderSurfaceAdapter::simulateContextLoss(bool dropCaches)
{
    if (dropCaches) {
        clearCache();
    }
    notifyGraphicsReset(dropCaches ? QStringLiteral("simulated-loss-drop-caches")
                                   : QStringLiteral("simulated-loss"));
}

void RenderSurfaceAdapter::notifyGraphicsReset(const QString &reason)
{
    ++m_generation;
    m_resetReason = reason;
    emit backendChanged();
    emit updateRequested();
}

void RenderSurfaceAdapter::evict(quint64 frame)
{
    if (m_cacheBytes <= m_cacheLimit) {
        return;
    }
    QVector<std::pair<quint64, CanvasTileKey>> order;
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        if (it->lastUsed < frame) {
            order.append({it->lastUsed, it.key()});
        }
    }
    std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first < b.first
             : std::tie(a.second.lod, a.second.y, a.second.x) < std::tie(b.second.lod, b.second.y, b.second.x);
    });
    for (const auto &entry : order) {
        if (m_cacheBytes <= m_cacheLimit) {
            break;
        }
        const Tile tile = m_cache.take(entry.second);
        m_cacheBytes -= tile.content ? tile.content->bytes : 0;
        ++m_totalEvicted;
    }
}

std::shared_ptr<const CanvasRenderFrame> RenderSurfaceAdapter::prepare(CanvasRenderRequest request)
{
    request.color = m_color;
    auto frame = std::make_shared<CanvasRenderFrame>();
    frame->request = request;
    frame->backend = m_backend;
    frame->resourceGeneration = m_generation;
    frame->tilePixels = m_tilePixels;
    frame->documentToView = CanvasTransform::documentToView(request.pose, request.viewSize);
    m_dpr = request.devicePixelRatio;
    m_visible = m_rendered = m_pending = 0;
    m_candidates = m_dots = 0;
    if (!m_document || request.viewSize.isEmpty()) {
        emit statsChanged();
        return frame;
    }
    ++m_frame;
    const quint64 revision = m_document->revision();
    const QVector<CanvasTileKey> keys = CanvasTiles::visible(request.pose, request.viewSize,
                                                             request.devicePixelRatio, m_tilePixels);
    m_lod = CanvasTiles::levelOfDetail(request.pose.zoom, request.devicePixelRatio);
    m_visible = int(keys.size());
    const qreal scale = std::ldexp(1.0, m_lod);
    for (const CanvasTileKey &key : keys) {
        const QRectF rect = CanvasTiles::tileRect(key, m_tilePixels);
        Tile &tile = m_cache[key];
        const bool needs = !tile.content || tile.stale || tile.colorGeneration != m_color.generation;
        if (needs && m_rendered < m_budget) {
            CanvasPaintStats ps;
            auto content = std::make_shared<CanvasTileContent>();
            if (m_backend == CanvasBackend::Software) {
                content->raster = CanvasPainter::renderTile(*m_document, rect, m_tilePixels, m_color, &ps);
                content->bytes = content->raster.sizeInBytes();
            } else {
                *content = CanvasTessellator::build(*m_document, rect, scale, &ps);
            }
            m_cacheBytes += content->bytes - (tile.content ? tile.content->bytes : 0);
            tile.content = std::move(content);
            tile.revision = revision;
            tile.colorGeneration = m_color.generation;
            tile.serial = ++m_serial;
            tile.stale = false;
            m_candidates += ps.candidates;
            m_dots += ps.dots;
            ++m_rendered;
            ++m_totalRendered;
        } else if (needs) {
            ++m_pending;
        }
        tile.lastUsed = m_frame;
        if (tile.content) {
            frame->items.append({key, rect, tile.serial, tile.stale, tile.content});
        } else {
            m_cache.remove(key); // placeholder created by operator[]
        }
    }
    evict(m_frame);
    if (m_pending == 0) {
        m_renderedRevision = revision;
    }
    emit statsChanged();
    return frame;
}

QVariantMap RenderSurfaceAdapter::stats() const
{
    return {{QStringLiteral("backend"), effectiveBackend()},
            {QStringLiteral("lod"), m_lod},
            {QStringLiteral("devicePixelRatio"), m_dpr},
            {QStringLiteral("visibleTiles"), m_visible},
            {QStringLiteral("renderedTiles"), m_rendered},
            {QStringLiteral("pendingTiles"), m_pending},
            {QStringLiteral("cachedTiles"), int(m_cache.size())},
            {QStringLiteral("cacheBytes"), qlonglong(m_cacheBytes)},
            {QStringLiteral("cacheLimitBytes"), qlonglong(m_cacheLimit)},
            {QStringLiteral("candidates"), qlonglong(m_candidates)},
            {QStringLiteral("dots"), qlonglong(m_dots)},
            {QStringLiteral("resourceGeneration"), qulonglong(m_generation)},
            {QStringLiteral("resetReason"), m_resetReason},
            {QStringLiteral("renderedRevision"), qulonglong(m_renderedRevision)},
            {QStringLiteral("documentRevision"), qulonglong(m_document ? m_document->revision() : 0)},
            {QStringLiteral("totalRendered"), qulonglong(m_totalRendered)},
            {QStringLiteral("totalEvicted"), qulonglong(m_totalEvicted)},
            {QStringLiteral("colorContext"), m_color.description()}};
}

} // namespace QindaTK
