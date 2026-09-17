// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Tk.StylusHandler: pen input for a canvas item, with pressure, tilt,
// rotation and the pen/eraser pointer type.
//
// AGENT-NOTE: QtQuick never hands a QTabletEvent to QQuickItem::event(),
// and a QML PointHandler only ever sees the mouse events Qt synthesises
// from unhandled tablet events (pressure 0, device "core pointer"). The one
// place the raw tablet event is reachable is a QQuickPointerDeviceHandler
// subclass, which is why this class exists and why it includes a private
// header (the second private-API use in the toolkit after LayoutInfo;
// docs/decisions.md D-013). Verified against Qt 6.11.1 with
// QWindowSystemInterface::handleTabletEvent.
//
// AGENT-GUARD: a handler receives a LOCALISED COPY of the event, so
// accepting it here does not stop Qt from synthesising a mouse event for
// the same tablet sample. The mouse fallback therefore only accepts mouse
// events whose source is MouseEventNotSynthesized, or every stroke would be
// sampled twice.

#include <QtQuick/private/qquickpointerdevicehandler_p.h>

#include <QPointF>
#include <QPointingDevice>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

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

class StylusHandler : public QQuickPointerDeviceHandler {
    Q_OBJECT
    QML_NAMED_ELEMENT(StylusHandler)
    // Live values of the most recent sample (bindable from QML).
    Q_PROPERTY(QPointF position READ position NOTIFY sampleChanged)
    Q_PROPERTY(qreal pressure READ pressure NOTIFY sampleChanged)
    Q_PROPERTY(qreal tiltX READ tiltX NOTIFY sampleChanged)
    Q_PROPERTY(qreal tiltY READ tiltY NOTIFY sampleChanged)
    Q_PROPERTY(qreal rotation READ rotation NOTIFY sampleChanged)
    Q_PROPERTY(QString pointerType READ pointerType NOTIFY sampleChanged)
    Q_PROPERTY(int buttons READ buttons NOTIFY sampleChanged)
    // Pressure reported for the mouse fallback (a mouse has none).
    Q_PROPERTY(qreal mousePressure READ mousePressure WRITE setMousePressure NOTIFY mousePressureChanged)
public:
    explicit StylusHandler(QQuickItem *parent = nullptr);

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

signals:
    // The gesture: one began, any number of moved, one ended. `canceled`
    // (from QQuickPointerHandler) fires when a grab is taken away; treat it
    // as an ended without a final sample.
    void began(const QindaTK::StylusSample &sample);
    void moved(const QindaTK::StylusSample &sample);
    void ended(const QindaTK::StylusSample &sample);
    void sampleChanged();
    void mousePressureChanged();

protected:
    bool wantsPointerEvent(QPointerEvent *event) override;
    void handlePointerEventImpl(QPointerEvent *event) override;

private:
    [[nodiscard]] StylusSample sampleFrom(QPointerEvent *event, const QEventPoint &point) const;

    StylusSample m_last;
    qreal m_mousePressure = 0.6;
};

} // namespace QindaTK
