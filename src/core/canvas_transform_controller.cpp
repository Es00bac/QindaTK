// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_transform_controller.h"

#include <QtMath>
#include <cmath>

namespace QindaTK {
namespace {

bool finite(QPointF point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool hasLeft(const QString &handle) { return handle.contains(QLatin1String("Left")) || handle == QLatin1String("left"); }
bool hasRight(const QString &handle) { return handle.contains(QLatin1String("Right")) || handle == QLatin1String("right"); }
bool hasTop(const QString &handle) { return handle.contains(QLatin1String("Top")) || handle == QLatin1String("top"); }
bool hasBottom(const QString &handle) { return handle.contains(QLatin1String("Bottom")) || handle == QLatin1String("bottom"); }

} // namespace

void CanvasTransformController::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    cancel();
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &CanvasDocumentAdapter::edited, this, [this] {
            if (m_active && !m_committing) cancel();
            emit changed();
            emit previewChanged();
        });
        connect(document, &CanvasDocumentAdapter::reset, this, [this] {
            cancel();
            emit changed();
            emit previewChanged();
        });
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            cancel();
            emit changed();
            emit previewChanged();
        });
    }
    emit changed();
    emit previewChanged();
}

void CanvasTransformController::setTools(ToolController *tools)
{
    if (m_tools == tools) return;
    cancel();
    if (m_tools) disconnect(m_tools, nullptr, this, nullptr);
    m_tools = tools;
    if (tools) {
        connect(tools, &ToolController::selectionChanged, this, [this] {
            if (m_active && !m_committing) cancel();
            emit changed();
            emit previewChanged();
        });
        connect(tools, &ToolController::documentChanged, this, [this] {
            cancel();
            emit changed();
            emit previewChanged();
        });
        connect(tools, &QObject::destroyed, this, [this] {
            m_tools = nullptr;
            cancel();
            emit changed();
            emit previewChanged();
        });
    }
    emit changed();
    emit previewChanged();
}

void CanvasTransformController::setSnapModel(CanvasSnapModel *model)
{
    if (m_snapModel == model) return;
    m_snapModel = model;
    emit changed();
}

void CanvasTransformController::setViewState(CanvasViewState *state)
{
    if (m_viewState == state) return;
    m_viewState = state;
    emit changed();
}

QRectF CanvasTransformController::selectionBounds() const
{
    QRectF bounds;
    bool first = true;
    if (!m_document || !m_tools || m_tools->document() != m_document) return bounds;
    for (CanvasObjectId id : m_tools->selection()) {
        const CanvasObject *object = m_document->object(id);
        if (!object || !CanvasGeometry::isRenderable(*object)
            || !m_document->effectiveVisible(*object)) continue;
        const QRectF current = CanvasGeometry::documentBounds(*object);
        if (first) { bounds = current; first = false; }
        else bounds |= current;
    }
    return bounds;
}

bool CanvasTransformController::supportedHandle(const QString &handle)
{
    static const QStringList handles = {
        QStringLiteral("move"), QStringLiteral("topLeft"), QStringLiteral("top"),
        QStringLiteral("topRight"), QStringLiteral("right"),
        QStringLiteral("bottomRight"), QStringLiteral("bottom"),
        QStringLiteral("bottomLeft"), QStringLiteral("left")};
    return handles.contains(handle);
}

QPointF CanvasTransformController::handlePoint(const QString &handle, const QRectF &bounds)
{
    return {hasLeft(handle) ? bounds.left() : hasRight(handle) ? bounds.right() : bounds.center().x(),
            hasTop(handle) ? bounds.top() : hasBottom(handle) ? bounds.bottom() : bounds.center().y()};
}

bool CanvasTransformController::capture(const QString &handle)
{
    if (!supportedHandle(handle) || !m_document || !m_tools
        || m_tools->document() != m_document || m_tools->selection().isEmpty()) return false;
    m_before.clear();
    m_before.reserve(m_tools->selection().size());
    for (CanvasObjectId id : m_tools->selection()) {
        const CanvasObject *object = m_document->object(id);
        if (!object || !CanvasGeometry::isRenderable(*object)
            || !m_document->effectiveVisible(*object)
            || m_document->effectiveLocked(*object)) {
            m_before.clear();
            return false;
        }
        m_before.append(*object);
    }
    m_baseBounds = selectionBounds();
    m_previewBounds = m_baseBounds;
    m_revision = m_document->revision();
    m_handle = handle;
    m_active = true;
    emit previewChanged();
    return true;
}

bool CanvasTransformController::update(const QString &handle, QPointF start,
                                       QPointF current, int modifiers)
{
    if (!finite(start) || !finite(current)) return false;
    if (!m_active && !capture(handle)) return false;
    if (handle != m_handle || !m_document || m_document->revision() != m_revision) {
        cancel();
        return false;
    }
    if (m_snapModel && m_snapModel->document() == m_document) {
        QVariantList excluded;
        for (const CanvasObject &object : std::as_const(m_before))
            excluded.append(QString::number(object.id));
        current = m_snapModel->snapPoint(current, excluded).value(QStringLiteral("point")).toPointF();
    }
    const QPointF delta = current - start;
    QTransform world;
    if (handle == QLatin1String("move")) {
        world = QTransform::fromTranslate(delta.x(), delta.y());
    } else {
        const bool xActive = hasLeft(handle) || hasRight(handle);
        const bool yActive = hasTop(handle) || hasBottom(handle);
        if ((xActive && m_baseBounds.width() <= 1e-12)
            || (yActive && m_baseBounds.height() <= 1e-12)) return false;
        const bool centered = modifiers & Qt::AltModifier;
        const QPointF edge = handlePoint(handle, m_baseBounds);
        const QPointF pivot(
            centered ? m_baseBounds.center().x()
                     : hasLeft(handle) ? m_baseBounds.right()
                     : hasRight(handle) ? m_baseBounds.left() : m_baseBounds.center().x(),
            centered ? m_baseBounds.center().y()
                     : hasTop(handle) ? m_baseBounds.bottom()
                     : hasBottom(handle) ? m_baseBounds.top() : m_baseBounds.center().y());
        qreal sx = xActive ? (edge.x() + delta.x() - pivot.x()) / (edge.x() - pivot.x()) : 1;
        qreal sy = yActive ? (edge.y() + delta.y() - pivot.y()) / (edge.y() - pivot.y()) : 1;
        if ((modifiers & Qt::ShiftModifier) && xActive && yActive) {
            if (std::abs(sx - 1) >= std::abs(sy - 1)) sy = sx;
            else sx = sy;
        }
        if (!std::isfinite(sx) || !std::isfinite(sy)
            || std::abs(sx) <= 1e-12 || std::abs(sy) <= 1e-12) return false;
        world = QTransform::fromTranslate(-pivot.x(), -pivot.y())
                * QTransform::fromScale(sx, sy)
                * QTransform::fromTranslate(pivot.x(), pivot.y());
    }
    CanvasEdit preview;
    preview.label = handle == QLatin1String("move") ? tr("Move selection") : tr("Resize selection");
    for (const CanvasObject &before : std::as_const(m_before)) {
        CanvasObject after = before;
        after.transform = before.transform * world;
        if (!CanvasGeometry::isRenderable(after)) return false;
        if (after.transform != before.transform)
            preview.changes.append({before.id, before, after});
    }
    m_previewEdit = preview;
    m_previewBounds = world.mapRect(m_baseBounds);
    emit previewChanged();
    return true;
}

bool CanvasTransformController::finish(const QString &handle)
{
    if (!m_active || handle != m_handle) return false;
    if (!m_document || m_document->revision() != m_revision) {
        cancel();
        return false;
    }
    const CanvasEdit edit = m_previewEdit;
    bool committed = false;
    if (!edit.isEmpty()) {
        m_committing = true;
        committed = m_document->commit(edit);
        m_committing = false;
    }
    clearPreview();
    emit changed();
    emit gestureFinished(committed);
    return edit.isEmpty() || committed;
}

bool CanvasTransformController::cancel(const QString &handle)
{
    if (!m_active || (!handle.isEmpty() && handle != m_handle)) return false;
    clearPreview();
    emit gestureCanceled();
    return true;
}

void CanvasTransformController::clearPreview()
{
    m_before.clear();
    m_previewEdit = {};
    m_handle.clear();
    m_active = false;
    emit previewChanged();
}

bool CanvasTransformController::nudge(const QString &handle, qreal viewDx,
                                      qreal viewDy, int modifiers)
{
    if (!m_viewState || !std::isfinite(viewDx) || !std::isfinite(viewDy)
        || m_viewState->zoom() <= 0) return false;
    const qreal angle = qDegreesToRadians(m_viewState->rotation());
    const qreal cosine = std::cos(angle);
    const qreal sine = std::sin(angle);
    const QPointF delta((cosine * viewDx + sine * viewDy) / m_viewState->zoom(),
                        (-sine * viewDx + cosine * viewDy) / m_viewState->zoom());
    const QPointF start = handlePoint(handle, selectionBounds());
    if (!update(handle, start, start + delta, modifiers)) return false;
    return finish(handle);
}

} // namespace QindaTK
