// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_surface.h"

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {


// AGENT-CONTRACT (docs/canvas.md#overlays): view-space feedback drawn above
// a CanvasSurface: hover outline, selection outlines and the active tool's
// transient preview. It reads the surface transform, the ToolController and
// the document; it never changes any of them. WP-041 replaces the selection
// part with SelectionOverlay/TransformHandles against the same contracts.
class CanvasOverlay : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasSurface *surface READ surface WRITE setSurface NOTIFY surfaceChanged)
    Q_PROPERTY(QColor selectionColor MEMBER m_selectionColor NOTIFY styleChanged)
    Q_PROPERTY(QColor hoverColor MEMBER m_hoverColor NOTIFY styleChanged)
    Q_PROPERTY(QColor previewColor MEMBER m_previewColor NOTIFY styleChanged)
    Q_PROPERTY(QColor previewFill MEMBER m_previewFill NOTIFY styleChanged)
    Q_PROPERTY(qreal lineWidth MEMBER m_lineWidth NOTIFY styleChanged)
    Q_PROPERTY(int outlineLimit MEMBER m_outlineLimit NOTIFY styleChanged)
public:
    explicit CanvasOverlay(QQuickItem *parent = nullptr);
    CanvasSurface *surface() const { return m_surface; }
    void setSurface(CanvasSurface *surface);
    void paint(QPainter *painter) override;

signals:
    void surfaceChanged();
    void styleChanged();

private:
    void rewire();
    QPointer<CanvasSurface> m_surface;
    QList<QMetaObject::Connection> m_connections;
    QColor m_selectionColor{Qt::cyan};
    QColor m_hoverColor{Qt::cyan};
    QColor m_previewColor{Qt::cyan};
    QColor m_previewFill{Qt::transparent};
    qreal m_lineWidth = 1.0;
    int m_outlineLimit = 512;
};

} // namespace QindaTK
