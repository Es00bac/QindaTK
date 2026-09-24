// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_render.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#render-surface-adapter): the GUI-thread half
// of the rendering boundary (PLT-10). It owns
//   * tile planning for the visible region at the current LOD and DPR,
//   * a byte-bounded LRU cache of tile CONTENT (images / vertex runs),
//   * damage from the document (tiles go stale, stay displayed, re-render),
//   * a per-frame render budget, and
//   * the resource generation: any bump makes the render side discard every
//     GPU handle and rebuild from the cache, which is how resize, backend
//     switch and context loss avoid stale handles.
// It never owns scene-graph objects. Those live only in the node tree built
// by CanvasSurface::updatePaintNode, which Qt destroys with the scene graph.
class RenderSurfaceAdapter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by CanvasView")
    Q_PROPERTY(QString requestedBackend READ requestedBackend WRITE setRequestedBackend NOTIFY backendChanged)
    Q_PROPERTY(QString effectiveBackend READ effectiveBackend NOTIFY backendChanged)
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY backendChanged)
    Q_PROPERTY(qulonglong resourceGeneration READ resourceGeneration NOTIFY backendChanged)
    Q_PROPERTY(int tilePixels READ tilePixels WRITE setTilePixels NOTIFY configurationChanged)
    Q_PROPERTY(int maxTilesPerFrame READ maxTilesPerFrame WRITE setMaxTilesPerFrame NOTIFY configurationChanged)
    Q_PROPERTY(int cacheLimitMiB READ cacheLimitMiB WRITE setCacheLimitMiB NOTIFY configurationChanged)
    Q_PROPERTY(QString colorContext READ colorContextDescription NOTIFY configurationChanged)
    Q_PROPERTY(QString colorError READ colorError NOTIFY configurationChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(bool pending READ hasPendingWork NOTIFY statsChanged)

public:
    explicit RenderSurfaceAdapter(QObject *parent = nullptr);

    void setDocument(CanvasDocumentAdapter *document);
    CanvasDocumentAdapter *document() const { return m_document; }

    QString requestedBackend() const { return m_requested; }
    void setRequestedBackend(const QString &name); // "auto" | "software" | "scenegraph"
    QString effectiveBackend() const;
    CanvasBackend backend() const { return m_backend; }
    QString fallbackReason() const { return m_fallbackReason; }
    // Called by the surface once it knows its window's graphics API.
    void setSceneGraphAvailable(bool available, const QString &reason);
    quint64 resourceGeneration() const { return m_generation; }

    int tilePixels() const { return m_tilePixels; }
    void setTilePixels(int pixels);
    int maxTilesPerFrame() const { return m_budget; }
    void setMaxTilesPerFrame(int tiles);
    int cacheLimitMiB() const { return int(m_cacheLimit / (1024 * 1024)); }
    void setCacheLimitMiB(int mebibytes);
    const CanvasColorContext &colorContext() const { return m_color; }
    void setColorContext(const CanvasColorContext &context);
    QString colorContextDescription() const { return m_color.description(); }
    QString colorError() const { return m_colorError; }

    // Plans the visible tiles for `request`, renders at most
    // maxTilesPerFrame missing/stale ones, evicts beyond the byte limit and
    // returns the immutable frame for the render thread.
    std::shared_ptr<const CanvasRenderFrame> prepare(CanvasRenderRequest request);
    bool hasPendingWork() const { return m_pending > 0; }
    QVariantMap stats() const;
    qsizetype cachedTiles() const { return m_cache.size(); }
    qsizetype cacheBytes() const { return m_cacheBytes; }
    int lastVisible() const { return m_visible; }
    int lastRendered() const { return m_rendered; }
    int lastPending() const { return m_pending; }
    qsizetype lastCandidates() const { return m_candidates; }

    // A real graphics reset (scene graph invalidated) or a test injection.
    // dropCaches also discards the CPU tile cache so every visible tile is
    // re-rendered from the document.
    Q_INVOKABLE void simulateContextLoss(bool dropCaches = false);
    void notifyGraphicsReset(const QString &reason);
    Q_INVOKABLE void invalidateAll();
    QString lastResetReason() const { return m_resetReason; }

signals:
    void backendChanged();
    void configurationChanged();
    void statsChanged();
    // Something the view shows changed; the surface should schedule a frame.
    void updateRequested();

private:
    struct Tile {
        std::shared_ptr<const CanvasTileContent> content;
        quint64 revision = 0;
        quint64 colorGeneration = 0;
        quint64 serial = 0;
        quint64 lastUsed = 0;
        bool stale = false;
    };
    void onDamaged(const QVector<QRectF> &regions, quint64 revision);
    void onReset(quint64 revision);
    void resolveBackend();
    void clearCache();
    void evict(quint64 frame);
    void syncDocumentOutputProfile();

    QPointer<CanvasDocumentAdapter> m_document;
    QHash<CanvasTileKey, Tile> m_cache;
    CanvasColorContext m_color;
    QByteArray m_documentOutputIcc;
    bool m_explicitColorContext = false;
    QString m_colorError;
    QString m_requested = QStringLiteral("auto");
    QString m_fallbackReason;
    QString m_resetReason;
    CanvasBackend m_backend = CanvasBackend::Software;
    bool m_sceneGraphAvailable = false;
    quint64 m_generation = 1;
    quint64 m_frame = 0;
    quint64 m_serial = 0;
    qsizetype m_cacheBytes = 0;
    qsizetype m_cacheLimit = 96 * 1024 * 1024;
    int m_tilePixels = 256;
    int m_budget = 24;
    // last-frame evidence
    int m_visible = 0;
    int m_rendered = 0;
    int m_pending = 0;
    int m_lod = 0;
    qsizetype m_candidates = 0;
    qsizetype m_dots = 0;
    quint64 m_renderedRevision = 0;
    quint64 m_totalRendered = 0;
    quint64 m_totalEvicted = 0;
    qreal m_dpr = 1.0;
};

} // namespace QindaTK
