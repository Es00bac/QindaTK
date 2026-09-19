// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include "theme_ramp.h"

namespace QindaTK {

// AGENT-CONTRACT: a magnitude bar for telemetry -- one CPU core, a
// filesystem's used space, a fan against its maximum. Distinct from
// ProgressBar, which reports how far a *task* has run: a Meter reports how
// hard something is working right now, so it is coloured by its reading and
// has no indeterminate state.
//
// AGENT-NOTE: this is a QQuickPaintedItem, not a scene-graph item like Graph.
// A meter is small and cheap to raster, and QQuickPaintedItem draws correctly
// under Qt's software adaptation for free -- Graph needs two drawing paths
// precisely because it is not one of these.
class Meter : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(qreal from READ from WRITE setFrom NOTIFY rangeChanged)
    Q_PROPERTY(qreal to READ to WRITE setTo NOTIFY rangeChanged)
    Q_PROPERTY(qreal position READ position NOTIFY valueChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(QColor trackColor READ trackColor WRITE setTrackColor NOTIFY styleChanged)
    Q_PROPERTY(QindaTK::ThemeRamp *ramp READ ramp WRITE setRamp NOTIFY styleChanged)
    Q_PROPERTY(RampMode rampMode READ rampMode WRITE setRampMode NOTIFY styleChanged)
    Q_PROPERTY(int segments READ segments WRITE setSegments NOTIFY styleChanged)
    Q_PROPERTY(qreal segmentGap READ segmentGap WRITE setSegmentGap NOTIFY styleChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY styleChanged)
    Q_PROPERTY(bool vertical READ vertical WRITE setVertical NOTIFY styleChanged)
    Q_PROPERTY(QColor valueColor READ valueColor NOTIFY valueChanged)

public:
    enum RampMode {
        // Each segment takes the ramp colour of its own position, so a bar at
        // 80% runs calm-to-hot across its length. This is btop's bar.
        ByPosition,
        // The whole fill takes the ramp colour of the current reading, so the
        // bar changes colour as one block.
        ByValue,
    };
    Q_ENUM(RampMode)

    explicit Meter(QQuickItem *parent = nullptr);

    [[nodiscard]] qreal value() const { return m_value; }
    void setValue(qreal value);
    [[nodiscard]] qreal from() const { return m_from; }
    void setFrom(qreal from);
    [[nodiscard]] qreal to() const { return m_to; }
    void setTo(qreal to);
    // The reading as a fraction of the range, clamped to [0, 1].
    [[nodiscard]] qreal position() const;
    [[nodiscard]] QColor color() const { return m_color; }
    void setColor(const QColor &color);
    [[nodiscard]] QColor trackColor() const { return m_trackColor; }
    void setTrackColor(const QColor &color);
    [[nodiscard]] ThemeRamp *ramp() const { return m_ramp; }
    void setRamp(ThemeRamp *ramp);
    [[nodiscard]] RampMode rampMode() const { return m_rampMode; }
    void setRampMode(RampMode mode);
    [[nodiscard]] int segments() const { return m_segments; }
    void setSegments(int segments);
    [[nodiscard]] qreal segmentGap() const { return m_segmentGap; }
    void setSegmentGap(qreal gap);
    [[nodiscard]] qreal radius() const { return m_radius; }
    void setRadius(qreal radius);
    [[nodiscard]] bool vertical() const { return m_vertical; }
    void setVertical(bool vertical);
    // The colour the fill resolves to at the current reading. Labels beside a
    // meter bind to this so the number agrees with the bar.
    [[nodiscard]] QColor valueColor() const;

    void paint(QPainter *painter) override;

signals:
    void valueChanged();
    void rangeChanged();
    void styleChanged();

private:
    void restyle();

    QColor m_color;
    QColor m_trackColor;
    ThemeRamp *m_ramp = nullptr;
    qreal m_value = 0;
    qreal m_from = 0;
    qreal m_to = 100;
    qreal m_segmentGap = 1;
    qreal m_radius = 1;
    int m_segments = 0;
    RampMode m_rampMode = ByPosition;
    bool m_vertical = false;
};

} // namespace QindaTK
