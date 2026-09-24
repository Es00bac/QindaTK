// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_view_state.h"
#include "hit_test_service.h"
#include "render_surface_adapter.h"
#include "tool_controller.h"

#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <atomic>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#canvasview): the native document surface
// behind Tk.CanvasView. It maps input to document coordinates, routes
// pointer gestures to a ToolController, keeps navigation in a
// CanvasViewState, and draws through a RenderSurfaceAdapter. It owns no
// document state.
//
// AGENT-GUARD: updatePaintNode runs on the render thread. It may read only
// m_frame (an immutable snapshot produced in updatePolish) and plain values
// copied under sync. Never read the document, the tool controller or the
// adapter there.
class CanvasSurface : public QQuickItem, public CanvasNavigator {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QindaTK::ToolController *toolController READ toolController WRITE setToolController NOTIFY toolControllerChanged)
    Q_PROPERTY(QindaTK::CanvasViewState *viewState READ viewState WRITE setViewState NOTIFY viewStateChanged)
    Q_PROPERTY(QindaTK::RenderSurfaceAdapter *renderSurface READ renderSurface CONSTANT)
    Q_PROPERTY(QString viewId READ viewId WRITE setViewId NOTIFY configurationChanged)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY configurationChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(qreal viewRotation READ viewRotation WRITE setViewRotation NOTIFY viewChanged)
    Q_PROPERTY(qreal centerX READ centerX WRITE setCenterX NOTIFY viewChanged)
    Q_PROPERTY(qreal centerY READ centerY WRITE setCenterY NOTIFY viewChanged)
    Q_PROPERTY(QRectF visibleDocumentRect READ visibleDocumentRect NOTIFY viewChanged)
    Q_PROPERTY(QString fitMode READ fitMode WRITE setFitMode NOTIFY configurationChanged)
    Q_PROPERTY(qreal fitPadding MEMBER m_fitPadding NOTIFY configurationChanged)
    Q_PROPERTY(qreal hitTolerance MEMBER m_hitTolerance NOTIFY configurationChanged)
    Q_PROPERTY(qreal wheelZoomStep MEMBER m_wheelZoomStep NOTIFY configurationChanged)
    Q_PROPERTY(qreal rotationStep MEMBER m_rotationStep NOTIFY configurationChanged)
    Q_PROPERTY(qreal keyboardPanStep MEMBER m_panStep NOTIFY configurationChanged)
    Q_PROPERTY(bool touchRotation MEMBER m_touchRotation NOTIFY configurationChanged)
    Q_PROPERTY(bool interactive MEMBER m_interactive NOTIFY configurationChanged)
    Q_PROPERTY(QColor canvasColor MEMBER m_canvasColor NOTIFY configurationChanged)
    Q_PROPERTY(qreal devicePixelRatioOverride MEMBER m_dprOverride NOTIFY configurationChanged)
    Q_PROPERTY(qreal effectiveDevicePixelRatio READ effectiveDevicePixelRatio NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap hoverHit READ hoverHit NOTIFY hoverChanged)

public:
    explicit CanvasSurface(QQuickItem *parent = nullptr);
    ~CanvasSurface() override;

    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    ToolController *toolController() const { return m_tools; }
    void setToolController(ToolController *controller);
    CanvasViewState *viewState() const { return m_state; }
    void setViewState(CanvasViewState *state);
    RenderSurfaceAdapter *renderSurface() const { return m_render; }
    QString viewId() const { return m_viewId; }
    void setViewId(const QString &id);
    QString backend() const { return m_render->requestedBackend(); }
    void setBackend(const QString &backend);
    QString fitMode() const { return m_fitMode; }
    void setFitMode(const QString &mode);

    CanvasViewPose pose() const { return m_state->pose(); }
    void setPose(const CanvasViewPose &pose);
    QSizeF viewSize() const { return size(); }
    qreal zoom() const { return pose().zoom; }
    void setZoom(qreal zoom);
    // Not `rotation`: QQuickItem::rotation turns the item itself.
    qreal viewRotation() const { return pose().rotation; }
    void setViewRotation(qreal degrees);
    qreal centerX() const { return pose().center.x(); }
    void setCenterX(qreal x);
    qreal centerY() const { return pose().center.y(); }
    void setCenterY(qreal y);
    QRectF visibleDocumentRect() const;
    qreal effectiveDevicePixelRatio() const;
    QVariantMap hoverHit() const { return m_hoverHit; }
    CanvasObjectId hoverObject() const { return m_hoverObject; }

    Q_INVOKABLE QPointF mapToDocument(const QPointF &viewPoint) const;
    Q_INVOKABLE QPointF mapFromDocument(const QPointF &documentPoint) const;
    Q_INVOKABLE void zoomAbout(qreal viewX, qreal viewY, qreal factor);
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void setZoomCentered(qreal zoom);
    Q_INVOKABLE void rotateBy(qreal degrees, qreal viewX = -1, qreal viewY = -1);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    Q_INVOKABLE void fitDocument();
    Q_INVOKABLE void fitRect(const QRectF &documentRect);
    Q_INVOKABLE void resetView();
    // View-space probes; results carry document ids (see HitTestService).
    Q_INVOKABLE QVariantList hitTest(qreal viewX, qreal viewY,
                                     const QString &lockPolicy = QStringLiteral("skip")) const;
    Q_INVOKABLE QVariantList hitRegion(const QRectF &viewRect, bool contain = false) const;
    Q_INVOKABLE QVariantMap renderStats() const { return m_render->stats(); }
    Q_INVOKABLE void simulateContextLoss(bool dropCaches = false);

    // CanvasNavigator
    void navigatePan(QPointF viewDelta) override;
    void navigateZoom(QPointF viewAnchor, qreal factor) override;

signals:
    void documentChanged();
    void toolControllerChanged();
    void viewStateChanged();
    void configurationChanged();
    void viewChanged();
    void hoverChanged();
    void frameRendered();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void releaseResources() override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    enum class Pointer { None, Tool, Pan };
    CanvasToolInput toolInput(QPointF viewPoint, Qt::KeyboardModifiers modifiers,
                              CanvasDevice device, qreal pressure) const;
    void onStateChanged();
    void onPoseChanged();
    void scheduleFrame();
    void applyFit();
    void attachWindow(QQuickWindow *window);
    void updateHover(QPointF viewPoint);
    void updateCursor();
    void cancelPointer(const QString &reason);
    bool handleNativeGesture(QEvent *event);

    QPointer<CanvasDocumentAdapter> m_document;
    QPointer<ToolController> m_tools;
    QPointer<CanvasViewState> m_state;
    CanvasViewState *m_ownState = nullptr;
    RenderSurfaceAdapter *m_render = nullptr;
    std::shared_ptr<const CanvasRenderFrame> m_frame;
    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_initializedConnection;
    QMetaObject::Connection m_invalidatedConnection;
    std::atomic_bool m_graphicsInvalidated{false};
    QString m_viewId;
    QString m_fitMode = QStringLiteral("none");
    QColor m_canvasColor{QStringLiteral("#1e1e1e")};
    QVariantMap m_hoverHit;
    CanvasObjectId m_hoverObject = 0;
    Pointer m_pointer = Pointer::None;
    Qt::MouseButton m_mouseButton = Qt::NoButton;
    QPointF m_lastPan;
    QPointF m_touchCentroid;
    qreal m_touchDistance = 0;
    qreal m_touchAngle = 0;
    bool m_touchNavigating = false;
    bool m_userNavigated = false;
    bool m_writingPose = false; // inside setPose: this surface's own write
    CanvasViewPose m_lastPose;  // the pose onPoseChanged last saw
    bool m_refineScheduled = false;
    bool m_spaceHeld = false;
    qreal m_fitPadding = 16;
    qreal m_hitTolerance = 4;
    qreal m_wheelZoomStep = 1.25;
    qreal m_rotationStep = 15;
    qreal m_panStep = 48;
    qreal m_dprOverride = 0;
    bool m_touchRotation = true;
    bool m_interactive = true;
};

} // namespace QindaTK
