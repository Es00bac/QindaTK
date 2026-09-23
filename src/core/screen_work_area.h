// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QPointer>
#include <QQuickItem>
#include <QRect>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Logical global coordinates, as reported by the platform. Never multiply by DPR.
class ScreenWorkArea : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *anchor READ anchor WRITE setAnchor NOTIFY anchorChanged)
    Q_PROPERTY(QPointF anchorPoint READ anchorPoint WRITE setAnchorPoint NOTIFY anchorPointChanged)
    Q_PROPERTY(bool trackTransforms READ trackTransforms WRITE setTrackTransforms NOTIFY trackTransformsChanged)
    Q_PROPERTY(QRect availableGeometry READ availableGeometry NOTIFY availableGeometryChanged)
public:
    explicit ScreenWorkArea(QObject *parent = nullptr);
    QQuickItem *anchor() const { return m_anchor; }
    void setAnchor(QQuickItem *anchor);
    QPointF anchorPoint() const { return m_point; }
    void setAnchorPoint(QPointF point);
    bool trackTransforms() const { return m_trackTransforms; }
    void setTrackTransforms(bool track);
    QRect availableGeometry() const { return m_geometry; }
    Q_INVOKABLE void refresh();
signals:
    void anchorChanged();
    void anchorPointChanged();
    void trackTransformsChanged();
    void availableGeometryChanged();
private:
    void reconnect();
    QPointer<QQuickItem> m_anchor;
    QPointF m_point;
    bool m_trackTransforms = false;
    QRect m_geometry;
    QList<QMetaObject::Connection> m_connections;
};
}
