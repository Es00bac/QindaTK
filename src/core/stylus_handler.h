// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QInputDevice>
#include <QObject>
#include <QPointF>
#include <QPointingDevice>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class StylusInputHandler;

// One pen sample in the parent item's coordinates. A value type in QML:
// `onMoved: (sample) => ink.add(sample.position, sample.pressure)`.
struct StylusSample {
    Q_GADGET
    QML_VALUE_TYPE(stylusSample)
    Q_PROPERTY(QPointF position MEMBER position)
    Q_PROPERTY(QPointF scenePosition MEMBER scenePosition)
    Q_PROPERTY(qreal pressure MEMBER pressure)
    Q_PROPERTY(qreal tiltX MEMBER tiltX)
    Q_PROPERTY(qreal tiltY MEMBER tiltY)
    Q_PROPERTY(qreal rotation MEMBER rotation)
    Q_PROPERTY(QString pointerType READ pointerTypeName)
    Q_PROPERTY(int buttons MEMBER buttons)
    Q_PROPERTY(int modifiers MEMBER modifiers)
    Q_PROPERTY(bool synthesizedFromMouse MEMBER synthesizedFromMouse)
    Q_PROPERTY(qulonglong timestamp MEMBER timestamp)
public:
    QPointF position;
    QPointF scenePosition;
    qreal pressure = 0.5;
    qreal tiltX = 0.0;
    qreal tiltY = 0.0;
    qreal rotation = 0.0;
    QPointingDevice::PointerType type = QPointingDevice::PointerType::Pen;
    int buttons = 0;
    int modifiers = 0;
    bool synthesizedFromMouse = false;
    qulonglong timestamp = 0;

    // "pen", "eraser", "mouse" (a Generic pointer) or "other".
    [[nodiscard]] QString pointerTypeName() const;
};

// Public, private-API-free proxy for the Qt pointer-device implementation.
// The implementation is parented to the QQuickItem, never to this QObject.
class StylusHandler : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_NAMED_ELEMENT(StylusHandler)
    Q_CLASSINFO("ParentProperty", "parent")
    Q_PROPERTY(QQuickItem *parent READ parentItem WRITE setParentItem NOTIFY parentChanged)
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QPointF position READ position NOTIFY sampleChanged)
    Q_PROPERTY(qreal pressure READ pressure NOTIFY sampleChanged)
    Q_PROPERTY(qreal tiltX READ tiltX NOTIFY sampleChanged)
    Q_PROPERTY(qreal tiltY READ tiltY NOTIFY sampleChanged)
    Q_PROPERTY(qreal rotation READ rotation NOTIFY sampleChanged)
    Q_PROPERTY(QString pointerType READ pointerType NOTIFY sampleChanged)
    Q_PROPERTY(int buttons READ buttons NOTIFY sampleChanged)
    Q_PROPERTY(qreal mousePressure READ mousePressure WRITE setMousePressure NOTIFY mousePressureChanged)
    Q_PROPERTY(QInputDevice::DeviceTypes acceptedDevices READ acceptedDevices WRITE setAcceptedDevices NOTIFY acceptedDevicesChanged)
    Q_PROPERTY(QPointingDevice::PointerTypes acceptedPointerTypes READ acceptedPointerTypes WRITE setAcceptedPointerTypes NOTIFY acceptedPointerTypesChanged)
    Q_PROPERTY(Qt::MouseButtons acceptedButtons READ acceptedButtons WRITE setAcceptedButtons NOTIFY acceptedButtonsChanged)
    Q_PROPERTY(Qt::KeyboardModifiers acceptedModifiers READ acceptedModifiers WRITE setAcceptedModifiers NOTIFY acceptedModifiersChanged)
    Q_PROPERTY(qreal margin READ margin WRITE setMargin NOTIFY marginChanged)
    Q_PROPERTY(GrabPermissions grabPermissions READ grabPermissions WRITE setGrabPermissions NOTIFY grabPermissionChanged)

public:
    enum GrabPermission {
        TakeOverForbidden = 0x0,
        CanTakeOverFromHandlersOfSameType = 0x01,
        CanTakeOverFromHandlersOfDifferentType = 0x02,
        CanTakeOverFromItems = 0x04,
        CanTakeOverFromAnything = 0x0F,
        ApprovesTakeOverByHandlersOfSameType = 0x10,
        ApprovesTakeOverByHandlersOfDifferentType = 0x20,
        ApprovesTakeOverByItems = 0x40,
        ApprovesCancellation = 0x80,
        ApprovesTakeOverByAnything = 0xF0
    };
    Q_ENUM(GrabPermission)
    Q_DECLARE_FLAGS(GrabPermissions, GrabPermission)
    Q_FLAG(GrabPermissions)

    explicit StylusHandler(QQuickItem *parent = nullptr);
    ~StylusHandler() override;

    void classBegin() override;
    void componentComplete() override;

    [[nodiscard]] QQuickItem *parentItem() const { return m_parentItem; }
    void setParentItem(QQuickItem *parent);
    [[nodiscard]] QQuickItem *target() const;
    void setTarget(QQuickItem *target);
    [[nodiscard]] bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    [[nodiscard]] bool active() const { return m_active; }

    [[nodiscard]] QPointF position() const { return m_last.position; }
    [[nodiscard]] qreal pressure() const { return m_last.pressure; }
    [[nodiscard]] qreal tiltX() const { return m_last.tiltX; }
    [[nodiscard]] qreal tiltY() const { return m_last.tiltY; }
    [[nodiscard]] qreal rotation() const { return m_last.rotation; }
    [[nodiscard]] QString pointerType() const { return m_last.pointerTypeName(); }
    [[nodiscard]] int buttons() const { return m_last.buttons; }
    [[nodiscard]] const StylusSample &lastSample() const { return m_last; }

    [[nodiscard]] qreal mousePressure() const { return m_mousePressure; }
    void setMousePressure(qreal pressure);
    [[nodiscard]] QInputDevice::DeviceTypes acceptedDevices() const { return m_acceptedDevices; }
    void setAcceptedDevices(QInputDevice::DeviceTypes devices);
    [[nodiscard]] QPointingDevice::PointerTypes acceptedPointerTypes() const { return m_acceptedPointerTypes; }
    void setAcceptedPointerTypes(QPointingDevice::PointerTypes types);
    [[nodiscard]] Qt::MouseButtons acceptedButtons() const { return m_acceptedButtons; }
    void setAcceptedButtons(Qt::MouseButtons buttons);
    [[nodiscard]] Qt::KeyboardModifiers acceptedModifiers() const { return m_acceptedModifiers; }
    void setAcceptedModifiers(Qt::KeyboardModifiers modifiers);
    [[nodiscard]] qreal margin() const { return m_margin; }
    void setMargin(qreal margin);
    [[nodiscard]] GrabPermissions grabPermissions() const { return m_grabPermissions; }
    void setGrabPermissions(GrabPermissions permissions);

signals:
    void began(const QindaTK::StylusSample &sample);
    void moved(const QindaTK::StylusSample &sample);
    void ended(const QindaTK::StylusSample &sample);
    void canceled(const QEventPoint &point);
    void sampleChanged();
    void mousePressureChanged();
    void parentChanged();
    void targetChanged();
    void enabledChanged();
    void activeChanged();
    void acceptedDevicesChanged();
    void acceptedPointerTypesChanged();
    void acceptedButtonsChanged();
    void acceptedModifiersChanged();
    void marginChanged();
    void grabPermissionChanged();
    void grabChanged(QPointingDevice::GrabTransition transition, const QEventPoint &point);

private:
    friend class StylusInputHandler;

    void ensureInputHandler();
    void setActiveFromInputHandler(bool active);
    void dispatchBegin(const StylusSample &sample);
    void dispatchMove(const StylusSample &sample);
    void dispatchEnd(const StylusSample &sample);

    QPointer<QQuickItem> m_parentItem;
    QPointer<QQuickItem> m_target;
    QPointer<QObject> m_inputHandler;
    bool m_targetWasSet = false;
    bool m_componentComplete = false;
    bool m_enabled = true;
    bool m_active = false;
    StylusSample m_last;
    qreal m_mousePressure = 0.6;
    QInputDevice::DeviceTypes m_acceptedDevices = QInputDevice::DeviceType::Stylus
        | QInputDevice::DeviceType::Mouse | QInputDevice::DeviceType::Airbrush
        | QInputDevice::DeviceType::Puck;
    QPointingDevice::PointerTypes m_acceptedPointerTypes = QPointingDevice::PointerType::Pen
        | QPointingDevice::PointerType::Eraser | QPointingDevice::PointerType::Generic;
    Qt::MouseButtons m_acceptedButtons = Qt::LeftButton | Qt::MiddleButton | Qt::RightButton;
    // AGENT-GUARD: Qt's pointer-device handler defaults to all modifiers.
    // Restricting this to NoModifier would silently drop Shift/Ctrl strokes.
    Qt::KeyboardModifiers m_acceptedModifiers = Qt::KeyboardModifierMask;
    qreal m_margin = 0.0;
    GrabPermissions m_grabPermissions = GrabPermissions(CanTakeOverFromItems)
        | ApprovesCancellation;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(StylusHandler::GrabPermissions)

} // namespace QindaTK
