// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_overlay.h"
#include "canvas_surface.h"

#include <QPainter>
#include <QPen>

namespace QindaTK {

CanvasOverlay::CanvasOverlay(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::NoButton);
    connect(this, &CanvasOverlay::styleChanged, this, [this] { update(); });
}

void CanvasOverlay::setSurface(CanvasSurface *surface)
{
    if (m_surface == surface) {
        return;
    }
    m_surface = surface;
    rewire();
    emit surfaceChanged();
    update();
}

void CanvasOverlay::rewire()
{
    for (const auto &c : std::as_const(m_connections)) {
        disconnect(c);
    }
    m_connections.clear();
    if (!m_surface) {
        return;
    }
    auto repaint = [this] { update(); };
    m_connections << connect(m_surface, &CanvasSurface::viewChanged, this, repaint)
                  << connect(m_surface, &CanvasSurface::hoverChanged, this, repaint)
                  << connect(m_surface, &CanvasSurface::frameRendered, this, repaint)
                  << connect(m_surface, &CanvasSurface::toolControllerChanged, this, [this] {
                         rewire();
                         update();
                     })
                  << connect(m_surface, &CanvasSurface::documentChanged, this, [this] {
                         rewire();
                         update();
                     });
    if (ToolController *tools = m_surface->toolController()) {
        m_connections << connect(tools, &ToolController::previewChanged, this, repaint)
                      << connect(tools, &ToolController::selectionChanged, this, repaint);
    }
    if (CanvasDocumentAdapter *doc = m_surface->document()) {
        m_connections << connect(doc, &CanvasDocumentAdapter::edited, this, repaint);
    }
}

namespace {

void drawOutline(QPainter *painter, const CanvasObject &o, const QTransform &toView)
{
    const QPolygonF outline = toView.map(CanvasGeometry::documentOutline(o));
    if (CanvasGeometry::isClosed(o)) {
        painter->drawPolygon(outline);
    } else {
        painter->drawPolyline(outline);
    }
}

} // namespace

void CanvasOverlay::paint(QPainter *painter)
{
    if (!m_surface) {
        return;
    }
    CanvasDocumentAdapter *doc = m_surface->document();
    ToolController *tools = m_surface->toolController();
    const QTransform toView = CanvasTransform::documentToView(m_surface->pose(), m_surface->viewSize());
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    const CanvasToolPreview preview = tools ? tools->preview() : CanvasToolPreview{};

    // Selection: outlines up to the limit, then only the union bounds, so an
    // enormous selection costs a bounded amount per repaint.
    if (doc && tools && !tools->selection().isEmpty()) {
        painter->setPen(QPen(m_selectionColor, m_lineWidth));
        QRectF unionBounds;
        int drawn = 0;
        for (CanvasObjectId id : tools->selection()) {
            const CanvasObject *o = doc->object(id);
            if (!o || !CanvasGeometry::isRenderable(*o)) {
                continue;
            }
            if (drawn < m_outlineLimit) {
                drawOutline(painter, *o, toView);
                ++drawn;
            } else {
                unionBounds |= CanvasGeometry::documentBounds(*o);
            }
        }
        if (!unionBounds.isNull()) {
            painter->drawPolygon(toView.map(QPolygonF(unionBounds)));
        }
    }
    if (doc && m_surface->hoverObject() != 0 && preview.kind == CanvasToolPreview::None) {
        if (const CanvasObject *o = doc->object(m_surface->hoverObject())) {
            painter->setPen(QPen(m_hoverColor, m_lineWidth * 2));
            drawOutline(painter, *o, toView);
        }
    }

    QPen previewPen(m_previewColor, m_lineWidth);
    switch (preview.kind) {
    case CanvasToolPreview::Marquee: {
        previewPen.setStyle(Qt::DashLine);
        painter->setPen(previewPen);
        painter->setBrush(m_previewFill);
        painter->drawPolygon(toView.map(QPolygonF(preview.rect)));
        break;
    }
    case CanvasToolPreview::Lasso: {
        previewPen.setStyle(Qt::DashLine);
        painter->setPen(previewPen);
        painter->setBrush(preview.points.size() >= 3 ? m_previewFill : QColor(Qt::transparent));
        const QPolygonF path = toView.map(preview.points);
        if (path.size() >= 3) painter->drawPolygon(path);
        else painter->drawPolyline(path);
        break;
    }
    case CanvasToolPreview::Rect:
    case CanvasToolPreview::Ellipse: {
        CanvasObject shape;
        shape.shape = preview.kind == CanvasToolPreview::Rect ? CanvasShape::Rect : CanvasShape::Ellipse;
        shape.rect = preview.rect;
        painter->setPen(previewPen);
        painter->setBrush(m_previewFill);
        painter->drawPolygon(toView.map(CanvasGeometry::documentOutline(shape)));
        break;
    }
    case CanvasToolPreview::Polyline: {
        const qreal width = tools ? std::max(m_lineWidth, tools->property("strokeWidth").toReal() * m_surface->zoom())
                                  : m_lineWidth;
        painter->setPen(QPen(m_previewColor, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(toView.map(preview.points));
        if (doc) {
            painter->setPen(QPen(m_hoverColor, m_lineWidth * 2));
            for (CanvasObjectId id : preview.ids) { // eraser targets
                if (const CanvasObject *o = doc->object(id)) {
                    drawOutline(painter, *o, toView);
                }
            }
        }
        break;
    }
    case CanvasToolPreview::Move: {
        if (!doc) {
            break;
        }
        previewPen.setStyle(Qt::DashLine);
        painter->setPen(previewPen);
        const QTransform moved = QTransform::fromTranslate(preview.offset.x(), preview.offset.y()) * toView;
        int drawn = 0;
        for (CanvasObjectId id : preview.ids) {
            const CanvasObject *o = doc->object(id);
            if (o && drawn++ < m_outlineLimit) {
                drawOutline(painter, *o, moved);
            }
        }
        break;
    }
    case CanvasToolPreview::None:
        break;
    }
}

} // namespace QindaTK
