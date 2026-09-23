// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_renderer.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QString csvField(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}
}

void ChartRenderer::setAnnotationModel(ChartAnnotationModel *model)
{
    if (m_annotationModel == model) return;
    if (m_annotationModel) disconnect(m_annotationModel, nullptr, this, nullptr);
    m_annotationModel = model;
    if (model) {
        connect(model, &ChartAnnotationModel::annotationsChanged, this, [this] {
            bool selectedExists = false;
            for (const auto &a : m_annotationModel->annotations())
                if (a.id == m_selectedAnnotationId) { selectedExists = true; break; }
            if (!selectedExists && !m_selectedAnnotationId.isEmpty()) {
                m_selectedAnnotationId.clear(); emit annotationSelectionChanged();
            }
            emit annotationsChanged(); update();
        });
        connect(model, &QObject::destroyed, this, [this] {
            m_selectedAnnotationId.clear(); emit annotationSelectionChanged();
            emit annotationsChanged(); update();
        });
    }
    m_selectedAnnotationId.clear(); emit annotationSelectionChanged();
    emit annotationsChanged(); update();
}

double ChartRenderer::annotationX(const ChartAnnotation &a) const
{
    if (!m_xAxis || a.kind == ChartAnnotation::HorizontalLine) return NAN;
    if (a.hasUtc) return m_xAxis->mapUtc(a.utcMs);
    if (a.hasCategory) return m_xAxis->map(m_xAxis->categoryIndex(a.category));
    return m_xAxis->map(a.x);
}
double ChartRenderer::annotationY(const ChartAnnotation &a) const
{ return !m_yAxis || a.kind == ChartAnnotation::VerticalLine ? NAN : m_yAxis->map(a.y); }

QString ChartRenderer::annotationError() const
{
    if (!m_annotationModel || m_annotationModel->count() == 0) return {};
    if (!m_xAxis || !m_yAxis || !m_xAxis->isValid() || !m_yAxis->isValid())
        return QStringLiteral("Annotations require valid chart axes");
    for (const auto &a : m_annotationModel->annotations()) {
        if (a.kind != ChartAnnotation::HorizontalLine) {
            const bool compatible = a.hasUtc ? m_xAxis->kind() == ChartAxis::DateTime
                : a.hasCategory ? m_xAxis->kind() == ChartAxis::Category
                : m_xAxis->kind() == ChartAxis::Numeric || m_xAxis->kind() == ChartAxis::Logarithmic;
            if (!compatible || a.xUnit != m_xAxis->unit() || !std::isfinite(annotationX(a)))
                return QStringLiteral("Annotation X coordinate or unit conflicts with the X axis");
        }
        if (a.kind != ChartAnnotation::VerticalLine) {
            if ((m_yAxis->kind() != ChartAxis::Numeric && m_yAxis->kind() != ChartAxis::Logarithmic)
                || a.yUnit != m_yAxis->unit() || !std::isfinite(annotationY(a)))
                return QStringLiteral("Annotation Y coordinate or unit conflicts with the Y axis");
        }
    }
    return {};
}

QVariantMap ChartRenderer::annotationAt(double x, double y) const
{
    if (!m_annotationModel || !annotationError().isEmpty()) return {};
    const QRectF r = plotRect();
    if (r.isEmpty() || !r.contains(QPointF(x, y))) return {};
    const double radius = metric("hitRadius");
    for (int i = m_annotationModel->count() - 1; i >= 0; --i) {
        const auto &a = m_annotationModel->annotations().at(i);
        const double px = r.left() + (annotationX(a) - m_xStart) / viewX() * r.width();
        const double py = r.bottom() - (annotationY(a) - m_yStart) / viewY() * r.height();
        const bool hit = a.kind == ChartAnnotation::VerticalLine ? std::abs(px - x) <= radius
            : a.kind == ChartAnnotation::HorizontalLine ? std::abs(py - y) <= radius
            : std::hypot(px - x, py - y) <= radius;
        if (hit) return m_annotationModel->get(i);
    }
    return {};
}
bool ChartRenderer::selectAnnotationAt(double x, double y)
{
    const QVariantMap hit = annotationAt(x, y);
    if (hit.isEmpty()) return false;
    m_selectedAnnotationId = hit.value(QStringLiteral("id")).toString();
    emit annotationSelectionChanged(); emit annotationSelected(m_selectedAnnotationId);
    update(); return true;
}

void ChartRenderer::drawAnnotations(QPainter *p, const QRectF &r) const
{
    if (!m_annotationModel) return;
    const QString error = annotationError();
    if (!error.isEmpty()) {
        p->setPen(m_axis); p->drawText(r, Qt::AlignCenter, error); return;
    }
    for (const auto &a : m_annotationModel->annotations()) {
        const double px = r.left() + (annotationX(a) - m_xStart) / viewX() * r.width();
        const double py = r.bottom() - (annotationY(a) - m_yStart) / viewY() * r.height();
        const bool selected = a.id == m_selectedAnnotationId;
        p->setPen(QPen(m_defaultSeriesColor,
            selected ? metric("selectionWidth") : metric("lineWidth"), Qt::DashLine));
        if (a.kind == ChartAnnotation::VerticalLine) p->drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
        else if (a.kind == ChartAnnotation::HorizontalLine) p->drawLine(QPointF(r.left(), py), QPointF(r.right(), py));
        else {
            p->setBrush(m_defaultSeriesColor);
            p->drawEllipse(QPointF(px, py), metric("pointRadius"), metric("pointRadius"));
        }
        if (!a.label.isEmpty()) {
            const QPointF at(a.kind == ChartAnnotation::HorizontalLine ? r.left() : px,
                             a.kind == ChartAnnotation::VerticalLine ? r.top() : py);
            p->setPen(m_axis);
            p->drawText(at + QPointF(metric("smallGap"), -metric("smallGap")), a.label);
        }
    }
}

QString ChartRenderer::annotationsCsv() const
{
    QString result = QStringLiteral("id,label,kind,x,utcMs,category,y,xUnit,yUnit\r\n");
    if (!m_annotationModel) return result;
    for (int i = 0; i < m_annotationModel->count(); ++i) {
        const QVariantMap row = m_annotationModel->get(i);
        QStringList fields;
        for (const QString &key : {QStringLiteral("id"), QStringLiteral("label"),
             QStringLiteral("kind"), QStringLiteral("x"), QStringLiteral("utcMs"),
             QStringLiteral("category"), QStringLiteral("y"), QStringLiteral("xUnit"),
             QStringLiteral("yUnit")}) fields.append(csvField(row.value(key).toString()));
        result += fields.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return result;
}

int ChartRenderer::histogramBinAt(const QPointF &position) const
{
    if (!m_histogram.valid() || !m_xAxis || !m_yAxis) return -1;
    const QRectF r = plotRect();
    if (r.isEmpty() || !r.contains(position)) return -1;
    const double f = m_xStart + (position.x() - r.left()) / r.width() * viewX();
    const double value = m_xAxis->unmap(f);
    if (!std::isfinite(value)) return -1;
    for (int i = 0; i < m_histogram.bins.size(); ++i) {
        const auto &bin = m_histogram.bins.at(i);
        if (value < bin.lower || (i + 1 == m_histogram.bins.size() ? value > bin.upper : value >= bin.upper))
            continue;
        const double top = r.bottom() - (m_yAxis->map(bin.displayed) - m_yStart) / viewY() * r.height();
        const double bottom = r.bottom() - (m_yAxis->map(0) - m_yStart) / viewY() * r.height();
        if (position.y() >= std::min(top, bottom) - metric("hitRadius")
            && position.y() <= std::max(top, bottom) + metric("hitRadius")) return i;
    }
    return -1;
}
bool ChartRenderer::selectHistogramBins(int first, int last)
{
    if (!m_histogram.valid() || m_histogram.bins.isEmpty()) return false;
    if (first < 0 || last < 0 || first >= m_histogram.bins.size()
        || last >= m_histogram.bins.size()) return false;
    if (first > last) std::swap(first, last);
    const auto &lower = m_histogram.bins.at(first);
    const auto &upper = m_histogram.bins.at(last);
    m_histogramSelection.assign(first, last, lower.lower, upper.upper,
        last + 1 == m_histogram.bins.size(), m_histogram.unit,
        m_histogram.channel, m_histogram.normalization);
    emit histogramRangeSelected(&m_histogramSelection); update(); return true;
}

} // namespace QindaTK
