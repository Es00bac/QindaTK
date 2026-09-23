// SPDX-License-Identifier: LGPL-3.0-or-later
// Input routing of CanvasSurface. Navigation (middle drag, wheel, trackpad,
// two-finger touch, native gestures, keys) moves the view pose; primary
// pointer gestures go to the ToolController. See docs/canvas.md#input.
#include "canvas_surface.h"

#include <QCursor>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QtMath>
#include <cmath>

namespace QindaTK {

namespace {

CanvasDevice deviceOf(const QPointerEvent *event)
{
    const QPointingDevice *device = event->pointingDevice();
    if (!device) {
        return CanvasDevice::Mouse;
    }
    if (device->pointerType() == QPointingDevice::PointerType::Eraser) {
        return CanvasDevice::Eraser;
    }
    switch (device->type()) {
    case QInputDevice::DeviceType::Stylus:
    case QInputDevice::DeviceType::Airbrush:
        return CanvasDevice::Pen;
    case QInputDevice::DeviceType::TouchScreen:
        return CanvasDevice::Touch;
    default:
        return CanvasDevice::Mouse;
    }
}

qreal pressureOf(const QPointerEvent *event)
{
    if (event->points().isEmpty()) {
        return 1.0;
    }
    const qreal p = event->points().constFirst().pressure();
    return p > 0 ? p : 1.0;
}

} // namespace

CanvasToolInput CanvasSurface::toolInput(QPointF viewPoint, Qt::KeyboardModifiers modifiers,
                                         CanvasDevice device, qreal pressure) const
{
    CanvasToolInput input;
    input.viewPoint = viewPoint;
    input.documentPoint = mapToDocument(viewPoint);
    input.pressure = pressure;
    input.zoom = zoom();
    input.modifiers = modifiers;
    input.device = device;
    return input;
}

void CanvasSurface::cancelPointer(const QString &reason)
{
    if (m_pointer == Pointer::Tool && m_tools && m_tools->gestureOwner() == m_viewId) {
        m_tools->cancel(reason);
    }
    m_pointer = Pointer::None;
    m_touchNavigating = false;
    setKeepMouseGrab(false);
    setKeepTouchGrab(false);
}

void CanvasSurface::updateCursor()
{
#if QT_CONFIG(cursor)
    setCursor(m_tools ? m_tools->cursor() : Qt::ArrowCursor);
#endif
}

void CanvasSurface::mousePressEvent(QMouseEvent *event)
{
    if (!m_interactive || !isEnabled()) {
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    if (m_pointer != Pointer::None) {
        event->accept(); // a second button during a gesture is ignored
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_pointer = Pointer::Pan;
        m_lastPan = event->position();
        setKeepMouseGrab(true);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_tools
        && m_tools->beginGesture(m_viewId,
                                 toolInput(event->position(), event->modifiers(),
                                           deviceOf(event), pressureOf(event)),
                                 this, this)) {
        m_pointer = Pointer::Tool;
        setKeepMouseGrab(true);
    }
    event->accept();
}

void CanvasSurface::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pointer == Pointer::Pan) {
        panBy(event->position().x() - m_lastPan.x(), event->position().y() - m_lastPan.y());
        m_lastPan = event->position();
    } else if (m_pointer == Pointer::Tool && m_tools) {
        if (!m_tools->updateGesture(m_viewId, toolInput(event->position(), event->modifiers(),
                                                        deviceOf(event), pressureOf(event)))) {
            m_pointer = Pointer::None; // the gesture ended elsewhere (Escape, tool switch)
            setKeepMouseGrab(false);
        }
    }
    event->accept();
}

void CanvasSurface::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pointer == Pointer::Tool && m_tools) {
        m_tools->endGesture(m_viewId, toolInput(event->position(), event->modifiers(),
                                                deviceOf(event), pressureOf(event)));
    }
    m_pointer = Pointer::None;
    setKeepMouseGrab(false);
    updateHover(event->position());
    event->accept();
}

void CanvasSurface::mouseUngrabEvent()
{
    if (m_pointer == Pointer::Tool && m_tools) {
        m_tools->lostGrab(m_viewId);
    }
    m_pointer = Pointer::None;
    setKeepMouseGrab(false);
}

void CanvasSurface::updateHover(QPointF viewPoint)
{
    QVariantMap next;
    CanvasObjectId id = 0;
    if (m_document) {
        CanvasHitPolicy policy;
        policy.maxResults = 1;
        const auto hits = HitTestService::hitPoint(
            *m_document, mapToDocument(viewPoint),
            HitTestService::documentTolerance(m_hitTolerance, zoom()), policy);
        if (!hits.isEmpty()) {
            next = HitTestService::toVariant(hits.first());
            id = hits.first().object;
        }
    }
    if (next != m_hoverHit) {
        m_hoverHit = next;
        m_hoverObject = id;
        emit hoverChanged();
    }
}

void CanvasSurface::hoverMoveEvent(QHoverEvent *event)
{
    if (m_pointer == Pointer::None) {
        updateHover(event->position());
    }
    event->ignore();
}

void CanvasSurface::hoverLeaveEvent(QHoverEvent *event)
{
    if (!m_hoverHit.isEmpty()) {
        m_hoverHit.clear();
        m_hoverObject = 0;
        emit hoverChanged();
    }
    event->ignore();
}

void CanvasSurface::touchEvent(QTouchEvent *event)
{
    if (!m_interactive || !isEnabled()) {
        event->ignore();
        return;
    }
    if (event->type() == QEvent::TouchCancel) {
        cancelPointer(QStringLiteral("touch-cancel"));
        event->accept();
        return;
    }
    QList<QEventPoint> live;
    for (const QEventPoint &p : event->points()) {
        if (p.state() != QEventPoint::Released) {
            live << p;
        }
    }
    if (live.size() >= 2) {
        // A second finger turns any tool gesture into navigation: the
        // partial stroke is canceled, never committed.
        if (m_pointer == Pointer::Tool) {
            cancelPointer(QStringLiteral("navigation-override"));
        }
        const QPointF a = live.at(0).position();
        const QPointF b = live.at(1).position();
        const QPointF centroid = (a + b) / 2.0;
        const qreal distance = std::hypot(b.x() - a.x(), b.y() - a.y());
        const qreal angle = std::atan2(b.y() - a.y(), b.x() - a.x());
        if (m_touchNavigating) {
            panBy(centroid.x() - m_touchCentroid.x(), centroid.y() - m_touchCentroid.y());
            if (m_touchDistance > 0 && distance > 0) {
                zoomAbout(centroid.x(), centroid.y(), distance / m_touchDistance);
            }
            if (m_touchRotation) {
                qreal delta = qRadiansToDegrees(angle - m_touchAngle);
                delta = CanvasTransform::normalizeRotation(delta);
                rotateBy(delta, centroid.x(), centroid.y());
            }
        }
        m_touchNavigating = true;
        m_touchCentroid = centroid;
        m_touchDistance = distance;
        m_touchAngle = angle;
        setKeepTouchGrab(true);
    } else if (live.size() == 1 && !m_touchNavigating) {
        const QEventPoint &p = live.constFirst();
        const CanvasToolInput input = toolInput(p.position(), event->modifiers(),
                                                CanvasDevice::Touch, p.pressure() > 0 ? p.pressure() : 1.0);
        if (p.state() == QEventPoint::Pressed && m_pointer == Pointer::None && m_tools
            && m_tools->beginGesture(m_viewId, input, this, this)) {
            m_pointer = Pointer::Tool;
            setKeepTouchGrab(true);
        } else if (m_pointer == Pointer::Tool && m_tools) {
            m_tools->updateGesture(m_viewId, input);
        }
    } else if (live.isEmpty()) {
        if (m_pointer == Pointer::Tool && m_tools && !event->points().isEmpty()) {
            const QEventPoint &p = event->points().constFirst();
            m_tools->endGesture(m_viewId, toolInput(p.position(), event->modifiers(),
                                                    CanvasDevice::Touch, 1.0));
        }
        m_pointer = Pointer::None;
        m_touchNavigating = false;
        setKeepTouchGrab(false);
    }
    event->accept();
}

void CanvasSurface::touchUngrabEvent()
{
    cancelPointer(QStringLiteral("lost-grab"));
}

void CanvasSurface::wheelEvent(QWheelEvent *event)
{
    if (!m_interactive || !isEnabled()) {
        event->ignore();
        return;
    }
    const QPoint angle = event->angleDelta();
    const QPoint pixels = event->pixelDelta();
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & Qt::ControlModifier) {
        // Cursor-anchored zoom: the document point under the pointer stays.
        const qreal steps = angle.y() != 0 ? angle.y() / 120.0 : pixels.y() / 60.0;
        zoomAbout(event->position().x(), event->position().y(), std::pow(m_wheelZoomStep, steps));
    } else if (mods & Qt::AltModifier) {
        const int raw = angle.y() != 0 ? angle.y() : angle.x();
        rotateBy(m_rotationStep * raw / 120.0, event->position().x(), event->position().y());
    } else if (!pixels.isNull()) {
        panBy(pixels.x(), pixels.y()); // trackpad: content follows the fingers
    } else {
        qreal dx = angle.x() / 120.0 * m_panStep;
        qreal dy = angle.y() / 120.0 * m_panStep;
        if ((mods & Qt::ShiftModifier) && dx == 0) {
            std::swap(dx, dy);
        }
        panBy(dx, dy);
    }
    event->accept();
}

bool CanvasSurface::handleNativeGesture(QEvent *e)
{
    auto *event = static_cast<QNativeGestureEvent *>(e);
    const QPointF at = event->position();
    switch (event->gestureType()) {
    case Qt::ZoomNativeGesture:
        zoomAbout(at.x(), at.y(), 1.0 + event->value());
        break;
    case Qt::RotateNativeGesture:
        if (m_touchRotation) {
            rotateBy(-event->value(), at.x(), at.y()); // platform: positive = counter-clockwise
        }
        break;
    case Qt::PanNativeGesture:
        panBy(event->delta().x(), event->delta().y());
        break;
    case Qt::SmartZoomNativeGesture:
        fitDocument();
        break;
    default:
        break;
    }
    event->accept();
    return true;
}

bool CanvasSurface::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture && m_interactive) {
        return handleNativeGesture(event);
    }
    if (event->type() == QEvent::FocusOut && m_spaceHeld && m_tools) {
        m_spaceHeld = false;
        m_tools->popTemporaryTool();
    }
    return QQuickItem::event(event);
}

void CanvasSurface::keyPressEvent(QKeyEvent *event)
{
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    if (!m_interactive && event->key() != Qt::Key_Escape) {
        event->ignore();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Escape:
        if (m_tools && m_tools->gestureActive()) {
            m_tools->escape();
            m_pointer = Pointer::None;
            setKeepMouseGrab(false);
            event->accept();
            return;
        }
        break;
    case Qt::Key_Space:
        if (m_tools && m_interactive) {
            if (!event->isAutoRepeat() && !m_spaceHeld) {
                m_spaceHeld = m_tools->pushTemporaryTool(QStringLiteral("pan"));
            }
            event->accept();
            return;
        }
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomOut();
        event->accept();
        return;
    case Qt::Key_0:
        if (ctrl) {
            fitDocument();
            event->accept();
            return;
        }
        break;
    case Qt::Key_1:
        if (ctrl) {
            setZoomCentered(1.0);
            event->accept();
            return;
        }
        break;
    case Qt::Key_R:
        if (!ctrl) {
            rotateBy(event->modifiers() & Qt::ShiftModifier ? -m_rotationStep : m_rotationStep);
            event->accept();
            return;
        }
        break;
    case Qt::Key_Left:
        panBy(m_panStep, 0);
        event->accept();
        return;
    case Qt::Key_Right:
        panBy(-m_panStep, 0);
        event->accept();
        return;
    case Qt::Key_Up:
        panBy(0, m_panStep);
        event->accept();
        return;
    case Qt::Key_Down:
        panBy(0, -m_panStep);
        event->accept();
        return;
    default:
        break;
    }
    event->ignore();
}

void CanvasSurface::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && m_spaceHeld && m_tools) {
        m_spaceHeld = false;
        m_tools->popTemporaryTool();
        event->accept();
        return;
    }
    event->ignore();
}

} // namespace QindaTK
