// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_surface.h"
#include "canvas_surface_nodes.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QUuid>
#include <cmath>

namespace QindaTK {

CanvasSurface::CanvasSurface(QQuickItem *parent)
    : QQuickItem(parent),
      m_ownState(new CanvasViewState(this)),
      m_render(new RenderSurfaceAdapter(this)),
      m_viewId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    setFlag(ItemHasContents, true);
    setFlag(ItemIsFocusScope, false);
    setClip(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(true);
    setActiveFocusOnTab(true);
    m_state = m_ownState;
    connect(m_ownState, &CanvasViewState::changed, this, &CanvasSurface::onPoseChanged);
    connect(m_render, &RenderSurfaceAdapter::updateRequested, this, &CanvasSurface::scheduleFrame);
}

CanvasSurface::~CanvasSurface()
{
    // A view that disappears mid-gesture must not leave its tool active.
    if (m_tools) {
        m_tools->viewDetached(m_viewId);
    }
    QObject::disconnect(m_initializedConnection);
    QObject::disconnect(m_invalidatedConnection);
}

void CanvasSurface::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) {
        return;
    }
    cancelPointer(QStringLiteral("document-switch"));
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    m_render->setDocument(document);
    m_hoverHit.clear();
    m_hoverObject = 0;
    if (document) {
        connect(document, &CanvasDocumentAdapter::edited, this, [this] {
            if (m_fitMode != QLatin1String("none")) {
                applyFit();
            }
        });
        connect(document, &QObject::destroyed, this, [this] {
            m_hoverHit.clear();
            m_hoverObject = 0;
            emit hoverChanged();
            emit documentChanged();
        });
    }
    m_userNavigated = false;
    applyFit();
    emit hoverChanged();
    emit documentChanged();
    scheduleFrame();
}

void CanvasSurface::setToolController(ToolController *controller)
{
    if (m_tools == controller) {
        return;
    }
    cancelPointer(QStringLiteral("view-detached"));
    if (m_tools) {
        m_tools->viewDetached(m_viewId);
        disconnect(m_tools, nullptr, this, nullptr);
    }
    m_tools = controller;
    if (controller) {
        connect(controller, &ToolController::toolChanged, this, &CanvasSurface::updateCursor);
        connect(controller, &ToolController::stateChanged, this, &CanvasSurface::updateCursor);
    }
    updateCursor();
    emit toolControllerChanged();
}

void CanvasSurface::setViewState(CanvasViewState *state)
{
    CanvasViewState *next = state ? state : m_ownState;
    if (m_state == next) {
        return;
    }
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
    }
    m_state = next;
    connect(next, &CanvasViewState::changed, this, &CanvasSurface::onPoseChanged);
    connect(next, &QObject::destroyed, this, [this] { setViewState(nullptr); });
    m_lastPose = next->pose();
    emit viewStateChanged();
    onStateChanged();
}

void CanvasSurface::setViewId(const QString &id)
{
    if (id.isEmpty() || id == m_viewId) {
        return;
    }
    cancelPointer(QStringLiteral("view-detached"));
    if (m_tools) {
        m_tools->viewDetached(m_viewId);
    }
    m_viewId = id;
    emit configurationChanged();
}

void CanvasSurface::setBackend(const QString &backend)
{
    if (backend == m_render->requestedBackend()) {
        return;
    }
    m_render->setRequestedBackend(backend);
    emit configurationChanged();
}

void CanvasSurface::setFitMode(const QString &mode)
{
    const QString next = mode == QLatin1String("page") || mode == QLatin1String("content")
        ? mode : QStringLiteral("none");
    if (next != m_fitMode) {
        m_fitMode = next;
        m_userNavigated = false;
        applyFit();
        emit configurationChanged();
    }
}

void CanvasSurface::setPose(const CanvasViewPose &pose)
{
    m_writingPose = true;
    m_state->setPose(pose);
    m_writingPose = false;
}

void CanvasSurface::setZoom(qreal zoom)
{
    setZoomCentered(zoom);
}

void CanvasSurface::setViewRotation(qreal degrees)
{
    CanvasViewPose p = pose();
    p.rotation = degrees;
    setPose(p);
}

void CanvasSurface::setCenterX(qreal x)
{
    CanvasViewPose p = pose();
    p.center.setX(x);
    setPose(p);
}

void CanvasSurface::setCenterY(qreal y)
{
    CanvasViewPose p = pose();
    p.center.setY(y);
    setPose(p);
}

QRectF CanvasSurface::visibleDocumentRect() const
{
    return CanvasTransform::visibleDocumentBounds(pose(), viewSize());
}

qreal CanvasSurface::effectiveDevicePixelRatio() const
{
    if (m_dprOverride > 0) {
        return m_dprOverride;
    }
    return window() ? window()->effectiveDevicePixelRatio() : 1.0;
}

QPointF CanvasSurface::mapToDocument(const QPointF &viewPoint) const
{
    return CanvasTransform::mapToDocument(pose(), viewSize(), viewPoint);
}

QPointF CanvasSurface::mapFromDocument(const QPointF &documentPoint) const
{
    return CanvasTransform::mapToView(pose(), viewSize(), documentPoint);
}

void CanvasSurface::zoomAbout(qreal viewX, qreal viewY, qreal factor)
{
    m_userNavigated = true;
    setPose(CanvasTransform::zoomAbout(pose(), viewSize(), QPointF(viewX, viewY), factor,
                                       m_state->limits()));
}

void CanvasSurface::zoomIn()
{
    zoomAbout(width() / 2, height() / 2, m_wheelZoomStep);
}

void CanvasSurface::zoomOut()
{
    zoomAbout(width() / 2, height() / 2, 1.0 / m_wheelZoomStep);
}

void CanvasSurface::setZoomCentered(qreal zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0) {
        return;
    }
    zoomAbout(width() / 2, height() / 2, zoom / pose().zoom);
}

void CanvasSurface::rotateBy(qreal degrees, qreal viewX, qreal viewY)
{
    const QPointF anchor = viewX < 0 || viewY < 0 ? QPointF(width() / 2, height() / 2)
                                                 : QPointF(viewX, viewY);
    m_userNavigated = true;
    setPose(CanvasTransform::rotateAbout(pose(), viewSize(), anchor, degrees));
}

void CanvasSurface::panBy(qreal dx, qreal dy)
{
    m_userNavigated = true;
    setPose(CanvasTransform::panBy(pose(), QPointF(dx, dy)));
}

void CanvasSurface::navigatePan(QPointF viewDelta)
{
    panBy(viewDelta.x(), viewDelta.y());
}

void CanvasSurface::navigateZoom(QPointF viewAnchor, qreal factor)
{
    zoomAbout(viewAnchor.x(), viewAnchor.y(), factor);
}

void CanvasSurface::fitRect(const QRectF &documentRect)
{
    setPose(CanvasTransform::fit(pose(), viewSize(), documentRect, m_fitPadding, m_state->limits()));
}

void CanvasSurface::fitDocument()
{
    if (m_document) {
        m_userNavigated = false;
        fitRect(m_fitMode == QLatin1String("content") ? m_document->contentBounds()
                                                      : m_document->pageRect());
    }
}

void CanvasSurface::resetView()
{
    CanvasViewPose p = pose();
    p.rotation = 0;
    setPose(p);
    fitDocument();
}

void CanvasSurface::applyFit()
{
    if (m_fitMode == QLatin1String("none") || m_userNavigated || !m_document || viewSize().isEmpty()) {
        return;
    }
    fitRect(m_fitMode == QLatin1String("content") ? m_document->contentBounds()
                                                  : m_document->pageRect());
}

QVariantList CanvasSurface::hitTest(qreal viewX, qreal viewY, const QString &lockPolicy) const
{
    QVariantList out;
    if (!m_document) {
        return out;
    }
    CanvasHitPolicy policy;
    policy.locked = HitTestService::lockPolicy(lockPolicy);
    const QPointF doc = mapToDocument(QPointF(viewX, viewY));
    for (const CanvasHit &h : HitTestService::hitPoint(
             *m_document, doc, HitTestService::documentTolerance(m_hitTolerance, zoom()), policy)) {
        out << HitTestService::toVariant(h);
    }
    return out;
}

QVariantList CanvasSurface::hitRegion(const QRectF &viewRect, bool contain) const
{
    QVariantList out;
    if (!m_document) {
        return out;
    }
    const QPolygonF region = CanvasTransform::viewToDocument(pose(), viewSize()).map(QPolygonF(viewRect));
    const auto mode = contain ? CanvasRegionMode::Contain : CanvasRegionMode::Intersect;
    for (CanvasObjectId id : HitTestService::hitRegion(*m_document, region, mode)) {
        out << qulonglong(id);
    }
    return out;
}

void CanvasSurface::simulateContextLoss(bool dropCaches)
{
    m_render->simulateContextLoss(dropCaches);
}

void CanvasSurface::onPoseChanged()
{
    // AGENT-GUARD (docs/canvas.md#synchronized-views): a pose change that did
    // not come through this surface (a synchronized view, a NavigatorView,
    // the host writing the state) is navigation and ends refitting, or the
    // next edit or resize would refit the shared pose and undo it. The
    // surface's own writes mark navigation where they are navigation.
    // CanvasViewState also emits `changed` for a limit change that leaves the
    // pose as it was; that is not navigation.
    const CanvasViewPose current = pose();
    if (!m_writingPose && !(current == m_lastPose)) m_userNavigated = true;
    m_lastPose = current;
    onStateChanged();
}

void CanvasSurface::onStateChanged()
{
    emit viewChanged();
    scheduleFrame();
}

void CanvasSurface::scheduleFrame()
{
    polish();
    update();
}

void CanvasSurface::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        applyFit();
        emit viewChanged();
        scheduleFrame();
    }
}

void CanvasSurface::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange) {
        attachWindow(value.window);
    }
    QQuickItem::itemChange(change, value);
}

void CanvasSurface::attachWindow(QQuickWindow *window)
{
    if (m_window == window) {
        return;
    }
    const bool hadWindow = m_window;
    QObject::disconnect(m_initializedConnection);
    QObject::disconnect(m_invalidatedConnection);
    if (m_window) {
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    m_graphicsInvalidated = false;
    if (hadWindow) {
        // A QSG texture belongs to its creating window. A live-to-live move
        // need not invalidate either scene graph, so force this view's render
        // generation forward while retaining its window-independent CPU tiles.
        m_render->notifyGraphicsReset(QStringLiteral("window-change"));
    }
    if (!window) {
        return;
    }
    // AGENT-GUARD: both lifecycle signals can arrive on the render thread.
    // Only touch atomics and post back; the GUI thread rebuilds the immutable
    // frame, and Qt deletes old-window nodes on its render thread.
    m_initializedConnection = connect(window, &QQuickWindow::sceneGraphInitialized, this, [this] {
        QMetaObject::invokeMethod(this, [this] { scheduleFrame(); }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    m_invalidatedConnection = connect(window, &QQuickWindow::sceneGraphInvalidated, this, [this] {
        m_graphicsInvalidated = true;
        QMetaObject::invokeMethod(this, [this] { scheduleFrame(); }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    connect(window, &QWindow::screenChanged, this, [this] {
        emit viewChanged();
        scheduleFrame();
    });
    scheduleFrame();
}

void CanvasSurface::releaseResources()
{
    m_render->notifyGraphicsReset(QStringLiteral("release-resources"));
}

void CanvasSurface::updatePolish()
{
    if (m_graphicsInvalidated.exchange(false)) {
        m_render->notifyGraphicsReset(QStringLiteral("scenegraph-invalidated"));
    }
    // The live renderer is authoritative once the scene graph exists; before
    // that, the configured adaptation name is ("software" for QT_QUICK_BACKEND).
    auto api = QQuickWindow::graphicsApi();
    if (window() && window()->rendererInterface()
        && window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Unknown) {
        api = window()->rendererInterface()->graphicsApi();
    }
    if (api == QSGRendererInterface::Software
        || QQuickWindow::sceneGraphBackend() == QLatin1String("software")) {
        // Qt's software adaptation silently skips custom geometry nodes.
        m_render->setSceneGraphAvailable(false, QStringLiteral("software-adaptation"));
    } else if (api == QSGRendererInterface::Null) {
        m_render->setSceneGraphAvailable(false, QStringLiteral("null-rhi"));
    } else {
        m_render->setSceneGraphAvailable(true, {});
    }
    CanvasRenderRequest request;
    request.documentRevision = m_document ? m_document->revision() : 0;
    request.pose = pose();
    request.viewSize = viewSize();
    request.devicePixelRatio = effectiveDevicePixelRatio();
    m_frame = m_render->prepare(request);
    if (m_render->hasPendingWork() && !m_refineScheduled) {
        // Bounded work per frame: finish the remaining tiles on later turns
        // of the event loop instead of stalling this one.
        m_refineScheduled = true;
        QTimer::singleShot(0, this, [this] {
            m_refineScheduled = false;
            scheduleFrame();
        });
    }
    emit frameRendered();
}

QSGNode *CanvasSurface::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    return CanvasSceneGraph::sync(oldNode, m_frame.get(), window(), m_canvasColor, size());
}

} // namespace QindaTK
