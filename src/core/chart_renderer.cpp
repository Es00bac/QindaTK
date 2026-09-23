// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_renderer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QSet>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int MaxDatasets = 64;
constexpr int MaxProjectedPoints = 200000;
QPointF screenPoint(const ChartProjectedPoint &p, const QRectF &r,
                    double xs, double xe, double ys, double ye)
{
    return {r.left() + (p.x - xs) / (xe - xs) * r.width(),
            r.bottom() - (p.y - ys) / (ye - ys) * r.height()};
}
}

ChartRenderer::ChartRenderer(QQuickItem *parent)
    : QQuickPaintedItem(parent), m_histogramSelection(this)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setFlag(ItemHasContents, true);
    setAntialiasing(true);
    setActiveFocusOnTab(true);
}

void ChartRenderer::setXAxis(ChartAxis *axis)
{
    if (m_xAxis == axis) return;
    if (m_xAxis) disconnect(m_xAxis, nullptr, this, nullptr);
    m_xAxis = axis;
    if (axis) connect(axis, &ChartAxis::changed, this, [this] {
        if (m_xAxis) m_xAxis->setViewport(0, 1);
        m_hitDirty = true;
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
        emit annotationsChanged(); update();
    });
    if (axis) connect(axis, &ChartAxis::viewportChanged, this, &ChartRenderer::syncViewportFromAxes);
    if (axis) connect(axis, &QObject::destroyed, this, [this] {
        m_hitDirty = true;
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
        else m_tableModel.clear();
        emit axesChanged(); emit annotationsChanged(); update();
    });
    syncViewportFromAxes(); emit axesChanged(); emit annotationsChanged(); update();
}
void ChartRenderer::setYAxis(ChartAxis *axis)
{
    if (m_yAxis == axis) return;
    if (m_yAxis) disconnect(m_yAxis, nullptr, this, nullptr);
    m_yAxis = axis;
    if (axis) connect(axis, &ChartAxis::changed, this, [this] {
        if (m_yAxis) m_yAxis->setViewport(0, 1);
        m_hitDirty = true;
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
        emit annotationsChanged(); update();
    });
    if (axis) connect(axis, &ChartAxis::viewportChanged, this, &ChartRenderer::syncViewportFromAxes);
    if (axis) connect(axis, &QObject::destroyed, this, [this] {
        m_hitDirty = true;
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
        else m_tableModel.clear();
        emit axesChanged(); emit annotationsChanged(); update();
    });
    syncViewportFromAxes(); emit axesChanged(); emit annotationsChanged(); update();
}

QQmlListProperty<ChartDataset> ChartRenderer::datasets()
{ return {this, this, &appendDataset, &datasetCount, &datasetAt, &clearDatasets}; }
void ChartRenderer::appendDataset(QQmlListProperty<ChartDataset> *p, ChartDataset *d)
{ if (d) static_cast<ChartRenderer *>(p->object)->connectDataset(d); }
qsizetype ChartRenderer::datasetCount(QQmlListProperty<ChartDataset> *p)
{ return static_cast<ChartRenderer *>(p->object)->m_datasets.size(); }
ChartDataset *ChartRenderer::datasetAt(QQmlListProperty<ChartDataset> *p, qsizetype i)
{ return static_cast<ChartRenderer *>(p->object)->m_datasets.value(i); }
void ChartRenderer::clearDatasets(QQmlListProperty<ChartDataset> *p)
{
    auto *self = static_cast<ChartRenderer *>(p->object);
    for (auto *d : std::as_const(self->m_datasets)) disconnect(d, nullptr, self, nullptr);
    self->m_datasets.clear(); self->m_hitDirty = true;
    if (self->m_xAxis) self->m_tableModel.reset({}, *self->m_xAxis, &self->m_histogram);
    emit self->datasetsChanged(); self->update();
}
void ChartRenderer::connectDataset(ChartDataset *dataset)
{
    if (m_datasets.size() >= MaxDatasets || m_datasets.contains(dataset)) return;
    m_datasets.append(dataset);
    connect(dataset, &ChartDataset::changed, this, [this] { m_hitDirty = true; update(); });
    connect(dataset, &ChartDataset::rowsChanged, this, [this] {
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
    });
    connect(dataset, &ChartDataset::tableMetadataChanged, this, [this] {
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
    });
    connect(dataset, &QObject::destroyed, this, [this, dataset] {
        m_datasets.removeAll(dataset); m_hitDirty = true;
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
        emit datasetsChanged(); update();
    });
    emit datasetsChanged(); m_hitDirty = true;
    if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
    update();
}

void ChartRenderer::setBackgroundColor(const QColor &v)
{ if (v.isValid() && m_background != v) { m_background = v; emit styleChanged(); update(); } }
void ChartRenderer::setGridColor(const QColor &v)
{ if (v.isValid() && m_grid != v) { m_grid = v; emit styleChanged(); update(); } }
void ChartRenderer::setAxisColor(const QColor &v)
{ if (v.isValid() && m_axis != v) { m_axis = v; emit styleChanged(); update(); } }
void ChartRenderer::setDefaultSeriesColor(const QColor &v)
{ if (v.isValid() && m_defaultSeriesColor != v) { m_defaultSeriesColor = v; emit styleChanged(); update(); } }
void ChartRenderer::setHistogramColor(const QColor &v)
{ if (v.isValid() && m_histogramColor != v) { m_histogramColor = v; emit styleChanged(); update(); } }
void ChartRenderer::setMetrics(const QVariantMap &v)
{ if (m_metrics != v) { m_metrics = v; emit styleChanged(); update(); } }
qreal ChartRenderer::metric(const char *name) const
{ return m_metrics.value(QLatin1String(name)).toReal(); }
void ChartRenderer::setPaintPointLimit(int v)
{ v = qBound(32, v, 10000); if (m_paintPointLimit != v) { m_paintPointLimit = v; emit paintPointLimitChanged(); update(); } }

bool ChartRenderer::setHistogram(const QVariantList &edges, const QVariantList &counts,
                                 int normalization, const QString &channel, const QString &unit)
{
    const auto reject = [this](const QString &message) {
        m_histogram = {};
        m_histogram.error = message;
        m_histogramSelection.clear();
        if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
        emit histogramChanged(); update(); return false;
    };
    if (normalization < ChartHistogram::Count || normalization > ChartHistogram::Density)
        return reject(QStringLiteral("Unknown histogram normalization"));
    QVector<double> edgeValues; QVector<quint64> countValues;
    if (edges.size() > 100001 || counts.size() > 100000)
        return reject(QStringLiteral("Histogram exceeds 100,000 bins"));
    edgeValues.reserve(edges.size()); countValues.reserve(counts.size());
    for (const QVariant &v : edges) edgeValues.append(v.toDouble());
    for (const QVariant &v : counts) {
        bool ok = false; const qulonglong count = v.toULongLong(&ok);
        if (!ok) return reject(QStringLiteral("Histogram counts must be nonnegative integers"));
        countValues.append(count);
    }
    auto result = ChartHistogram::fromCounts(edgeValues, countValues,
        ChartHistogram::Normalization(normalization), channel, unit);
    if (result.valid() && (!m_xAxis || !m_yAxis
        || (m_xAxis->kind() != ChartAxis::Numeric && m_xAxis->kind() != ChartAxis::Logarithmic)
        || m_yAxis->kind() != ChartAxis::Numeric || m_yAxis->minimum() > 0 || m_yAxis->maximum() < 0
        || m_xAxis->unit() != unit)) {
        result.error = QStringLiteral("Histogram requires a numeric/logarithmic X axis with matching units and a linear Y axis that includes zero");
        result.bins.clear();
    }
    if (result.valid() && m_xAxis->kind() == ChartAxis::Logarithmic && !result.bins.isEmpty()
        && result.bins.first().lower <= 0) {
        result.error = QStringLiteral("A logarithmic histogram X axis requires positive bin edges");
        result.bins.clear();
    }
    m_histogram = std::move(result);
    m_histogramSelection.clear();
    if (m_xAxis) m_tableModel.reset(seriesPointers(), *m_xAxis, &m_histogram);
    emit histogramChanged(); update();
    return m_histogram.valid();
}

QVector<ChartSeries *> ChartRenderer::seriesPointers() const
{
    QVector<ChartSeries *> out;
    for (ChartDataset *d : m_datasets) if (d) out.append(d->series());
    return out;
}
ChartPlot ChartRenderer::plot() const
{
    if (!m_xAxis || !m_yAxis) return {{}, QStringLiteral("Chart axes are not assigned")};
    qsizetype total = 0;
    for (const auto *d : m_datasets) if (d) total += d->rowCount();
    if (total > MaxProjectedPoints) return {{}, QStringLiteral("Chart point budget exceeded (200,000)")};
    return ChartData::project(seriesPointers(), *m_xAxis, *m_yAxis);
}
QRectF ChartRenderer::plotRect() const
{ return QRectF(metric("leftMargin"), metric("topMargin"),
    std::max(0.0, width() - metric("leftMargin") - metric("rightMargin")),
    std::max(0.0, height() - metric("topMargin") - metric("bottomMargin"))); }

void ChartRenderer::syncViewportFromAxes()
{
    const double xs = m_xAxis ? m_xAxis->viewportStart() : 0;
    const double xe = m_xAxis ? m_xAxis->viewportEnd() : 1;
    const double ys = m_yAxis ? m_yAxis->viewportStart() : 0;
    const double ye = m_yAxis ? m_yAxis->viewportEnd() : 1;
    if (xs == m_xStart && xe == m_xEnd && ys == m_yStart && ye == m_yEnd) return;
    m_xStart = xs; m_xEnd = xe; m_yStart = ys; m_yEnd = ye;
    m_hitDirty = true; emit viewportChanged(); update();
}

void ChartRenderer::setViewport(double xs, double xe, double ys, double ye)
{
    if (!std::isfinite(xs) || !std::isfinite(xe) || !std::isfinite(ys) || !std::isfinite(ye)) return;
    const double minSpan = 1e-6;
    xs = std::clamp(xs, 0.0, 1.0); xe = std::clamp(xe, 0.0, 1.0);
    ys = std::clamp(ys, 0.0, 1.0); ye = std::clamp(ye, 0.0, 1.0);
    if (xe - xs < minSpan || ye - ys < minSpan) return;
    if (m_xStart == xs && m_xEnd == xe && m_yStart == ys && m_yEnd == ye) return;
    if (m_xAxis) m_xAxis->setViewport(xs, xe);
    else { m_xStart = xs; m_xEnd = xe; }
    if (m_yAxis) m_yAxis->setViewport(ys, ye);
    else { m_yStart = ys; m_yEnd = ye; }
    syncViewportFromAxes();
}
void ChartRenderer::pan(double dx, double dy)
{
    const double w = m_xEnd - m_xStart, h = m_yEnd - m_yStart;
    double xs = m_xStart - dx * w, ys = m_yStart + dy * h;
    xs = std::clamp(xs, 0.0, 1.0 - w); ys = std::clamp(ys, 0.0, 1.0 - h);
    setViewport(xs, xs + w, ys, ys + h);
}
void ChartRenderer::zoom(double factor, double xa, double ya)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    xa = std::clamp(xa, 0.0, 1.0); ya = std::clamp(ya, 0.0, 1.0);
    const double x = m_xStart + xa * viewX(), y = m_yStart + ya * viewY();
    const double w = std::clamp(viewX() / factor, 1e-6, 1.0), h = std::clamp(viewY() / factor, 1e-6, 1.0);
    setViewport(std::clamp(x - xa * w, 0.0, 1.0 - w), std::clamp(x + (1 - xa) * w, w, 1.0),
                std::clamp(y - ya * h, 0.0, 1.0 - h), std::clamp(y + (1 - ya) * h, h, 1.0));
}
void ChartRenderer::rebuildHitIndex() const
{ if (m_hitDirty) { m_hitIndex.rebuild(plot()); m_hitDirty = false; } }

void ChartRenderer::draw(QPainter *p, const QSizeF &size) const
{
    p->save(); p->setRenderHint(QPainter::Antialiasing, true);
    p->fillRect(QRectF(QPointF(0, 0), size), m_background);
    if (!m_xAxis || !m_yAxis || !m_xAxis->isValid() || !m_yAxis->isValid()) {
        p->setPen(m_axis); p->drawText(QRectF(metric("smallGap"), metric("smallGap"),
            size.width() - metric("smallGap") * 2, size.height() - metric("smallGap") * 2),
            Qt::AlignCenter, !m_xAxis || !m_yAxis ? QStringLiteral("Chart axes are not assigned") : m_xAxis->error() + QStringLiteral(" ") + m_yAxis->error());
        p->restore(); return;
    }
    const QRectF r(metric("leftMargin"), metric("topMargin"),
        std::max(0.0, size.width() - metric("leftMargin") - metric("rightMargin")),
        std::max(0.0, size.height() - metric("topMargin") - metric("bottomMargin")));
    QFont font(m_metrics.value(QStringLiteral("fontFamily")).toString());
    font.setPixelSize(qMax(1, qRound(metric("fontPixelSize"))));
    p->setFont(font);
    p->setPen(QPen(m_grid, metric("gridWidth")));
    const auto xticks = m_xAxis->ticks(5), yticks = m_yAxis->ticks(5);
    for (const ChartTick &tick : xticks) {
        const double f = m_xAxis->kind() == ChartAxis::DateTime ? m_xAxis->mapUtc(tick.utcMs)
                         : m_xAxis->kind() == ChartAxis::Category ? m_xAxis->map(m_xAxis->categoryIndex(tick.label))
                         : m_xAxis->map(tick.value);
        if (!std::isfinite(f) || f < m_xStart || f > m_xEnd) continue;
        const double x = r.left() + (f - m_xStart) / viewX() * r.width();
        p->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        const qreal half = metric("tickHalfWidth");
        p->setPen(m_axis); p->drawText(QRectF(x - half, r.bottom() + metric("smallGap"), half * 2,
            metric("axisLabelHeight")), Qt::AlignHCenter | Qt::AlignTop, tick.label); p->setPen(m_grid);
    }
    for (const ChartTick &tick : yticks) {
        const double f = m_yAxis->map(tick.value);
        if (!std::isfinite(f) || f < m_yStart || f > m_yEnd) continue;
        const double y = r.bottom() - (f - m_yStart) / viewY() * r.height();
        p->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p->setPen(m_axis); p->drawText(QRectF(0, y - metric("axisLabelHeight") / 2,
            metric("leftMargin") - metric("smallGap"), metric("axisLabelHeight")), Qt::AlignRight | Qt::AlignVCenter,
            tick.label + (m_yAxis->unit().isEmpty() ? QString() : QStringLiteral(" ") + m_yAxis->unit())); p->setPen(m_grid);
    }
    p->setPen(QPen(m_axis, metric("gridWidth"))); p->drawRect(r);
    const ChartPlot projection = plot();
    if (!projection.valid()) {
        p->setPen(m_axis); p->drawText(r, Qt::AlignCenter, projection.error); p->restore(); return;
    }
    p->save(); p->setClipRect(r.adjusted(metric("gridWidth"), metric("gridWidth"),
        -metric("gridWidth"), -metric("gridWidth")));
    for (const auto &series : projection.series) {
        const ChartDataset *dataset = nullptr;
        for (const auto *d : m_datasets) if (d && d->id() == series.id) { dataset = d; break; }
        if (!dataset) continue;
        QVector<ChartPoint> source;
        QHash<QString, ChartProjectedPoint> projectedById;
        source.reserve(series.points.size());
        for (const auto &point : series.points) {
            ChartPoint cp; cp.id = point.pointId; cp.x = point.x; cp.y = point.y; cp.gap = point.gap; source.append(cp);
            projectedById.insert(point.pointId, point);
        }
        const auto reduced = ChartData::decimate(source, m_paintPointLimit);
        QVector<ChartProjectedPoint> paintPoints;
        paintPoints.reserve(reduced.points.size());
        for (const auto &point : reduced.points) paintPoints.append(projectedById.value(point.id));
        const QColor color = dataset->color().isValid() ? dataset->color() : m_defaultSeriesColor;
        const QColor fill = dataset->fillColor().isValid() ? dataset->fillColor() : m_histogramColor;
        p->setPen(QPen(color, metric("lineWidth"), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (series.kind == ChartSeries::Line || series.kind == ChartSeries::Step || series.kind == ChartSeries::Area || series.kind == ChartSeries::StackedArea) {
            QPainterPath line, area; bool open = false; QPointF first, last, firstBase, lastBase;
            const bool isArea = series.kind == ChartSeries::Area || series.kind == ChartSeries::StackedArea;
            const auto flushSegment = [&] {
                if (!open) return;
                if (isArea) {
                    area.lineTo(last.x(), lastBase.y());
                    area.lineTo(first.x(), firstBase.y());
                    area.closeSubpath();
                    p->fillPath(area, fill);
                }
                p->drawPath(line);
                line = QPainterPath(); area = QPainterPath(); open = false;
            };
            for (const auto &point : paintPoints) {
                if (point.gap) { flushSegment(); continue; }
                if (point.x < m_xStart || point.x > m_xEnd) { flushSegment(); continue; }
                const QPointF pos = screenPoint(point, r, m_xStart, m_xEnd, m_yStart, m_yEnd);
                const QPointF base = screenPoint({point.seriesId, point.pointId, {}, point.x, point.baselineY, point.baselineY}, r, m_xStart, m_xEnd, m_yStart, m_yEnd);
                if (!open) { line.moveTo(pos); area.moveTo(pos); first = last = pos; firstBase = lastBase = base; open = true; }
                else if (series.kind == ChartSeries::Step) { line.lineTo(pos.x(), last.y()); line.lineTo(pos); area.lineTo(pos.x(), last.y()); area.lineTo(pos); }
                else { line.lineTo(pos); area.lineTo(pos); }
                last = pos; lastBase = base;
            }
            flushSegment();
        } else {
            for (const auto &point : paintPoints) {
                if (point.gap) continue;
                const QPointF pos = screenPoint(point, r, m_xStart, m_xEnd, m_yStart, m_yEnd);
                if (series.kind == ChartSeries::Bar || series.kind == ChartSeries::StackedBar || series.kind == ChartSeries::Waterfall) {
                    const QPointF base = screenPoint({{}, {}, {}, point.x, point.baselineY}, r, m_xStart, m_xEnd, m_yStart, m_yEnd);
                    const double w = std::max(metric("minimumBarWidth"), point.barWidth / viewX() * r.width());
                    p->fillRect(QRectF(pos.x() - w / 2, std::min(pos.y(), base.y()), w, std::max(1.0, std::abs(base.y() - pos.y()))), fill);
                } else if (series.kind == ChartSeries::Vector) {
                    const QPointF end(r.left() + (point.endX - m_xStart) / viewX() * r.width(), r.bottom() - (point.endY - m_yStart) / viewY() * r.height());
                    const qreal arrow = metric("arrowSize");
                    p->drawLine(pos, end); p->drawLine(end, end + QPointF(-arrow, -arrow * 0.6));
                    p->drawLine(end, end + QPointF(-arrow, arrow * 0.6));
                } else {
                    const double radius = series.kind == ChartSeries::Bubble
                        ? std::clamp(std::sqrt(std::max(0.0, point.size)), metric("bubbleMinRadius"), metric("bubbleMaxRadius"))
                        : metric("pointRadius");
                    p->setBrush(fill); p->drawEllipse(pos, radius, radius);
                }
            }
        }
    }
    if (m_histogram.valid()) {
        p->setPen(QPen(m_axis, metric("gridWidth"))); p->setBrush(m_histogramColor);
        for (int binIndex = 0; binIndex < m_histogram.bins.size(); ++binIndex) {
            const auto &bin = m_histogram.bins.at(binIndex);
            const double x1 = m_xAxis->map(bin.lower), x2 = m_xAxis->map(bin.upper);
            const double y = m_yAxis->map(bin.displayed), base = m_yAxis->map(0);
            if (!std::isfinite(x1) || !std::isfinite(x2) || !std::isfinite(y) || !std::isfinite(base)) continue;
            const double left = r.left() + (x1 - m_xStart) / viewX() * r.width();
            const double right = r.left() + (x2 - m_xStart) / viewX() * r.width();
            const double top = r.bottom() - (y - m_yStart) / viewY() * r.height();
            const double bottom = r.bottom() - (base - m_yStart) / viewY() * r.height();
            p->setPen(QPen(m_histogramSelection.valid()
                && binIndex >= m_histogramSelection.firstBin()
                && binIndex <= m_histogramSelection.lastBin()
                    ? m_defaultSeriesColor : m_axis,
                m_histogramSelection.valid() && binIndex >= m_histogramSelection.firstBin()
                    && binIndex <= m_histogramSelection.lastBin()
                    ? metric("selectionWidth") : metric("gridWidth")));
            p->drawRect(QRectF(std::min(left, right), std::min(top, bottom),
                std::max(metric("minimumBarWidth"), std::abs(right - left)),
                std::max(metric("gridWidth"), std::abs(bottom - top))));
        }
    }
    drawAnnotations(p, r);
    if (!m_selectedSeriesId.isEmpty()) {
        rebuildHitIndex(); const auto *selected = m_hitIndex.find(m_selectedSeriesId, m_selectedPointId);
        if (selected) { p->setBrush(Qt::NoBrush); p->setPen(QPen(m_axis, metric("selectionWidth")));
            p->drawEllipse(screenPoint(*selected, r, m_xStart, m_xEnd, m_yStart, m_yEnd), metric("selectionRadius"), metric("selectionRadius")); }
    }
    p->restore(); p->restore();
}

void ChartRenderer::paint(QPainter *painter) { draw(painter, QSizeF(width(), height())); }
QImage ChartRenderer::renderImage(const QSize &size) const
{
    const QSize bounded(qBound(1, size.width(), 8192), qBound(1, size.height(), 8192));
    QImage image(bounded, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); draw(&painter, bounded); painter.end(); emit const_cast<ChartRenderer *>(this)->exportImageReady(image); return image;
}

bool ChartRenderer::exportPng(const QString &path, const QSize &size) const
{
    if (path.isEmpty() || size.width() < 1 || size.height() < 1
        || size.width() > 8192 || size.height() > 8192) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    draw(&painter, size);
    painter.end();
    return image.save(&file, "PNG") && file.commit();
}

bool ChartRenderer::exportSvg(const QString &path, const QSize &size) const
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
    draw(&painter, size);
    painter.end();
    return file.commit();
}

void ChartRenderer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) { m_dragging = true; m_dragStart = event->position(); event->accept(); return; }
    const QRectF r = plotRect();
    if (!r.contains(event->position())) return;
    forceActiveFocus();
    const int bin = histogramBinAt(event->position());
    if (bin >= 0 && selectHistogramBins(bin, bin)) {
        m_histogramDragStart = bin; event->accept(); return;
    }
    if (selectAnnotationAt(event->position().x(), event->position().y())) {
        event->accept(); return;
    }
    rebuildHitIndex();
    const double nx = m_xStart + (event->position().x() - r.left()) / r.width() * viewX();
    const double ny = m_yStart + (r.bottom() - event->position().y()) / r.height() * viewY();
    const auto hit = m_hitIndex.nearest(nx, ny, metric("hitRadius") / std::max(1.0, std::min(r.width(), r.height())));
    if (!hit.pointId.isEmpty()) {
        m_selectedSeriesId = hit.seriesId; m_selectedPointId = hit.pointId;
        emit selectionChanged(); emit pointSelected(hit.seriesId, hit.pointId); update(); event->accept();
    }
}

void ChartRenderer::keyPressEvent(QKeyEvent *event)
{
    if (!navigateSelection(event->key())) { event->ignore(); return; }
    event->accept();
}

bool ChartRenderer::navigateSelection(int key)
{
    QVector<QPair<QString, QString>> points;
    const ChartPlot current = plot();
    for (const auto &series : current.series)
        for (const auto &point : series.points)
            if (!point.gap) points.append({point.seriesId, point.pointId});
    if (points.isEmpty()) return false;
    int index = -1;
    for (int i = 0; i < points.size(); ++i)
        if (points.at(i).first == m_selectedSeriesId && points.at(i).second == m_selectedPointId) { index = i; break; }
    switch (key) {
    case Qt::Key_Left: case Qt::Key_Up: index = qBound(0, index < 0 ? 0 : index - 1, points.size() - 1); break;
    case Qt::Key_Right: case Qt::Key_Down: index = qBound(0, index + 1, points.size() - 1); break;
    case Qt::Key_Home: index = 0; break;
    case Qt::Key_End: index = points.size() - 1; break;
    default: return false;
    }
    m_selectedSeriesId = points.at(index).first; m_selectedPointId = points.at(index).second;
    emit selectionChanged(); emit pointSelected(m_selectedSeriesId, m_selectedPointId); update(); return true;
}
void ChartRenderer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_histogramDragStart >= 0 && (event->buttons() & Qt::LeftButton)) {
        const int bin = histogramBinAt(event->position());
        if (bin >= 0) selectHistogramBins(m_histogramDragStart, bin);
        event->accept(); return;
    }
    if (!m_dragging || !(event->buttons() & Qt::MiddleButton)) return;
    const QPointF delta = event->position() - m_dragStart; m_dragStart = event->position();
    const QRectF r = plotRect(); if (r.width() > 0 && r.height() > 0) pan(delta.x() / r.width(), delta.y() / r.height());
    event->accept();
}
void ChartRenderer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_histogramDragStart >= 0) {
        m_histogramDragStart = -1; event->accept(); return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_dragging = false; event->accept(); return;
    }
    event->ignore();
}
void ChartRenderer::wheelEvent(QWheelEvent *event)
{
    const QRectF r = plotRect(); if (!r.contains(event->position()) || r.isEmpty()) return;
    const double factor = std::pow(1.2, event->angleDelta().y() / 120.0);
    const double xa = (event->position().x() - r.left()) / r.width();
    const double ya = (r.bottom() - event->position().y()) / r.height();
    zoom(factor, xa, ya); event->accept();
}

} // namespace QindaTK
