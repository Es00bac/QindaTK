// SPDX-License-Identifier: LGPL-3.0-or-later
#include "polar_renderer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QSvgGenerator>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace QindaTK {
namespace {
QString csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}
QString kindName(PolarPoint::Kind kind)
{
    switch (kind) {
    case PolarPoint::Points: return QStringLiteral("points");
    case PolarPoint::Polyline: return QStringLiteral("polyline");
    case PolarPoint::Polygon: return QStringLiteral("polygon");
    case PolarPoint::RadialBar: return QStringLiteral("radial-bar");
    }
    return {};
}
}

PolarRenderer::PolarRenderer(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemHasContents, true);
    setAntialiasing(true);
    setActiveFocusOnTab(true);
}
void PolarRenderer::refresh()
{ emit configurationChanged(); update(); }
void PolarRenderer::setModel(PolarDataModel *value)
{
    if (m_model == value) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = value;
    if (value) {
        connect(value, &PolarDataModel::rowsChanged, this, &PolarRenderer::refresh);
        connect(value, &QObject::destroyed, this, &PolarRenderer::refresh);
    }
    m_selectedSeriesId.clear(); m_selectedPointId.clear();
    emit selectionChanged(); refresh();
}
void PolarRenderer::setAngleMode(AngleMode value)
{ if (m_angleMode != value) { m_angleMode = value; refresh(); } }
void PolarRenderer::setAngleUnit(AngleUnit value)
{ if (m_angleUnit != value) { m_angleUnit = value; refresh(); } }
void PolarRenderer::setCategories(const QStringList &value)
{ if (m_categories != value) { m_categories = value; refresh(); } }
void PolarRenderer::setRadialMinimum(double value)
{ if (m_radialMinimum != value) { m_radialMinimum = value; refresh(); } }
void PolarRenderer::setRadialMaximum(double value)
{ if (m_radialMaximum != value) { m_radialMaximum = value; refresh(); } }
void PolarRenderer::setRadialUnit(const QString &value)
{ if (m_radialUnit != value) { m_radialUnit = value; refresh(); } }
void PolarRenderer::setStartAngleDegrees(double value)
{ if (m_startAngleDegrees != value) { m_startAngleDegrees = value; refresh(); } }
void PolarRenderer::setClockwise(bool value)
{ if (m_clockwise != value) { m_clockwise = value; refresh(); } }
void PolarRenderer::setBackgroundColor(const QColor &value)
{ if (m_background != value) { m_background = value; emit styleChanged(); update(); } }
void PolarRenderer::setGridColor(const QColor &value)
{ if (m_grid != value) { m_grid = value; emit styleChanged(); update(); } }
void PolarRenderer::setTextColor(const QColor &value)
{ if (m_text != value) { m_text = value; emit styleChanged(); update(); } }
void PolarRenderer::setDefaultSeriesColor(const QColor &value)
{ if (m_defaultSeriesColor != value) { m_defaultSeriesColor = value; emit styleChanged(); update(); } }
void PolarRenderer::setSeriesColors(const QVariantList &value)
{ if (m_seriesColors != value) { m_seriesColors = value; emit styleChanged(); update(); } }
void PolarRenderer::setMetrics(const QVariantMap &value)
{ if (m_metrics != value) { m_metrics = value; emit styleChanged(); update(); } }
void PolarRenderer::setFillOpacity(double value)
{ if (std::isfinite(value) && value >= 0 && value <= 1 && m_fillOpacity != value)
    { m_fillOpacity = value; emit styleChanged(); update(); } }

QString PolarRenderer::error() const
{
    if (!std::isfinite(m_radialMinimum) || !std::isfinite(m_radialMaximum)
        || m_radialMinimum < 0 || m_radialMaximum <= m_radialMinimum
        || m_radialMaximum > 1e12)
        return QStringLiteral("Radial domain must be finite, increasing and nonnegative");
    if (!std::isfinite(m_startAngleDegrees) || std::abs(m_startAngleDegrees) > 1e9)
        return QStringLiteral("Start angle must be finite and bounded");
    if (m_angleMode == Category) {
        if (m_categories.isEmpty() || m_categories.size() > 64)
            return QStringLiteral("Category angle axis requires 1–64 ordered categories");
        QSet<QString> distinct;
        for (const QString &category : m_categories) {
            if (category.isEmpty() || category.size() > 128 || distinct.contains(category))
                return QStringLiteral("Angular categories must be unique, nonempty and bounded");
            distinct.insert(category);
        }
    }
    if (!m_model) return {};
    QHash<QString, QSet<QString>> polygonCategories;
    QHash<QString, int> polygonCounts;
    for (const PolarPoint &point : m_model->points()) {
        if (point.hasCategory != (m_angleMode == Category))
            return QStringLiteral("Point angular coordinate does not match the angle mode");
        if (point.hasCategory && !m_categories.contains(point.category))
            return QStringLiteral("Point category is absent from the declared angle order");
        if (point.kind == PolarPoint::RadialBar && m_angleMode != Category)
            return QStringLiteral("Radial bars require an ordered category angle axis");
        if (point.radius < m_radialMinimum || point.radius > m_radialMaximum)
            return QStringLiteral("Point radius lies outside the declared radial domain");
        if (point.kind == PolarPoint::Polygon) {
            ++polygonCounts[point.seriesId];
            if (point.hasCategory) polygonCategories[point.seriesId].insert(point.category);
        }
    }
    for (auto it = polygonCounts.cbegin(); it != polygonCounts.cend(); ++it) {
        if (it.value() < 3 || (m_angleMode == Category
            && polygonCategories.value(it.key()).size() != m_categories.size()))
            return QStringLiteral("A polygon needs at least three points and every declared category");
    }
    return {};
}

double PolarRenderer::metric(const char *name, double fallback) const
{ return m_metrics.value(QLatin1String(name), fallback).toDouble(); }
QRectF PolarRenderer::plotSquare(const QSizeF &size) const
{
    const double margin = std::max(0.0, metric("margin", 0));
    const double diameter = std::max(0.0, std::min(size.width(), size.height()) - 2 * margin);
    return {(size.width() - diameter) / 2, (size.height() - diameter) / 2,
            diameter, diameter};
}
double PolarRenderer::pointAngleDegrees(const PolarPoint &point) const
{
    if (point.hasCategory) return double(m_categories.indexOf(point.category)) * 360.0 / m_categories.size();
    return m_angleUnit == Radians ? point.angle * 180.0 / std::numbers::pi : point.angle;
}
QPointF PolarRenderer::pointPosition(const PolarPoint &point, const QRectF &square) const
{
    return PolarTransform::project(pointAngleDegrees(point), point.radius,
        square, m_radialMinimum, m_radialMaximum, false, m_startAngleDegrees, m_clockwise);
}
QPointF PolarRenderer::project(double angle, double radius) const
{
    if (!valid() || m_angleMode != Numeric) return {};
    return PolarTransform::project(angle, radius, plotSquare(QSizeF(width(), height())),
        m_radialMinimum, m_radialMaximum, m_angleUnit == Radians,
        m_startAngleDegrees, m_clockwise);
}
QPointF PolarRenderer::projectCategory(const QString &category, double radius) const
{
    if (!valid() || m_angleMode != Category || !m_categories.contains(category)) return {};
    PolarPoint point; point.category = category; point.hasCategory = true; point.radius = radius;
    return pointPosition(point, plotSquare(QSizeF(width(), height())));
}
QVariantMap PolarRenderer::unproject(double x, double y) const
{
    if (!valid()) return {};
    const PolarCoordinates p = PolarTransform::unproject({x, y},
        plotSquare(QSizeF(width(), height())), m_radialMinimum, m_radialMaximum,
        m_angleUnit == Radians && m_angleMode == Numeric,
        m_startAngleDegrees, m_clockwise);
    if (!std::isfinite(p.angle) || !std::isfinite(p.radius)) return {};
    QVariantMap out{{QStringLiteral("angle"), p.angle},
                    {QStringLiteral("radius"), p.radius}};
    if (m_angleMode == Category) {
        const int n = m_categories.size();
        const int nearest = int(std::round(p.angle / 360.0 * n)) % n;
        out.insert(QStringLiteral("category"), m_categories.at(nearest));
    }
    return out;
}

QVariantMap PolarRenderer::hitTest(double x, double y) const
{
    if (!valid() || !m_model) return {};
    const QRectF square = plotSquare(QSizeF(width(), height()));
    const PolarCoordinates cursor = PolarTransform::unproject({x, y}, square,
        m_radialMinimum, m_radialMaximum, false, m_startAngleDegrees, m_clockwise);
    if (!std::isfinite(cursor.radius) || cursor.radius < m_radialMinimum
        || cursor.radius > m_radialMaximum) return {};
    const double hitRadius = std::max(0.0, metric("hitRadius", 0));
    double best = hitRadius * hitRadius;
    int bestIndex = -1;
    const auto &points = m_model->points();
    for (int i = 0; i < points.size(); ++i) {
        const PolarPoint &point = points.at(i);
        if (point.kind == PolarPoint::RadialBar && std::isfinite(cursor.angle)) {
            const double halfSweep = 360.0 / m_categories.size() * 0.375;
            if (cursor.radius >= m_radialMinimum && cursor.radius <= point.radius
                && PolarTransform::angularDistanceDegrees(cursor.angle,
                    pointAngleDegrees(point)) <= halfSweep) return m_model->get(i);
        }
        const QPointF projected = pointPosition(point, square);
        const double dx = projected.x() - x, dy = projected.y() - y;
        const double distance = dx * dx + dy * dy;
        if (distance <= best) { best = distance; bestIndex = i; }
    }
    return bestIndex < 0 ? QVariantMap{} : m_model->get(bestIndex);
}

bool PolarRenderer::selectAt(double x, double y)
{
    const QVariantMap point = hitTest(x, y);
    if (point.isEmpty()) return false;
    m_selectedSeriesId = point.value(QStringLiteral("seriesId")).toString();
    m_selectedPointId = point.value(QStringLiteral("pointId")).toString();
    emit selectionChanged(); emit pointSelected(m_selectedSeriesId, m_selectedPointId);
    update(); return true;
}
bool PolarRenderer::navigateSelection(int key)
{
    if (!m_model || m_model->count() == 0 || !valid()) return false;
    int current = -1;
    for (int i = 0; i < m_model->count(); ++i) {
        const PolarPoint &p = m_model->points().at(i);
        if (p.seriesId == m_selectedSeriesId && p.pointId == m_selectedPointId)
            { current = i; break; }
    }
    int next = current;
    switch (key) {
    case Qt::Key_Left: case Qt::Key_Up: next = std::max(0, current < 0 ? 0 : current - 1); break;
    case Qt::Key_Right: case Qt::Key_Down: next = std::min(m_model->count() - 1, current + 1); break;
    case Qt::Key_Home: next = 0; break;
    case Qt::Key_End: next = m_model->count() - 1; break;
    default: return false;
    }
    const PolarPoint &p = m_model->points().at(next);
    m_selectedSeriesId = p.seriesId; m_selectedPointId = p.pointId;
    emit selectionChanged(); emit pointSelected(p.seriesId, p.pointId); update();
    return true;
}
QVariantMap PolarRenderer::selectedPoint() const
{
    if (!m_model) return {};
    for (int i = 0; i < m_model->count(); ++i) {
        const PolarPoint &p = m_model->points().at(i);
        if (p.seriesId == m_selectedSeriesId && p.pointId == m_selectedPointId)
            return m_model->get(i);
    }
    return {};
}
QString PolarRenderer::csv() const
{
    QString out = QStringLiteral("seriesId,seriesName,pointId,kind,angle,angleUnit,category,categoryIndex,radius,radialUnit,radialMinimum,radialMaximum,startAngleDegrees,clockwise\r\n");
    if (!m_model) return out;
    for (const PolarPoint &p : m_model->points()) {
        const QStringList fields{
            p.seriesId, p.seriesName, p.pointId, kindName(p.kind),
            p.hasCategory ? QString{} : QString::number(p.angle, 'g', 17),
            p.hasCategory ? QString{} : (m_angleUnit == Radians ? QStringLiteral("rad") : QStringLiteral("deg")),
            p.category, p.hasCategory ? QString::number(m_categories.indexOf(p.category)) : QString{},
            QString::number(p.radius, 'g', 17), m_radialUnit,
            QString::number(m_radialMinimum, 'g', 17),
            QString::number(m_radialMaximum, 'g', 17),
            QString::number(m_startAngleDegrees, 'g', 17),
            m_clockwise ? QStringLiteral("true") : QStringLiteral("false")};
        QStringList escaped;
        for (const QString &field : fields) escaped.append(csvField(field));
        out += escaped.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}

QImage PolarRenderer::renderImage(const QSize &size) const
{
    const QSize bounded(qBound(1, size.width(), 8192), qBound(1, size.height(), 8192));
    QImage image(bounded, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image); draw(&painter, bounded); painter.end();
    emit const_cast<PolarRenderer *>(this)->exportImageReady(image);
    return image;
}
bool PolarRenderer::exportPng(const QString &path, const QSize &size) const
{
    if (path.isEmpty() || size.width() < 1 || size.height() < 1
        || size.width() > 8192 || size.height() > 8192) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image); draw(&painter, size); painter.end();
    return image.save(&file, "PNG") && file.commit();
}
bool PolarRenderer::exportSvg(const QString &path, const QSize &size) const
{
    if (path.isEmpty() || size.width() < 1 || size.height() < 1
        || size.width() > 8192 || size.height() > 8192) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QSvgGenerator generator;
    generator.setOutputDevice(&file);
    generator.setSize(size);
    generator.setViewBox(QRect(QPoint(0, 0), size));
    QPainter painter(&generator);
    if (!painter.isActive()) return false;
    draw(&painter, size); painter.end(); return file.commit();
}
void PolarRenderer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && selectAt(event->position().x(), event->position().y())) {
        forceActiveFocus(); event->accept(); return;
    }
    event->ignore();
}
void PolarRenderer::keyPressEvent(QKeyEvent *event)
{ if (navigateSelection(event->key())) event->accept(); else event->ignore(); }

} // namespace QindaTK
