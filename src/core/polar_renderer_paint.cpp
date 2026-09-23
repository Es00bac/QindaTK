// SPDX-License-Identifier: LGPL-3.0-or-later
#include "polar_renderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace QindaTK {

QColor PolarRenderer::seriesColor(int index) const
{
    if (index >= 0 && index < m_seriesColors.size()) {
        const QColor color = m_seriesColors.at(index).value<QColor>();
        if (color.isValid()) return color;
    }
    return m_defaultSeriesColor;
}

void PolarRenderer::draw(QPainter *painter, const QSizeF &size) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillRect(QRectF(QPointF(0, 0), size), m_background);
    if (!valid()) {
        painter->setPen(m_text);
        painter->drawText(QRectF(QPointF(0, 0), size), Qt::AlignCenter, error());
        painter->restore(); return;
    }
    const QRectF square = plotSquare(size);
    if (square.isEmpty()) { painter->restore(); return; }
    const QPointF center = square.center();
    const double extent = square.width() / 2;
    const double gridWidth = metric("gridWidth", 0);
    const double lineWidth = metric("lineWidth", 0);
    const double pointRadius = metric("pointRadius", 0);
    const double labelGap = metric("labelGap", 0);
    const double fontSize = metric("fontSize", 0);
    if (fontSize > 0) {
        QFont font = painter->font();
        font.setPixelSize(int(std::round(fontSize)));
        painter->setFont(font);
    }
    painter->setPen(QPen(m_grid, gridWidth));
    painter->setBrush(Qt::NoBrush);
    for (int ring = 1; ring <= 4; ++ring) {
        const double radius = extent * ring / 4;
        painter->drawEllipse(center, radius, radius);
    }
    const int spokeCount = m_angleMode == Category ? m_categories.size() : 4;
    for (int i = 0; i < spokeCount; ++i) {
        const double angle = double(i) * 360.0 / spokeCount;
        const QPointF outer = PolarTransform::project(angle, m_radialMaximum, square,
            m_radialMinimum, m_radialMaximum, false, m_startAngleDegrees, m_clockwise);
        painter->drawLine(center, outer);
        if (fontSize > 0) {
            const QPointF offset = outer - center;
            const double length = std::hypot(offset.x(), offset.y());
            const QPointF label = outer + offset / std::max(1.0, length) * labelGap;
            const QString text = m_angleMode == Category ? m_categories.at(i)
                : (m_angleUnit == Radians
                    ? QString::number(angle * std::numbers::pi / 180.0, 'g', 3) + QStringLiteral(" rad")
                    : QString::number(angle, 'g', 3) + QStringLiteral("°"));
            painter->setPen(m_text);
            painter->drawText(QRectF(label.x() - metric("labelWidth", 0) / 2,
                                     label.y() - fontSize,
                                     metric("labelWidth", 0), fontSize * 2),
                              Qt::AlignCenter, text);
            painter->setPen(QPen(m_grid, gridWidth));
        }
    }
    if (fontSize > 0) {
        painter->setPen(m_text);
        for (int ring = 1; ring <= 4; ++ring) {
            const double value = m_radialMinimum
                + (m_radialMaximum - m_radialMinimum) * ring / 4;
            const QString text = QString::number(value, 'g', 3)
                + (m_radialUnit.isEmpty() ? QString{} : QStringLiteral(" ") + m_radialUnit);
            const double diagonal = extent * ring / 4 / std::numbers::sqrt2;
            painter->drawText(QPointF(center.x() + diagonal + labelGap,
                center.y() - diagonal - labelGap), text);
        }
    }
    if (!m_model) { painter->restore(); return; }
    const auto &points = m_model->points();
    QHash<QString, QVector<int>> groups;
    QStringList seriesOrder;
    for (int i = 0; i < points.size(); ++i) {
        const QString &id = points.at(i).seriesId;
        if (!groups.contains(id)) seriesOrder.append(id);
        groups[id].append(i);
    }
    for (int seriesIndex = 0; seriesIndex < seriesOrder.size(); ++seriesIndex) {
        QVector<int> indices = groups.value(seriesOrder.at(seriesIndex));
        std::sort(indices.begin(), indices.end(), [this, &points](int a, int b) {
            return PolarTransform::normalizeDegrees(pointAngleDegrees(points.at(a)))
                 < PolarTransform::normalizeDegrees(pointAngleDegrees(points.at(b)));
        });
        const PolarPoint::Kind kind = points.at(indices.first()).kind;
        const QColor color = seriesColor(seriesIndex);
        painter->setPen(QPen(color, lineWidth));
        painter->setBrush(Qt::NoBrush);
        if (kind == PolarPoint::Polyline || kind == PolarPoint::Polygon) {
            QPainterPath path;
            for (int i = 0; i < indices.size(); ++i) {
                const QPointF position = pointPosition(points.at(indices.at(i)), square);
                if (i == 0) path.moveTo(position); else path.lineTo(position);
            }
            if (kind == PolarPoint::Polygon) {
                path.closeSubpath();
                QColor fill = color;
                fill.setAlphaF(m_fillOpacity);
                painter->fillPath(path, fill);
            }
            painter->drawPath(path);
        }
        if (kind == PolarPoint::RadialBar) {
            for (const int i : indices) {
                const PolarPoint &point = points.at(i);
                const QPointF outer = pointPosition(point, square);
                const double barRadius = std::hypot(outer.x() - center.x(),
                                                    outer.y() - center.y());
                const double bearing = PolarTransform::normalizeDegrees(
                    m_startAngleDegrees + (m_clockwise ? 1.0 : -1.0)
                        * pointAngleDegrees(point));
                const double halfSweep = 360.0 / m_categories.size() * 0.375;
                const double qtStart = 90.0 - bearing + (m_clockwise ? halfSweep : -halfSweep);
                const double qtSweep = (m_clockwise ? -1.0 : 1.0) * 2 * halfSweep;
                QPainterPath wedge;
                wedge.moveTo(center);
                wedge.arcTo(QRectF(center.x() - barRadius, center.y() - barRadius,
                                   barRadius * 2, barRadius * 2), qtStart, qtSweep);
                wedge.closeSubpath();
                QColor fill = color; fill.setAlphaF(m_fillOpacity);
                painter->fillPath(wedge, fill);
                painter->drawPath(wedge);
            }
        } else {
            painter->setBrush(color);
            for (const int i : indices)
                painter->drawEllipse(pointPosition(points.at(i), square), pointRadius, pointRadius);
        }
    }
    if (!m_selectedPointId.isEmpty()) {
        for (const PolarPoint &point : points) {
            if (point.seriesId == m_selectedSeriesId && point.pointId == m_selectedPointId) {
                painter->setPen(QPen(m_text, metric("selectionWidth", 0)));
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(pointPosition(point, square),
                                     pointRadius + metric("selectionGap", 0),
                                     pointRadius + metric("selectionGap", 0));
                break;
            }
        }
    }
    if (fontSize > 0) {
        double legendX = metric("legendGap", 0);
        double legendY = square.bottom() + labelGap + fontSize * 2;
        const double swatch = metric("legendSwatch", 0);
        const double legendGap = metric("legendGap", 0);
        for (int seriesIndex = 0; seriesIndex < seriesOrder.size(); ++seriesIndex) {
            const QVector<int> indices = groups.value(seriesOrder.at(seriesIndex));
            const QString name = points.at(indices.first()).seriesName;
            const double needed = swatch + labelGap
                + painter->fontMetrics().horizontalAdvance(name) + legendGap;
            if (legendX + needed > size.width() && legendX > legendGap) {
                legendX = legendGap;
                legendY += fontSize * 2;
            }
            painter->setPen(QPen(seriesColor(seriesIndex), lineWidth));
            painter->drawLine(QPointF(legendX, legendY - fontSize / 3),
                              QPointF(legendX + swatch, legendY - fontSize / 3));
            painter->setPen(m_text);
            painter->drawText(QPointF(legendX + swatch + labelGap, legendY), name);
            legendX += needed;
        }
    }
    painter->restore();
}

void PolarRenderer::paint(QPainter *painter)
{ draw(painter, boundingRect().size()); }

} // namespace QindaTK
