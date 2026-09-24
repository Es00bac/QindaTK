// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_map_canvas.h"
#include "geographic_tile_source.h"
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QPointF lonLat(const QJsonValue &value)
{ const auto a = value.toArray(); return {a[0].toDouble(), a[1].toDouble()}; }
}
GeographicMapCanvas::GeographicMapCanvas(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemHasContents);
    setFocus(true);
    connect(this, &GeographicMapCanvas::styleChanged, this, [this] { update(); });
}
void GeographicMapCanvas::setModel(GeographicFeatureModel *value)
{
    if (m_model == value) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = value;
    if (m_model) {
        connect(m_model, &GeographicFeatureModel::changed, this, [this] { update(); });
        connect(m_model, &GeographicFeatureModel::modelReset, this, [this] {
            if (!m_model || m_model->featureById(m_selectedId).isEmpty()) selectFeature({});
            update();
        });
        connect(m_model, &QObject::destroyed, this, [this] { m_model = nullptr; update(); });
    }
    selectFeature({}); emit modelChanged(); update();
}
double GeographicMapCanvas::wrap(double x)
{ x = std::fmod(x, 1.0); return x < 0 ? x + 1.0 : x; }
double GeographicMapCanvas::scale() const { return 256.0 * std::pow(2.0, m_zoom); }
double GeographicMapCanvas::centerLongitude() const
{ return GeographicFeatureModel::geographic(m_centerX, m_centerY).x(); }
void GeographicMapCanvas::setCenterLongitude(double value)
{
    if (!std::isfinite(value)) return;
    const double next = wrap((value + 180.0) / 360.0);
    if (qFuzzyCompare(next, m_centerX)) return;
    m_centerX = next; emit viewportChanged(); update();
    refreshTiles();
}
double GeographicMapCanvas::centerLatitude() const
{ return GeographicFeatureModel::geographic(m_centerX, m_centerY).y(); }
void GeographicMapCanvas::setCenterLatitude(double value)
{
    if (!std::isfinite(value)) return;
    const double next = GeographicFeatureModel::mercator(0, value).y();
    if (qFuzzyCompare(next, m_centerY)) return;
    m_centerY = next; emit viewportChanged(); update();
    refreshTiles();
}
void GeographicMapCanvas::setZoomLevel(double value)
{
    if (!std::isfinite(value)) return;
    const double next = std::clamp(value, 0.0, 18.0);
    if (qFuzzyCompare(next, m_zoom)) return;
    m_zoom = next; emit viewportChanged(); update();
    refreshTiles();
}
QPointF GeographicMapCanvas::screen(double lon, double lat) const
{
    const auto p = GeographicFeatureModel::mercator(lon, lat);
    const double x = p.x() + std::round(m_centerX - p.x());
    return {(x - m_centerX) * scale() + width()/2, (p.y() - m_centerY) * scale() + height()/2};
}
QVariantMap GeographicMapCanvas::screenPosition(double lon, double lat) const
{
    if (!std::isfinite(lon) || !std::isfinite(lat) || lon < -180 || lon > 180 || lat < -90 || lat > 90)
        return {{"valid", false}};
    const auto p = screen(lon, lat);
    return {{"valid", true}, {"x", p.x()}, {"y", p.y()}};
}
QVariantMap GeographicMapCanvas::coordinateAt(double x, double y) const
{
    if (!std::isfinite(x) || !std::isfinite(y)) return {{"valid", false}};
    const auto p = GeographicFeatureModel::geographic(m_centerX + (x-width()/2)/scale(),
                                                       m_centerY + (y-height()/2)/scale());
    return {{"valid", true}, {"longitude", p.x()}, {"latitude", p.y()}, {"crs", "EPSG:4326"}};
}
QPainterPath GeographicMapCanvas::featurePath(const GeographicFeatureModel::Feature &feature, int worldCopy) const
{
    QPainterPath path;
    const auto geometry = feature.geometry.value("coordinates");
    const auto point = [this, worldCopy](const QJsonValue &value, double previousX, bool first) {
        const auto ll = lonLat(value);
        const auto p = GeographicFeatureModel::mercator(ll.x(), ll.y());
        const double reference = first ? m_centerX : previousX;
        const double x = p.x() + std::round(reference - p.x()) + worldCopy;
        return QPointF((x-m_centerX)*scale()+width()/2, (p.y()-m_centerY)*scale()+height()/2);
    };
    const auto line = [&](const QJsonArray &coords, bool close) {
        double prev = 0; bool first = true;
        for (const auto &v : coords) {
            const auto p = point(v, prev, first);
            prev = m_centerX + (p.x()-width()/2)/scale() - worldCopy;
            if (first) path.moveTo(p); else path.lineTo(p);
            first = false;
        }
        if (close) path.closeSubpath();
    };
    const auto type = feature.type;
    if (type == QLatin1String("Point")) {
        const auto p = point(geometry, 0, true);
        path.addEllipse(p, m_pointRadius, m_pointRadius);
    } else if (type == QLatin1String("MultiPoint")) {
        for (const auto &v : geometry.toArray()) path.addEllipse(point(v, 0, true), m_pointRadius, m_pointRadius);
    } else if (type == QLatin1String("LineString")) line(geometry.toArray(), false);
    else if (type == QLatin1String("MultiLineString")) {
        for (const auto &v : geometry.toArray()) line(v.toArray(), false);
    } else if (type == QLatin1String("Polygon")) {
        for (const auto &v : geometry.toArray()) line(v.toArray(), true);
        path.setFillRule(Qt::OddEvenFill);
    } else if (type == QLatin1String("MultiPolygon")) {
        for (const auto &polygon : geometry.toArray())
            for (const auto &ring : polygon.toArray()) line(ring.toArray(), true);
        path.setFillRule(Qt::OddEvenFill);
    }
    return path;
}
QString GeographicMapCanvas::featureIdAt(double x, double y) const
{
    if (!m_model) return {};
    const QPointF p(x,y);
    const auto &features = m_model->features();
    for (auto it = features.crbegin(); it != features.crend(); ++it) {
        for (int copy = -1; copy <= 1; ++copy) {
            const auto path = featurePath(*it, copy);
            if (path.contains(p)) return it->id;
            QPainterPathStroker stroker;
            stroker.setWidth(std::max(m_hitRadius * 2, m_lineWidth));
            if (stroker.createStroke(path).contains(p)) return it->id;
        }
    }
    return {};
}
bool GeographicMapCanvas::selectFeature(const QString &id)
{
    if (!id.isEmpty() && (!m_model || m_model->featureById(id).isEmpty())) return false;
    if (m_selectedId == id) return true;
    m_selectedId = id; emit selectionChanged(); emit featureSelected(id); update(); return true;
}
bool GeographicMapCanvas::selectAt(double x, double y)
{ const auto id = featureIdAt(x,y); selectFeature(id); return !id.isEmpty(); }
void GeographicMapCanvas::panPixels(double dx, double dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    m_centerX = wrap(m_centerX - dx/scale());
    m_centerY = std::clamp(m_centerY - dy/scale(), 0.0, 1.0);
    emit viewportChanged(); update();
    refreshTiles();
}
void GeographicMapCanvas::zoomAt(double factor, double x, double y)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    const double old = scale();
    const double next = std::clamp(m_zoom + std::log2(factor), 0.0, 18.0);
    if (qFuzzyCompare(next, m_zoom)) return;
    const double worldX = m_centerX + (x-width()/2)/old;
    const double worldY = m_centerY + (y-height()/2)/old;
    m_zoom = next;
    m_centerX = wrap(worldX - (x-width()/2)/scale());
    m_centerY = std::clamp(worldY - (y-height()/2)/scale(), 0.0, 1.0);
    emit viewportChanged(); update();
    refreshTiles();
}
void GeographicMapCanvas::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), m_background);
    paintTiles(painter);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(boundingRect());
    painter->setPen(QPen(m_grid, m_gridWidth));
    for (int lon = -180; lon <= 180; lon += 30) {
        const auto p = screen(lon, 0); // local graticule; longitude wraps with camera
        if (p.x() >= 0 && p.x() <= width()) painter->drawLine(QPointF(p.x(),0), QPointF(p.x(),height()));
    }
    for (int lat = -60; lat <= 60; lat += 30) {
        const auto p = screen(0, lat);
        if (p.y() >= 0 && p.y() <= height()) painter->drawLine(QPointF(0,p.y()), QPointF(width(),p.y()));
    }
    if (!m_model) return;
    for (const auto &f : m_model->features()) {
        const bool selected = f.id == m_selectedId;
        const QColor color = selected ? m_selection : m_feature;
        painter->setPen(QPen(color, selected ? m_lineWidth * 2 : m_lineWidth));
        QColor fill = color;
        fill.setAlphaF(std::clamp(selected ? m_selectedFillOpacity : m_fillOpacity, 0.0, 1.0));
        painter->setBrush((f.type.contains("Polygon") || f.type.contains("Point")) ? QBrush(fill) : Qt::NoBrush);
        for (int copy = -1; copy <= 1; ++copy) {
            const auto path = featurePath(f, copy);
            if (path.boundingRect().intersects(boundingRect())) painter->drawPath(path);
        }
    }
}
void GeographicMapCanvas::mousePressEvent(QMouseEvent *event)
{ m_press = m_last = event->position(); m_dragged = false; forceActiveFocus(); event->accept(); }
void GeographicMapCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) return;
    const auto p = event->position();
    if (QLineF(m_press,p).length() > 3) m_dragged = true;
    if (m_dragged) panPixels(p.x()-m_last.x(), p.y()-m_last.y());
    m_last = p; event->accept();
}
void GeographicMapCanvas::mouseReleaseEvent(QMouseEvent *event)
{ if (!m_dragged) selectAt(event->position().x(), event->position().y()); event->accept(); }
void GeographicMapCanvas::wheelEvent(QWheelEvent *event)
{ zoomAt(std::pow(2.0, event->angleDelta().y()/480.0), event->position().x(), event->position().y()); event->accept(); }
void GeographicMapCanvas::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left: panPixels(width()/8,0); break;
    case Qt::Key_Right: panPixels(-width()/8,0); break;
    case Qt::Key_Up: panPixels(0,height()/8); break;
    case Qt::Key_Down: panPixels(0,-height()/8); break;
    case Qt::Key_Plus: case Qt::Key_Equal: zoomAt(2,width()/2,height()/2); break;
    case Qt::Key_Minus: zoomAt(0.5,width()/2,height()/2); break;
    default: QQuickPaintedItem::keyPressEvent(event); return;
    }
    event->accept();
}
}
