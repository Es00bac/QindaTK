// SPDX-License-Identifier: LGPL-3.0-or-later
// Painting and export for ScientificPlotRenderer. Screen, PNG and SVG share
// drawFigure(), so an exported figure is the figure on screen.
#include "scientific_plot_renderer.h"
#include "scientific_sampling.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSvgGenerator>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
bool finite(const QPointF &p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
constexpr int SurfaceRasterLimit = 1024;
QJsonObject axisJson(const ChartAxis *a)
{
    if (!a) return {};
    return {{QStringLiteral("kind"), a->kind() == ChartAxis::Logarithmic ? QStringLiteral("log") : QStringLiteral("linear")},
            {QStringLiteral("minimum"), a->minimum()}, {QStringLiteral("maximum"), a->maximum()},
            {QStringLiteral("unit"), a->unit()}, {QStringLiteral("reversed"), a->reversed()}};
}
}

QRectF ScientificPlotRenderer::plotRect(const QSizeF &size) const
{
    const double left = metric("leftMargin"), top = metric("topMargin"), bottom = metric("bottomMargin");
    double right = metric("rightMargin");
    if (m_surface && m_surface->hasValues())
        right += metric("colorBarWidth") + metric("tickLabelWidth") + metric("labelGap") * 2;
    return {left, top, std::max(0.0, size.width() - left - right), std::max(0.0, size.height() - top - bottom)};
}

QPointF ScientificPlotRenderer::point(double x, double y, const QRectF &plot) const
{
    // AGENT-NOTE: each coordinate is independent, so a sample with a
    // non-positive Y on a log axis still has a usable X (gap markers).
    const double nx = (m_xAxis->map(x) - m_xStart) / viewX();
    const double ny = (m_yAxis->map(y) - m_yStart) / viewY();
    return {plot.left() + nx * plot.width(), plot.bottom() - ny * plot.height()};
}

QColor ScientificPlotRenderer::seriesColor(int series) const
{
    return m_seriesColors.isEmpty() ? m_text : m_seriesColors.at(series % m_seriesColors.size());
}

QColor ScientificPlotRenderer::surfaceColor(double z) const
{
    if (m_surfaceColors.isEmpty()) return m_grid;
    if (m_surfaceColors.size() == 1) return m_surfaceColors.first();
    const double lo = m_surface->zMinimum(), hi = m_surface->zMaximum();
    const double t = hi > lo ? std::clamp((z - lo) / (hi - lo), 0.0, 1.0) : 0.5;
    const double position = t * (m_surfaceColors.size() - 1);
    const int i = std::min(int(position), int(m_surfaceColors.size()) - 2);
    const double f = position - i;
    const QColor a = m_surfaceColors[i], b = m_surfaceColors[i + 1];
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f), float(a.greenF() + (b.greenF() - a.greenF()) * f),
                            float(a.blueF() + (b.blueF() - a.blueF()) * f), float(a.alphaF() + (b.alphaF() - a.alphaF()) * f));
}

void ScientificPlotRenderer::drawSurface(QPainter *p, const QRectF &plot) const
{
    if (!m_surface || !m_surface->hasValues()) return;
    // AGENT-GUARD: sampling is bounded by the raster, never by lattice size;
    // each raster pixel samples the lattice at its centre in data space.
    const int w = std::clamp(int(std::ceil(plot.width())), 1, SurfaceRasterLimit);
    const int h = std::clamp(int(std::ceil(plot.height())), 1, SurfaceRasterLimit);
    QVector<double> xs(w);
    for (int i = 0; i < w; ++i) xs[i] = m_xAxis->unmap(m_xStart + viewX() * (i + 0.5) / w);
    QImage raster(w, h, QImage::Format_ARGB32);
    for (int j = 0; j < h; ++j) {
        const double y = m_yAxis->unmap(m_yStart + viewY() * (1.0 - (j + 0.5) / h));
        auto *line = reinterpret_cast<QRgb *>(raster.scanLine(j));
        for (int i = 0; i < w; ++i) {
            const bool inside = xs[i] >= m_surface->xs().first() && xs[i] <= m_surface->xs().last()
                && y >= m_surface->ys().first() && y <= m_surface->ys().last();
            const double z = m_surface->sampleAt(xs[i], y, m_interpolation);
            line[i] = std::isnan(z) ? (inside ? m_missing.rgba() : qRgba(0, 0, 0, 0)) : surfaceColor(z).rgba();
        }
    }
    p->save();
    p->setClipRect(plot);
    p->setRenderHint(QPainter::SmoothPixmapTransform, false);
    p->drawImage(plot, raster);
    p->restore();
    p->save();
    // Colour bar with calibrated end labels and the Z unit.
    const double gap = metric("labelGap"), barWidth = metric("colorBarWidth");
    const QRectF bar(plot.right() + gap * 2, plot.top(), barWidth, plot.height());
    for (int j = 0; j < int(bar.height()); ++j) {
        const double t = 1.0 - (j + 0.5) / bar.height();
        p->setPen(surfaceColor(m_surface->zMinimum() + t * (m_surface->zMaximum() - m_surface->zMinimum())));
        p->drawLine(QPointF(bar.left(), bar.top() + j + 0.5), QPointF(bar.right(), bar.top() + j + 0.5));
    }
    p->setPen(m_grid); p->setBrush(Qt::NoBrush); p->drawRect(bar);
    const QRectF labels(bar.right() + gap, bar.top(), metric("tickLabelWidth"), bar.height());
    p->setPen(m_text);
    p->drawText(labels, Qt::AlignLeft | Qt::AlignTop, ScientificSampling::tickLabel(m_surface->zMaximum()));
    p->drawText(labels, Qt::AlignLeft | Qt::AlignBottom, ScientificSampling::tickLabel(m_surface->zMinimum()));
    p->drawText(labels, Qt::AlignLeft | Qt::AlignVCenter, m_surface->zUnit());
    p->restore();
}

void ScientificPlotRenderer::drawAxes(QPainter *p, const QRectF &plot, const QSizeF &size) const
{
    const QFontMetricsF fm(p->font());
    const double gap = metric("labelGap"), tickWidth = metric("tickLabelWidth"), line = metric("lineWidth");
    p->setPen(QPen(m_grid, line));
    for (const QVariant &v : ticks(QStringLiteral("x"))) {
        const double x = point(v.toDouble(), 1, plot).x();
        if (!std::isfinite(x) || x < plot.left() - 0.5 || x > plot.right() + 0.5) continue;
        p->setPen(QPen(m_grid, line));
        p->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p->setPen(m_text);
        p->drawText(QRectF(x - tickWidth / 2, plot.bottom() + gap, tickWidth, fm.height()), Qt::AlignCenter,
                    ScientificSampling::tickLabel(v.toDouble()));
    }
    for (const QVariant &v : ticks(QStringLiteral("y"))) {
        const double y = point(1, v.toDouble(), plot).y();
        if (!std::isfinite(y) || y < plot.top() - 0.5 || y > plot.bottom() + 0.5) continue;
        p->setPen(QPen(m_grid, line));
        p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p->setPen(m_text);
        p->drawText(QRectF(plot.left() - gap - tickWidth, y - fm.height() / 2, tickWidth, fm.height()),
                    Qt::AlignRight | Qt::AlignVCenter, ScientificSampling::tickLabel(v.toDouble()));
    }
    p->setPen(m_text);
    const double xTitleTop = plot.bottom() + gap + fm.height();
    p->drawText(QRectF(plot.left(), xTitleTop, plot.width(), fm.height()), Qt::AlignCenter,
                axisTitle(m_xAxis, m_xLabel, "X"));
    p->save();
    p->translate(std::max(0.0, plot.left() - gap * 2 - tickWidth - fm.height()), plot.center().y());
    p->rotate(-90);
    p->drawText(QRectF(-plot.height() / 2, 0, plot.height(), fm.height()), Qt::AlignCenter,
                axisTitle(m_yAxis, m_yLabel, "Y"));
    p->restore();
    // Footer: uncertainty meaning, gaps and log-domain omissions.
    int missing = 0;
    for (const auto &d : m_series) if (d) missing += d->missingCount();
    QString footer = tr("Error bars: host supplied ±1σ · %1 missing").arg(missing);
    if (const int omitted = omittedCount()) footer += tr(" · %1 outside log domain").arg(omitted);
    const double footerTop = xTitleTop + fm.height();
    p->drawText(QRectF(plot.left(), footerTop, plot.width(), std::max(0.0, size.height() - footerTop)),
                Qt::AlignHCenter | Qt::AlignTop, footer);
}

void ScientificPlotRenderer::drawSeries(QPainter *p, const QRectF &plot, int s) const
{
    const ScientificDataset *d = m_series[s];
    const auto &samples = d->samples();
    const QVector<int> indices = painted(s);
    const QColor color = seriesColor(s);
    const double line = metric("lineWidth"), radius = metric("pointRadius"), cap = metric("capHalfWidth");
    const bool trace = d->kind() == ScientificDataset::Trace;
    const bool markers = indices.size() * radius * 4 <= plot.width();
    if (trace) {
        // Pass 1: bands (Band mode), broken at gaps and at samples without Y
        // uncertainty. Pass 2: the line, broken only at gaps, drawn on top.
        QColor band = color;
        band.setAlphaF(float(m_metrics.value(QStringLiteral("bandOpacity"), 0.25).toDouble()));
        QVector<QPointF> run, upper, lower;
        auto flushBand = [&] {
            if (upper.size() > 1) {
                QPainterPath area; area.moveTo(upper.first());
                for (int i = 1; i < upper.size(); ++i) area.lineTo(upper[i]);
                for (int i = int(lower.size()) - 1; i >= 0; --i) area.lineTo(lower[i]);
                area.closeSubpath(); p->fillPath(area, band);
            }
            upper.clear(); lower.clear();
        };
        auto flushLine = [&] {
            if (run.size() > 1) { p->setPen(QPen(color, line)); p->drawPolyline(run.constData(), int(run.size())); }
            run.clear();
        };
        if (m_mode == Band) {
            for (int i : indices) {
                const ScientificSample &v = samples[i];
                const QPointF hi = v.missingY ? QPointF() : point(v.x, v.y + v.sigmaYHigh, plot);
                const QPointF lo = v.missingY ? QPointF() : point(v.x, v.y - v.sigmaYLow, plot);
                if (v.missingY || !v.hasSigmaY || !finite(hi) || !finite(lo)) { flushBand(); continue; }
                upper.append(hi); lower.append(lo);
            }
            flushBand();
        }
        for (int i : indices) {
            const ScientificSample &v = samples[i];
            const QPointF c = v.missingY ? QPointF() : point(v.x, v.y, plot);
            if (v.missingY || !finite(c)) { flushLine(); continue; }
            run.append(c);
        }
        flushLine();
    }
    for (int i : indices) {
        const ScientificSample &v = samples[i];
        if (v.missingY) {
            // Gap marker on the X axis: a missing value is never drawn as 0.
            const double x = point(v.x, 1, plot).x();
            if (!std::isfinite(x)) continue;
            p->setPen(QPen(m_missing, line * 2));
            p->drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() - cap * 2));
            continue;
        }
        const QPointF c = point(v.x, v.y, plot);
        if (!finite(c)) continue;
        p->setPen(QPen(color, line));
        if (v.hasSigmaY && (m_mode == ErrorBars || !trace)) {
            const QPointF hi = point(v.x, v.y + v.sigmaYHigh, plot), lo = point(v.x, v.y - v.sigmaYLow, plot);
            if (finite(hi) && finite(lo)) {
                p->drawLine(lo, hi);
                p->drawLine(QPointF(c.x() - cap, hi.y()), QPointF(c.x() + cap, hi.y()));
                p->drawLine(QPointF(c.x() - cap, lo.y()), QPointF(c.x() + cap, lo.y()));
            }
        }
        if (v.hasSigmaX) {
            const QPointF left = point(v.x - v.sigmaXLow, v.y, plot), right = point(v.x + v.sigmaXHigh, v.y, plot);
            if (finite(left) && finite(right)) {
                p->drawLine(left, right);
                p->drawLine(QPointF(left.x(), c.y() - cap), QPointF(left.x(), c.y() + cap));
                p->drawLine(QPointF(right.x(), c.y() - cap), QPointF(right.x(), c.y() + cap));
            }
        }
        // A dense trace reads as a line; markers only when they do not touch.
        if (!trace || markers) {
            p->setBrush(trace ? QBrush(color) : QBrush(Qt::NoBrush));
            p->drawEllipse(c, radius, radius);
        }
    }
    p->setBrush(Qt::NoBrush);
}

void ScientificPlotRenderer::drawAnnotations(QPainter *p, const QRectF &plot) const
{
    if (!m_annotations) return;
    const QFontMetricsF fm(p->font());
    const double line = metric("lineWidth"), cap = metric("capHalfWidth"), gap = metric("labelGap");
    QPen pen(m_annotation, line, Qt::DashLine);
    for (const ChartAnnotation &a : m_annotations->annotations()) {
        const QString text = a.label.isEmpty() ? a.id : a.label;
        p->setPen(pen);
        if (a.kind == ChartAnnotation::VerticalLine) {
            const double x = point(a.x, 1, plot).x();
            if (!std::isfinite(x)) continue;
            p->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p->setPen(m_annotation); p->drawText(QPointF(x + gap, plot.top() + fm.ascent() + gap), text);
        } else if (a.kind == ChartAnnotation::HorizontalLine) {
            const double y = point(1, a.y, plot).y();
            if (!std::isfinite(y)) continue;
            p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p->setPen(m_annotation); p->drawText(QPointF(plot.left() + gap, y - gap), text);
        } else {
            const QPointF c = point(a.x, a.y, plot);
            if (!finite(c)) continue;
            p->setPen(QPen(m_annotation, line));
            p->drawLine(c - QPointF(cap, cap), c + QPointF(cap, cap));
            p->drawLine(c + QPointF(-cap, cap), c + QPointF(cap, -cap));
            p->drawText(c + QPointF(cap + gap, -gap), text);
        }
    }
}

void ScientificPlotRenderer::drawLegend(QPainter *p, const QRectF &plot) const
{
    QList<QPair<QString, QColor>> rows;
    for (int s = 0; s < m_series.size(); ++s)
        if (m_series[s]) rows.append({m_series[s]->label(), seriesColor(s)});
    if (rows.size() < 2) return;
    const QFontMetricsF fm(p->font());
    const double gap = metric("labelGap"), swatch = metric("pointRadius") * 2;
    double width = 0;
    for (const auto &row : rows) width = std::max(width, fm.horizontalAdvance(row.first));
    const QRectF box(plot.right() - width - swatch - gap * 4, plot.top() + gap,
                     width + swatch + gap * 3, rows.size() * fm.height() + gap * 2);
    p->setPen(m_grid); p->setBrush(m_background); p->drawRect(box);
    for (int i = 0; i < rows.size(); ++i) {
        const double y = box.top() + gap + i * fm.height();
        p->setPen(Qt::NoPen); p->setBrush(rows[i].second);
        p->drawEllipse(QPointF(box.left() + gap + swatch / 2, y + fm.height() / 2), swatch / 2, swatch / 2);
        p->setPen(m_text); p->setBrush(Qt::NoBrush);
        p->drawText(QRectF(box.left() + gap * 2 + swatch, y, width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, rows[i].first);
    }
}

void ScientificPlotRenderer::drawFigure(QPainter *p, const QSizeF &size) const
{
    p->fillRect(QRectF(QPointF(), size), m_background);
    const QRectF plot = plotRect(size);
    if (plot.isEmpty()) return;
    p->setRenderHint(QPainter::Antialiasing, true);
    QFont font = p->font();
    font.setFamily(m_metrics.value(QStringLiteral("fontFamily")).toString());
    font.setPixelSize(std::max(1, m_metrics.value(QStringLiteral("fontSize")).toInt()));
    p->setFont(font);
    if (!m_title.isEmpty()) {
        p->setPen(m_text);
        p->drawText(QRectF(plot.left(), 0, plot.width(), plot.top()), Qt::AlignCenter, m_title);
    }
    const QString issue = problem();
    if (!issue.isEmpty()) {
        p->setPen(QPen(m_grid, metric("lineWidth"))); p->drawRect(plot);
        p->setPen(m_text); p->drawText(plot, Qt::AlignCenter | Qt::TextWordWrap, issue);
        return;
    }
    drawSurface(p, plot);
    drawAxes(p, plot, size);
    p->save(); p->setClipRect(plot);
    for (int s = 0; s < m_series.size(); ++s) if (m_series[s]) drawSeries(p, plot, s);
    drawAnnotations(p, plot);
    const ScientificDataset *selected = m_series.value(m_selectedSeries);
    if (selected && selected->indexOf(m_selectedId) >= 0) {
        // Computed for this output size (projected() uses the item size).
        const ScientificSample &v = selected->samples()[selected->indexOf(m_selectedId)];
        const QPointF at = point(v.x, v.missingY ? 1 : v.y, plot);
        const QPointF centre(at.x(), v.missingY ? plot.bottom() : at.y());
        if (finite(centre)) {
            p->setPen(QPen(m_selection, metric("lineWidth") * 2)); p->setBrush(Qt::NoBrush);
            p->drawEllipse(centre, metric("pointRadius") * 2, metric("pointRadius") * 2);
        }
    }
    p->restore();
    drawLegend(p, plot);
    p->setPen(QPen(m_grid, metric("lineWidth"))); p->setBrush(Qt::NoBrush); p->drawRect(plot);
}

void ScientificPlotRenderer::paint(QPainter *painter) { drawFigure(painter, {width(), height()}); }

QJsonObject ScientificPlotRenderer::figureMetadata(const QSize &size) const
{
    QJsonArray series, painted, annotations;
    for (int s = 0; s < m_series.size(); ++s) {
        if (!m_series[s]) continue;
        series.append(QJsonObject::fromVariantMap(m_series[s]->metadata()));
        painted.append(int(this->painted(s).size()));
    }
    if (m_annotations)
        for (const ChartAnnotation &a : m_annotations->annotations())
            annotations.append(QJsonObject{{QStringLiteral("id"), a.id}, {QStringLiteral("label"), a.label},
                {QStringLiteral("kind"), int(a.kind)}, {QStringLiteral("x"), a.x}, {QStringLiteral("y"), a.y},
                {QStringLiteral("xUnit"), a.xUnit}, {QStringLiteral("yUnit"), a.yUnit}});
    const QRectF plot = plotRect(QSizeF(size));
    QJsonObject render{{QStringLiteral("title"), m_title}, {QStringLiteral("xLabel"), m_xLabel},
        {QStringLiteral("yLabel"), m_yLabel}, {QStringLiteral("mode"), m_mode == Band ? QStringLiteral("band") : QStringLiteral("errorBars")},
        {QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()},
        {QStringLiteral("xAxis"), axisJson(m_xAxis)}, {QStringLiteral("yAxis"), axisJson(m_yAxis)},
        {QStringLiteral("viewport"), QJsonArray{m_xStart, m_xEnd, m_yStart, m_yEnd}},
        {QStringLiteral("paintPointLimit"), m_paintPointLimit},
        {QStringLiteral("sampling"), QJsonObject{{QStringLiteral("trace"), QStringLiteral("m4-envelope")},
            {QStringLiteral("scatter"), QStringLiteral("stride-with-extremes")},
            {QStringLiteral("paintedPerSeries"), painted}, {QStringLiteral("omitted"), omittedCount()}}},
        {QStringLiteral("problem"), problem()}};
    // Everything else drawFigure() reads: selection ring, colours, fonts, metrics (D-141).
    const ScientificDataset *selected = m_series.value(m_selectedSeries);
    render.insert(QStringLiteral("selection"), selected && selected->indexOf(m_selectedId) >= 0
        ? QJsonValue(QJsonObject{{QStringLiteral("series"), m_selectedSeries}, {QStringLiteral("sampleId"), m_selectedId}})
        : QJsonValue(QJsonValue::Null));
    render.insert(QStringLiteral("style"), ScientificSampling::styleJson(m_metrics,
        {{QStringLiteral("backgroundColor"), m_background}, {QStringLiteral("gridColor"), m_grid},
         {QStringLiteral("textColor"), m_text}, {QStringLiteral("missingColor"), m_missing},
         {QStringLiteral("selectionColor"), m_selection}, {QStringLiteral("annotationColor"), m_annotation}},
        {{QStringLiteral("seriesColors"), m_seriesColors}, {QStringLiteral("surfaceColors"), m_surfaceColors}}));
    if (m_surface && m_surface->hasValues())
        render.insert(QStringLiteral("surface"), QJsonObject{
            {QStringLiteral("interpolation"), m_interpolation == ScientificGrid::Nearest ? QStringLiteral("nearest") : QStringLiteral("bilinear")},
            {QStringLiteral("colorDomain"), QJsonArray{m_surface->zMinimum(), m_surface->zMaximum()}},
            {QStringLiteral("rasterWidth"), std::clamp(int(std::ceil(plot.width())), 1, SurfaceRasterLimit)},
            {QStringLiteral("rasterHeight"), std::clamp(int(std::ceil(plot.height())), 1, SurfaceRasterLimit)}});
    return {{QStringLiteral("schema"), QStringLiteral("QindaTK.scientificFigure.v1")},
            {QStringLiteral("series"), series},
            {QStringLiteral("surface"), m_surface ? QJsonValue(QJsonObject::fromVariantMap(m_surface->metadata())) : QJsonValue()},
            {QStringLiteral("annotations"), annotations}, {QStringLiteral("render"), render}};
}
QString ScientificPlotRenderer::metadataJson(int width, int height) const
{ return QString::fromUtf8(QJsonDocument(figureMetadata(QSize(width, height))).toJson(QJsonDocument::Compact)); }

QImage ScientificPlotRenderer::renderImage(const QSize &size) const
{
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image); drawFigure(&painter, QSizeF(size)); painter.end();
    image.setText(QStringLiteral("QindaTK.ScientificFigure"), metadataJson(size.width(), size.height()));
    return image;
}
bool ScientificPlotRenderer::exportPng(const QString &path, const QSize &size) const
{
    const QImage image = renderImage(size);
    if (image.isNull()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) return false;
    return file.commit();
}
bool ScientificPlotRenderer::exportSvg(const QString &path, const QSize &size) const
{
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192) return false;
    QByteArray bytes; QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return false;
    QSvgGenerator svg; svg.setOutputDevice(&buffer); svg.setSize(size); svg.setViewBox(QRect(QPoint(), size));
    svg.setTitle(m_title);
    QPainter painter(&svg); drawFigure(&painter, QSizeF(size)); painter.end(); buffer.close();
    const qsizetype end = bytes.lastIndexOf("</svg>");
    if (end < 0) return false;
    bytes.insert(end, "<metadata id=\"qindatk-scientific-figure\" data-encoding=\"base64-json\">"
                 + metadataJson(size.width(), size.height()).toUtf8().toBase64() + "</metadata>");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

} // namespace QindaTK
