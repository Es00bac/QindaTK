// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_transform.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#synchronized-views): the navigation pose of
// one or more CanvasViews. Views that share one CanvasViewState show the
// same document point at their centres with the same zoom and rotation;
// each keeps its own size and device pixel ratio. Navigation writes only
// this object -- never the document.
class CanvasViewState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal centerX READ centerX WRITE setCenterX NOTIFY changed)
    Q_PROPERTY(qreal centerY READ centerY WRITE setCenterY NOTIFY changed)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY changed)
    Q_PROPERTY(qreal rotation READ rotation WRITE setRotation NOTIFY changed)
    Q_PROPERTY(qreal minZoom READ minZoom WRITE setMinZoom NOTIFY changed)
    Q_PROPERTY(qreal maxZoom READ maxZoom WRITE setMaxZoom NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit CanvasViewState(QObject *parent = nullptr) : QObject(parent) {}

    const CanvasViewPose &pose() const { return m_pose; }
    // Sanitizes (clamps zoom, normalizes rotation, rejects non-finite).
    void setPose(const CanvasViewPose &pose);
    CanvasZoomLimits limits() const { return m_limits; }

    qreal centerX() const { return m_pose.center.x(); }
    qreal centerY() const { return m_pose.center.y(); }
    qreal zoom() const { return m_pose.zoom; }
    qreal rotation() const { return m_pose.rotation; }
    qreal minZoom() const { return m_limits.minimum; }
    qreal maxZoom() const { return m_limits.maximum; }
    int revision() const { return m_revision; }
    void setCenterX(qreal x);
    void setCenterY(qreal y);
    void setZoom(qreal zoom);
    void setRotation(qreal degrees);
    void setMinZoom(qreal zoom);
    void setMaxZoom(qreal zoom);

signals:
    void changed();

private:
    CanvasViewPose m_pose;
    CanvasZoomLimits m_limits;
    int m_revision = 0;
};

} // namespace QindaTK
