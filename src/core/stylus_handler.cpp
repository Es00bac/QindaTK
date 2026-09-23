// SPDX-License-Identifier: LGPL-3.0-or-later
#include "stylus_handler.h"

#include <QtQuick/private/qquickpointerdevicehandler_p.h>

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

// Qt Quick's private event hook stays in this implementation-only sibling of
// the QML facade. Its QObject and handler parent is the canvas item itself.
class StylusInputHandler final : public QQuickPointerDeviceHandler {
public:
    StylusInputHandler(StylusHandler *facade, QQuickItem *parent)
        : QQuickPointerDeviceHandler(parent), m_facade(facade)
    {
    }

protected:
    bool wantsPointerEvent(QPointerEvent *event) override
    {
        if (!QQuickPointerDeviceHandler::wantsPointerEvent(event)
            || event->pointCount() != 1) {
            return false;
        }
        const bool tablet = isTablet(event);
        if (!tablet && !isMouse(event)) {
            return false;
        }
        if (!tablet) {
            // AGENT-GUARD: tablet delivery is followed by a synthesized
            // mouse event; accepting both would double every pen sample.
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->source() != Qt::MouseEventNotSynthesized) {
                return false;
            }
        }
        const QEventPoint &point = event->point(0);
        if (active()) {
            return true;
        }
        return event->isBeginEvent() && parentContains(point);
    }

    void handlePointerEventImpl(QPointerEvent *event) override
    {
        QEventPoint &point = event->point(0);
        if (event->isBeginEvent()) {
            if (!setExclusiveGrab(event, point, true)) {
                event->setAccepted(false);
                return;
            }
            const StylusSample sample = sampleFrom(event, point);
            m_facade->m_last = sample;
            setActive(true);
            m_facade->dispatchBegin(sample);
        } else if (event->isUpdateEvent()) {
            if (!active()) {
                return;
            }
            m_facade->dispatchMove(sampleFrom(event, point));
        } else if (event->isEndEvent()) {
            if (!active()) {
                return;
            }
            m_facade->dispatchEnd(sampleFrom(event, point));
            setActive(false);
            setExclusiveGrab(event, point, false);
        }
        event->setAccepted(true);
    }

    void onActiveChanged() override
    {
        if (m_facade) {
            m_facade->setActiveFromInputHandler(active());
        }
    }

private:
    [[nodiscard]] StylusSample sampleFrom(QPointerEvent *event, const QEventPoint &point) const
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
            // The event accessors preserve pressure and tilt; the event point
            // reports zero pressure for some real tablet events on Qt 6.11.
            sample.pressure = tablet->pressure();
            sample.tiltX = tablet->xTilt();
            sample.tiltY = tablet->yTilt();
            sample.rotation = tablet->rotation();
            sample.type = tablet->pointingDevice() != nullptr
                ? tablet->pointingDevice()->pointerType()
                : QPointingDevice::PointerType::Pen;
            return sample;
        }
        sample.pressure = m_facade ? m_facade->mousePressure() : 0.6;
        sample.type = QPointingDevice::PointerType::Generic;
        sample.synthesizedFromMouse = true;
        return sample;
    }

    QPointer<StylusHandler> m_facade;
};

StylusHandler::StylusHandler(QObject *parent)
    : QObject(parent)
{
}

StylusHandler::~StylusHandler()
{
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        m_inputHandler = nullptr;
        delete handler;
    }
}

void StylusHandler::classBegin()
{
}

void StylusHandler::componentComplete()
{
    m_componentComplete = true;
    if (!m_parentItem) {
        m_parentItem = qobject_cast<QQuickItem *>(QObject::parent());
    }
    ensureInputHandler();
}

void StylusHandler::setParentItem(QQuickItem *parent)
{
    if (m_parentItem == parent) {
        return;
    }
    m_parentItem = parent;
    if (QObject::parent() != parent) {
        QObject::setParent(parent);
    }
    ensureInputHandler();
    emit parentChanged();
}

QQuickItem *StylusHandler::target() const
{
    return m_targetWasSet ? m_target.data() : m_parentItem.data();
}

void StylusHandler::setTarget(QQuickItem *targetItem)
{
    if (m_targetWasSet && m_target == targetItem) {
        return;
    }
    m_targetWasSet = true;
    m_target = targetItem;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setTarget(targetItem);
    }
    emit targetChanged();
}

void StylusHandler::setEnabled(bool enabledValue)
{
    if (m_enabled == enabledValue) {
        return;
    }
    m_enabled = enabledValue;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setEnabled(enabledValue);
    }
    emit enabledChanged();
}

void StylusHandler::setMousePressure(qreal pressureValue)
{
    const qreal next = qBound(0.0, pressureValue, 1.0);
    if (qFuzzyCompare(next, m_mousePressure)) {
        return;
    }
    m_mousePressure = next;
    emit mousePressureChanged();
}

void StylusHandler::setAcceptedDevices(QInputDevice::DeviceTypes devices)
{
    if (m_acceptedDevices == devices) {
        return;
    }
    m_acceptedDevices = devices;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setAcceptedDevices(devices);
    }
    emit acceptedDevicesChanged();
}

void StylusHandler::setAcceptedPointerTypes(QPointingDevice::PointerTypes types)
{
    if (m_acceptedPointerTypes == types) {
        return;
    }
    m_acceptedPointerTypes = types;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setAcceptedPointerTypes(types);
    }
    emit acceptedPointerTypesChanged();
}

void StylusHandler::setAcceptedButtons(Qt::MouseButtons buttonsValue)
{
    if (m_acceptedButtons == buttonsValue) {
        return;
    }
    m_acceptedButtons = buttonsValue;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setAcceptedButtons(buttonsValue);
    }
    emit acceptedButtonsChanged();
}

void StylusHandler::setAcceptedModifiers(Qt::KeyboardModifiers modifiers)
{
    if (m_acceptedModifiers == modifiers) {
        return;
    }
    m_acceptedModifiers = modifiers;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setAcceptedModifiers(modifiers);
    }
    emit acceptedModifiersChanged();
}

void StylusHandler::setMargin(qreal marginValue)
{
    if (qFuzzyCompare(marginValue + 1.0, m_margin + 1.0)) {
        return;
    }
    m_margin = marginValue;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setMargin(marginValue);
    }
    emit marginChanged();
}

void StylusHandler::setGrabPermissions(GrabPermissions permissions)
{
    if (m_grabPermissions == permissions) {
        return;
    }
    m_grabPermissions = permissions;
    if (auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data())) {
        handler->setGrabPermissions(QQuickPointerHandler::GrabPermissions(int(permissions)));
    }
    emit grabPermissionChanged();
}

void StylusHandler::ensureInputHandler()
{
    if (!m_componentComplete) {
        return;
    }
    auto *handler = static_cast<StylusInputHandler *>(m_inputHandler.data());
    if (handler && handler->parentItem() == m_parentItem) {
        handler->setTarget(target());
        handler->setEnabled(m_enabled);
        handler->setAcceptedDevices(m_acceptedDevices);
        handler->setAcceptedPointerTypes(m_acceptedPointerTypes);
        handler->setAcceptedButtons(m_acceptedButtons);
        handler->setAcceptedModifiers(m_acceptedModifiers);
        handler->setMargin(m_margin);
        handler->setGrabPermissions(QQuickPointerHandler::GrabPermissions(int(m_grabPermissions)));
        return;
    }
    if (handler) {
        delete handler;
        m_inputHandler = nullptr;
        setActiveFromInputHandler(false);
    }
    if (!m_parentItem) {
        return;
    }
    handler = new StylusInputHandler(this, m_parentItem);
    m_inputHandler = handler;
    handler->setTarget(target());
    handler->setEnabled(m_enabled);
    handler->setAcceptedDevices(m_acceptedDevices);
    handler->setAcceptedPointerTypes(m_acceptedPointerTypes);
    handler->setAcceptedButtons(m_acceptedButtons);
    handler->setAcceptedModifiers(m_acceptedModifiers);
    handler->setMargin(m_margin);
    handler->setGrabPermissions(QQuickPointerHandler::GrabPermissions(int(m_grabPermissions)));
    connect(handler, &QQuickPointerHandler::canceled, this, [this](QEventPoint point) {
        emit canceled(point);
    });
    connect(handler, &QQuickPointerHandler::grabChanged, this,
            [this](QPointingDevice::GrabTransition transition, QEventPoint point) {
        emit grabChanged(transition, point);
    });
    connect(handler, &QObject::destroyed, this, [this] {
        m_inputHandler = nullptr;
        setActiveFromInputHandler(false);
    });
}

void StylusHandler::setActiveFromInputHandler(bool activeValue)
{
    if (m_active == activeValue) {
        return;
    }
    m_active = activeValue;
    emit activeChanged();
}

void StylusHandler::dispatchBegin(const StylusSample &sample)
{
    m_last = sample;
    emit sampleChanged();
    emit began(m_last);
}

void StylusHandler::dispatchMove(const StylusSample &sample)
{
    m_last = sample;
    emit sampleChanged();
    emit moved(m_last);
}

void StylusHandler::dispatchEnd(const StylusSample &sample)
{
    m_last = sample;
    emit sampleChanged();
    emit ended(m_last);
}

} // namespace QindaTK
