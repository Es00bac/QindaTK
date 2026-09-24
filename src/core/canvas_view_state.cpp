// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_view_state.h"

#include <cmath>

namespace QindaTK {

void CanvasViewState::setPose(const CanvasViewPose &pose)
{
    const CanvasViewPose next = CanvasTransform::sanitized(pose, m_limits);
    if (next == m_pose) {
        return;
    }
    m_pose = next;
    ++m_revision;
    emit changed();
}

void CanvasViewState::setCenterX(qreal x)
{
    CanvasViewPose p = m_pose;
    p.center.setX(x);
    setPose(p);
}

void CanvasViewState::setCenterY(qreal y)
{
    CanvasViewPose p = m_pose;
    p.center.setY(y);
    setPose(p);
}

void CanvasViewState::setCenter(qreal x, qreal y)
{
    CanvasViewPose p = m_pose;
    p.center = QPointF(x, y);
    setPose(p);
}

void CanvasViewState::setZoom(qreal zoom)
{
    CanvasViewPose p = m_pose;
    p.zoom = zoom;
    setPose(p);
}

void CanvasViewState::setRotation(qreal degrees)
{
    CanvasViewPose p = m_pose;
    p.rotation = degrees;
    setPose(p);
}

void CanvasViewState::setMinZoom(qreal zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0 || zoom == m_limits.minimum) {
        return;
    }
    m_limits.minimum = zoom;
    m_limits.maximum = std::max(m_limits.maximum, zoom);
    setPose(m_pose);
    emit changed();
}

void CanvasViewState::setMaxZoom(qreal zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0 || zoom == m_limits.maximum) {
        return;
    }
    m_limits.maximum = zoom;
    m_limits.minimum = std::min(m_limits.minimum, zoom);
    setPose(m_pose);
    emit changed();
}

} // namespace QindaTK
