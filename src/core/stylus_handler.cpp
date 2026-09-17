// SPDX-License-Identifier: LGPL-3.0-or-later
#include "stylus_handler.h"

#include <QMouseEvent>
#include <QTabletEvent>

namespace QindaTK {

namespace {

bool isTablet(const QPointerEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        return true;
    default:
        return false;
    }
}

bool isMouse(const QPointerEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease:
        return true;
    default:
        return false;
    }
}

} // namespace

QString StylusSample::pointerTypeName() const
{
    if (synthesizedFromMouse) {
        return QStringLiteral("mouse");
    }
    switch (type) {
    case QPointingDevice::PointerType::Pen:
        return QStringLiteral("pen");
    case QPointingDevice::PointerType::Eraser:
        return QStringLiteral("eraser");
    case QPointingDevice::PointerType::Generic:
        return QStringLiteral("mouse");
    default:
        return QStringLiteral("other");
    }
}

StylusHandler::StylusHandler(QQuickItem *parent)
    : QQuickPointerDeviceHandler(parent)
{
    // Pens and the mouse fallback; never touch (a resting palm is touch).
    setAcceptedDevices(QInputDevice::DeviceType::Stylus | QInputDevice::DeviceType::Mouse
                       | QInputDevice::DeviceType::Airbrush | QInputDevice::DeviceType::Puck);
    setAcceptedPointerTypes(QPointingDevice::PointerType::Pen | QPointingDevice::PointerType::Eraser
                            | QPointingDevice::PointerType::Generic);
    setAcceptedButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    // Hold on to the point once a stroke began: a Flickable above must not
    // steal it mid-stroke, and nothing may take it over.
    setGrabPermissions(QQuickPointerHandler::CanTakeOverFromItems
                       | QQuickPointerHandler::ApprovesCancellation);
}

void StylusHandler::setMousePressure(qreal pressure)
{
    const qreal next = qBound(0.0, pressure, 1.0);
    if (qFuzzyCompare(next, m_mousePressure)) {
        return;
    }
    m_mousePressure = next;
    emit mousePressureChanged();
}

bool StylusHandler::wantsPointerEvent(QPointerEvent *event)
{
    if (!QQuickPointerDeviceHandler::wantsPointerEvent(event)) {
        return false;
    }
    if (event->pointCount() != 1) {
        return false;
    }
    const bool tablet = isTablet(event);
    if (!tablet && !isMouse(event)) {
        return false;
    }
    if (!tablet) {
        // AGENT-GUARD: see the header — the synthesised twin of a tablet
        // sample must not become a second sample.
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->source() != Qt::MouseEventNotSynthesized) {
            return false;
        }
    }
    const QEventPoint &point = event->point(0);
    if (active()) {
        return true; // the rest of the gesture, wherever the pen goes
    }
    if (!event->isBeginEvent()) {
        return false; // a move or release that did not begin here
    }
    return parentContains(point);
}

StylusSample StylusHandler::sampleFrom(QPointerEvent *event, const QEventPoint &point) const
{
    StylusSample sample;
    sample.position = eventPos(point);
    sample.scenePosition = point.scenePosition();
    sample.timestamp = point.timestamp();
    sample.modifiers = int(event->modifiers());
    if (auto *single = dynamic_cast<QSinglePointEvent *>(event)) {
        sample.buttons = int(single->buttons());
    }
    if (isTablet(event)) {
        auto *tablet = static_cast<QTabletEvent *>(event);
        // AGENT-NOTE: the tablet event's own accessors, not the event
        // point's: the point reported pressure 0 for a real 0.8 press in
        // the verification probe (Qt 6.11.1).
        sample.pressure = tablet->pressure();
        sample.tiltX = tablet->xTilt();
        sample.tiltY = tablet->yTilt();
        sample.rotation = tablet->rotation();
        sample.type = tablet->pointingDevice() != nullptr
            ? tablet->pointingDevice()->pointerType()
            : QPointingDevice::PointerType::Pen;
        return sample;
    }
    sample.pressure = m_mousePressure;
    sample.type = QPointingDevice::PointerType::Generic;
    sample.synthesizedFromMouse = true;
    return sample;
}

void StylusHandler::handlePointerEventImpl(QPointerEvent *event)
{
    QEventPoint &point = event->point(0);
    if (event->isBeginEvent()) {
        m_last = sampleFrom(event, point);
        setExclusiveGrab(event, point, true);
        setActive(true);
        emit sampleChanged();
        emit began(m_last);
    } else if (event->isUpdateEvent()) {
        if (!active()) {
            return;
        }
        m_last = sampleFrom(event, point);
        emit sampleChanged();
        emit moved(m_last);
    } else if (event->isEndEvent()) {
        if (!active()) {
            return;
        }
        m_last = sampleFrom(event, point);
        emit sampleChanged();
        emit ended(m_last);
        setActive(false);
        setExclusiveGrab(event, point, false);
    }
    event->setAccepted(true);
}

} // namespace QindaTK
