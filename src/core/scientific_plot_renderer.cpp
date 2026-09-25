// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_plot_renderer.h"
#include "scientific_sampling.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace QindaTK {

ScientificPlotRenderer::ScientificPlotRenderer(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    connect(this, &ScientificPlotRenderer::styleChanged, this, [this] { update(); });
    connect(this, &ScientificPlotRenderer::seriesChanged, this, [this] { update(); });
    for (auto signal : {&ScientificPlotRenderer::seriesChanged, &ScientificPlotRenderer::styleChanged,
                        &ScientificPlotRenderer::viewportChanged})
        connect(this, signal, this, &ScientificPlotRenderer::summaryChanged);
}

QQmlListProperty<ScientificDataset> ScientificPlotRenderer::seriesList()
{
    return {this, nullptr, &appendSeriesTo, &countSeries, &seriesAt, &clearSeriesOf};
}
void ScientificPlotRenderer::appendSeriesTo(QQmlListProperty<ScientificDataset> *list, ScientificDataset *value)
{ static_cast<ScientificPlotRenderer *>(list->object)->appendSeries(value); }
qsizetype ScientificPlotRenderer::countSeries(QQmlListProperty<ScientificDataset> *list)
{ return static_cast<ScientificPlotRenderer *>(list->object)->m_series.size(); }
ScientificDataset *ScientificPlotRenderer::seriesAt(QQmlListProperty<ScientificDataset> *list, qsizetype index)
{ return static_cast<ScientificPlotRenderer *>(list->object)->m_series.value(index); }
void ScientificPlotRenderer::clearSeriesOf(QQmlListProperty<ScientificDataset> *list)
{ static_cast<ScientificPlotRenderer *>(list->object)->clearSeries(); }

void ScientificPlotRenderer::watch(QObject *object)
{
    if (!object) return;
    if (auto *d = qobject_cast<ScientificDataset *>(object))
        connect(d, &ScientificDataset::changed, this, &ScientificPlotRenderer::dataChangedSlot);
    else if (auto *g = qobject_cast<ScientificGrid *>(object))
        connect(g, &ScientificGrid::changed, this, &ScientificPlotRenderer::dataChangedSlot);
    else if (auto *a = qobject_cast<ChartAnnotationModel *>(object))
        connect(a, &ChartAnnotationModel::annotationsChanged, this, &ScientificPlotRenderer::dataChangedSlot);
    else if (auto *axis = qobject_cast<ChartAxis *>(object))
        connect(axis, &ChartAxis::changed, this, &ScientificPlotRenderer::dataChangedSlot);
    // QPointer members clear themselves; repaint and re-validate selection.
    connect(object, &QObject::destroyed, this, [this] { QMetaObject::invokeMethod(this, [this] { dataChangedSlot(); }, Qt::QueuedConnection); });
}
void ScientificPlotRenderer::dataChangedSlot()
{
    const ScientificDataset *d = m_series.value(m_selectedSeries);
    if (m_selectedSeries >= 0 && (!d || d->indexOf(m_selectedId) < 0)) {
        m_selectedSeries = -1; m_selectedId.clear(); emit selectionChanged();
    }
    emit seriesChanged();
}
void ScientificPlotRenderer::appendSeries(ScientificDataset *dataset)
{
    m_series.append(dataset);
    if (dataset && m_series.count(dataset) == 1) watch(dataset);
    emit seriesChanged();
}
void ScientificPlotRenderer::clearSeries()
{
    for (const auto &d : std::as_const(m_series)) if (d) disconnect(d, nullptr, this, nullptr);
    m_series.clear();
    // AGENT-GUARD: clearing drops the selection; a rebinding QML list is
    // cleared and re-appended, and a stale series index would point elsewhere.
    if (m_selectedSeries >= 0) { m_selectedSeries = -1; m_selectedId.clear(); emit selectionChanged(); }
    emit seriesChanged();
}
void ScientificPlotRenderer::setSurface(ScientificGrid *value)
{
    if (m_surface == value) return;
    if (m_surface) disconnect(m_surface, nullptr, this, nullptr);
    m_surface = value; watch(value); emit seriesChanged();
}
void ScientificPlotRenderer::setSurfaceInterpolation(ScientificGrid::Interpolation value)
{ if (m_interpolation == value) return; m_interpolation = value; emit styleChanged(); }
void ScientificPlotRenderer::setAnnotations(ChartAnnotationModel *value)
{
    if (m_annotations == value) return;
    if (m_annotations) disconnect(m_annotations, nullptr, this, nullptr);
    m_annotations = value; watch(value); emit seriesChanged();
}
void ScientificPlotRenderer::setXAxis(ChartAxis *value)
{
    if (m_xAxis == value) return;
    if (m_xAxis) disconnect(m_xAxis, nullptr, this, nullptr);
    m_xAxis = value; watch(value); emit axesChanged(); emit seriesChanged();
}
void ScientificPlotRenderer::setYAxis(ChartAxis *value)
{
    if (m_yAxis == value) return;
    if (m_yAxis) disconnect(m_yAxis, nullptr, this, nullptr);
    m_yAxis = value; watch(value); emit axesChanged(); emit seriesChanged();
}
void ScientificPlotRenderer::setMode(Mode value)
{ if (m_mode == value) return; m_mode = value; emit styleChanged(); }
void ScientificPlotRenderer::setPaintPointLimit(int value)
{ value = std::clamp(value, 64, 16384); if (m_paintPointLimit == value) return; m_paintPointLimit = value; emit styleChanged(); }

bool ScientificPlotRenderer::axesDrawable() const
{
    auto ok = [](const ChartAxis *a) {
        return a && a->isValid() && (a->kind() == ChartAxis::Numeric || a->kind() == ChartAxis::Logarithmic);
    };
    return ok(m_xAxis) && ok(m_yAxis);
}

QString ScientificPlotRenderer::problem() const
{
    if (!axesDrawable()) return tr("Declare valid numeric or logarithmic X and Y axes");
    const QString xu = m_xAxis->unit(), yu = m_yAxis->unit();
    for (const auto &d : m_series) {
        if (!d) continue;
        if (d->dimensions() != 2) return tr("Series %1 is 3D; show it in ScientificPlot3D").arg(d->label());
        if (d->xUnit() != xu || d->yUnit() != yu)
            return tr("Series %1 is in (%2, %3) but the axes are (%4, %5)")
                .arg(d->label(), d->xUnit(), d->yUnit(), xu, yu);
    }
    if (m_surface && (m_surface->xUnit() != xu || m_surface->yUnit() != yu))
        return tr("Surface %1 is in (%2, %3) but the axes are (%4, %5)")
            .arg(m_surface->label(), m_surface->xUnit(), m_surface->yUnit(), xu, yu);
    if (m_annotations)
        for (const ChartAnnotation &a : m_annotations->annotations()) {
            if (a.hasUtc || a.hasCategory) return tr("Annotation %1 needs a numeric x").arg(a.id);
            if ((!a.xUnit.isEmpty() && a.xUnit != xu) || (!a.yUnit.isEmpty() && a.yUnit != yu))
                return tr("Annotation %1 units differ from the axes").arg(a.id);
        }
    return {};
}

QVariantMap ScientificPlotRenderer::selectedSample() const
{
    const ScientificDataset *d = m_series.value(m_selectedSeries);
    if (!d) return {};
    QVariantMap out = d->sampleById(m_selectedId);
    if (out.isEmpty()) return out;
    out.insert(QStringLiteral("series"), m_selectedSeries);
    out.insert(QStringLiteral("sourceId"), d->sourceId());
    out.insert(QStringLiteral("xUnit"), d->xUnit());
    out.insert(QStringLiteral("yUnit"), d->yUnit());
    return out;
}
bool ScientificPlotRenderer::selectSample(int series, const QString &id)
{
    const ScientificDataset *d = m_series.value(series);
    if (!d || d->indexOf(id) < 0) return false;
    if (m_selectedSeries == series && m_selectedId == id) return true;
    m_selectedSeries = series; m_selectedId = id;
    emit selectionChanged(); emit sampleSelected(series, id); update();
    return true;
}
bool ScientificPlotRenderer::navigateSelection(int key)
{
    QList<int> filled;
    for (int i = 0; i < m_series.size(); ++i) if (m_series[i] && m_series[i]->count()) filled.append(i);
    if (filled.isEmpty()) return false;
    const ScientificDataset *current = m_series.value(m_selectedSeries);
    const int index = current ? current->indexOf(m_selectedId) : -1;
    if (key == Qt::Key_Left || key == Qt::Key_Right) {
        if (!current || index < 0) return selectSample(filled.first(), m_series[filled.first()]->samples().first().id);
        const int next = std::clamp(index + (key == Qt::Key_Right ? 1 : -1), 0, current->count() - 1);
        return selectSample(m_selectedSeries, current->samples()[next].id);
    }
    if (key != Qt::Key_Up && key != Qt::Key_Down) return false;
    int position = filled.indexOf(m_selectedSeries);
    position = position < 0 ? 0 : std::clamp(position + (key == Qt::Key_Down ? 1 : -1), 0, int(filled.size()) - 1);
    const ScientificDataset *target = m_series[filled[position]];
    // Keep the reader at the same X: nearest sample of the target series.
    const double x = current && index >= 0 ? current->samples()[index].x : target->samples().first().x;
    int best = 0;
    for (int i = 1; i < target->count(); ++i)
        if (std::abs(target->samples()[i].x - x) < std::abs(target->samples()[best].x - x)) best = i;
    return selectSample(filled[position], target->samples()[best].id);
}

void ScientificPlotRenderer::setViewport(double xs, double xe, double ys, double ye)
{
    if (!std::isfinite(xs) || !std::isfinite(xe) || !std::isfinite(ys) || !std::isfinite(ye)
        || xs < 0 || xe > 1 || ys < 0 || ye > 1 || xe - xs < 1e-6 || ye - ys < 1e-6) return;
    if (m_xStart == xs && m_xEnd == xe && m_yStart == ys && m_yEnd == ye) return;
    m_xStart = xs; m_xEnd = xe; m_yStart = ys; m_yEnd = ye;
    emit viewportChanged(); update();
}
void ScientificPlotRenderer::pan(double dx, double dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    const double x = std::clamp(m_xStart + dx, 0.0, 1.0 - viewX());
    const double y = std::clamp(m_yStart + dy, 0.0, 1.0 - viewY());
    setViewport(x, x + viewX(), y, y + viewY());
}
void ScientificPlotRenderer::zoom(double factor, double xAnchor, double yAnchor)
{
    if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(xAnchor) || !std::isfinite(yAnchor)) return;
    const double nx = std::clamp(viewX() / factor, 1e-6, 1.0), ny = std::clamp(viewY() / factor, 1e-6, 1.0);
    const double xs = std::clamp(m_xStart + viewX() * xAnchor - nx * xAnchor, 0.0, 1.0 - nx);
    const double ys = std::clamp(m_yStart + viewY() * yAnchor - ny * yAnchor, 0.0, 1.0 - ny);
    setViewport(xs, xs + nx, ys, ys + ny);
}

QVector<int> ScientificPlotRenderer::painted(int series) const
{
    const ScientificDataset *d = m_series.value(series);
    if (!d || !problem().isEmpty()) return {};
    const double a = m_xAxis->unmap(m_xStart), b = m_xAxis->unmap(m_xEnd);
    const double lo = std::min(a, b), hi = std::max(a, b);
    const auto &samples = d->samples();
    if (d->kind() == ScientificDataset::Trace) {
        // One neighbour beyond each edge keeps the line running to the frame.
        const int first = std::max(0, d->lowerBound(lo) - 1);
        const int last = std::min(d->count(), d->upperBound(hi) + 1);
        if (last - first <= m_paintPointLimit) {
            QVector<int> all; all.reserve(last - first);
            for (int i = first; i < last; ++i) all.append(i);
            return all;
        }
        return ScientificSampling::traceEnvelope(samples, first, last, m_paintPointLimit / 5);
    }
    QVector<int> candidates;
    for (int i = 0; i < samples.size(); ++i)
        if (samples[i].x + samples[i].sigmaXHigh >= lo && samples[i].x - samples[i].sigmaXLow <= hi) candidates.append(i);
    return ScientificSampling::scatterStride(samples, candidates, m_paintPointLimit);
}
QVariantList ScientificPlotRenderer::paintedIndices(int series) const
{
    QVariantList out;
    for (int i : painted(series)) out.append(i);
    return out;
}
int ScientificPlotRenderer::omittedCount() const
{
    if (!problem().isEmpty()) return 0;
    int omitted = 0;
    for (const auto &d : m_series) {
        if (!d) continue;
        for (const ScientificSample &s : d->samples())
            if (!std::isfinite(m_xAxis->map(s.x)) || (!s.missingY && !std::isfinite(m_yAxis->map(s.y)))) ++omitted;
    }
    return omitted;
}

QVariantMap ScientificPlotRenderer::hitAt(double x, double y) const
{
    const QRectF plot = plotRect({width(), height()});
    if (!problem().isEmpty() || !plot.contains(x, y)) return {};
    const double radius = metric("hitRadius");
    double best = radius; QVariantMap hit;
    for (int s = 0; s < m_series.size(); ++s) {
        if (!m_series[s]) continue;
        const auto &samples = m_series[s]->samples();
        for (int i : painted(s)) {
            const ScientificSample &v = samples[i];
            if (v.missingY) continue;
            const QPointF c = point(v.x, v.y, plot);
            if (!std::isfinite(c.x()) || !std::isfinite(c.y())) continue;
            const double distance = std::hypot(c.x() - x, c.y() - y);
            if (distance <= best) {
                best = distance;
                hit = {{QStringLiteral("series"), s}, {QStringLiteral("id"), v.id}};
            }
        }
    }
    return hit;
}
QVariantMap ScientificPlotRenderer::projected(int series, const QString &id) const
{
    const ScientificDataset *d = m_series.value(series);
    const QRectF plot = plotRect({width(), height()});
    if (!d || !problem().isEmpty() || plot.isEmpty() || d->indexOf(id) < 0) return {};
    const ScientificSample &v = d->samples()[d->indexOf(id)];
    const double py = v.missingY ? plot.bottom() : point(v.x, v.y, plot).y();
    const QPointF c(point(v.x, v.missingY ? 1 : v.y, plot).x(), py);
    if (!std::isfinite(c.x()) || !std::isfinite(c.y())) return {};
    QVariantMap out{{QStringLiteral("x"), c.x()}, {QStringLiteral("y"), c.y()}};
    if (v.hasSigmaY) {
        out.insert(QStringLiteral("lowY"), point(v.x, v.y - v.sigmaYLow, plot).y());
        out.insert(QStringLiteral("highY"), point(v.x, v.y + v.sigmaYHigh, plot).y());
    }
    if (v.hasSigmaX) {
        out.insert(QStringLiteral("lowX"), point(v.x - v.sigmaXLow, v.missingY ? 1 : v.y, plot).x());
        out.insert(QStringLiteral("highX"), point(v.x + v.sigmaXHigh, v.missingY ? 1 : v.y, plot).x());
    }
    return out;
}
QVariantMap ScientificPlotRenderer::dataAt(double pixelX, double pixelY) const
{
    const QRectF plot = plotRect({width(), height()});
    if (!problem().isEmpty() || !std::isfinite(pixelX) || !std::isfinite(pixelY) || !plot.contains(pixelX, pixelY))
        return {};
    const double x = m_xAxis->unmap(m_xStart + viewX() * (pixelX - plot.left()) / plot.width());
    const double y = m_yAxis->unmap(m_yStart + viewY() * (plot.bottom() - pixelY) / plot.height());
    if (!std::isfinite(x) || !std::isfinite(y)) return {};
    QVariantMap out{{QStringLiteral("x"), x}, {QStringLiteral("y"), y},
                    {QStringLiteral("xUnit"), m_xAxis->unit()}, {QStringLiteral("yUnit"), m_yAxis->unit()}};
    if (m_surface && m_surface->count()) {
        const double z = m_surface->sampleAt(x, y, m_interpolation);
        out.insert(QStringLiteral("z"), std::isnan(z) ? QVariant() : QVariant(z));
        out.insert(QStringLiteral("zUnit"), m_surface->zUnit());
        out.insert(QStringLiteral("surfaceMissing"), std::isnan(z));
    }
    return out;
}
QVariantList ScientificPlotRenderer::ticks(const QString &axis) const
{
    QVariantList out;
    const ChartAxis *a = axis == QLatin1String("x") ? m_xAxis.data() : axis == QLatin1String("y") ? m_yAxis.data() : nullptr;
    if (!a || !axesDrawable()) return out;
    const double start = a == m_xAxis ? m_xStart : m_yStart, end = a == m_xAxis ? m_xEnd : m_yEnd;
    for (double v : ScientificSampling::ticks(a->kind() == ChartAxis::Logarithmic, a->unmap(start), a->unmap(end), 6))
        out.append(v);
    return out;
}

QString ScientificPlotRenderer::csvData() const
{
    QString out;
    for (int s = 0; s < m_series.size(); ++s) {
        if (!m_series[s]) continue;
        const QStringList lines = m_series[s]->csvData().split('\n', Qt::SkipEmptyParts);
        for (int i = 0; i < lines.size(); ++i) {
            if (i == 0 && !out.isEmpty()) continue; // one header for the long table
            out += (i == 0 ? QStringLiteral("series,") : QStringLiteral("\"%1\",").arg(s)) + lines[i] + '\n';
        }
    }
    return out;
}

QString ScientificPlotRenderer::textSummary() const
{
    using ScientificSampling::tickLabel;
    QStringList parts;
    if (!m_title.isEmpty()) parts << m_title + '.';
    const QString issue = problem();
    if (!issue.isEmpty()) return (parts << issue + '.').join(' ');
    auto axisText = [this](const ChartAxis *a, const QString &name, double s, double e) {
        return tr("%1 axis %2 from %3 to %4 %5.").arg(name, a->kind() == ChartAxis::Logarithmic ? tr("logarithmic") : tr("linear"),
            tickLabel(a->unmap(s)), tickLabel(a->unmap(e)), a->unit());
    };
    parts << axisText(m_xAxis, m_xLabel.isEmpty() ? QStringLiteral("X") : m_xLabel, m_xStart, m_xEnd)
          << axisText(m_yAxis, m_yLabel.isEmpty() ? QStringLiteral("Y") : m_yLabel, m_yStart, m_yEnd);
    for (int s = 0; s < m_series.size(); ++s) {
        const ScientificDataset *d = m_series[s];
        if (!d) continue;
        const QVariantMap e = d->extent();
        parts << tr("Series %1, %2: %3, %4 samples, %5 missing%6.").arg(s + 1).arg(d->label(),
            d->kind() == ScientificDataset::Trace ? tr("trace") : tr("scatter")).arg(d->count()).arg(d->missingCount())
            .arg(e.value(QStringLiteral("valid")).toBool()
                 ? tr(", Y from %1 to %2 %3 including uncertainty").arg(tickLabel(e.value(QStringLiteral("yMinimum")).toDouble()),
                       tickLabel(e.value(QStringLiteral("yMaximum")).toDouble()), d->yUnit()) : QString());
    }
    if (m_surface && m_surface->count())
        parts << tr("Surface %1: %2 by %3 nodes, Z from %4 to %5 %6, %7 missing, %8 interpolation.")
            .arg(m_surface->label()).arg(m_surface->rows()).arg(m_surface->columns())
            .arg(tickLabel(m_surface->zMinimum()), tickLabel(m_surface->zMaximum()), m_surface->zUnit())
            .arg(m_surface->missingCount())
            .arg(m_interpolation == ScientificGrid::Nearest ? tr("nearest") : tr("bilinear"));
    if (m_annotations)
        for (const ChartAnnotation &a : m_annotations->annotations())
            parts << tr("Note %1: %2.").arg(a.label.isEmpty() ? a.id : a.label,
                a.kind == ChartAnnotation::VerticalLine ? tr("X = %1").arg(tickLabel(a.x))
                : a.kind == ChartAnnotation::HorizontalLine ? tr("Y = %1").arg(tickLabel(a.y))
                : tr("X = %1, Y = %2").arg(tickLabel(a.x), tickLabel(a.y)));
    if (const int omitted = omittedCount())
        parts << tr("%1 samples lie outside the logarithmic domain and are not drawn.").arg(omitted);
    return parts.join(' ');
}

QString ScientificPlotRenderer::axisTitle(const ChartAxis *axis, const QString &label, const char *fallback) const
{
    const QString name = label.isEmpty() ? QString::fromLatin1(fallback) : label;
    return axis && !axis->unit().isEmpty() ? QStringLiteral("%1 (%2)").arg(name, axis->unit()) : name;
}

void ScientificPlotRenderer::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus();
    const QVariantMap hit = hitAt(event->position().x(), event->position().y());
    if (!hit.isEmpty()) selectSample(hit.value(QStringLiteral("series")).toInt(), hit.value(QStringLiteral("id")).toString());
    event->accept();
}
void ScientificPlotRenderer::keyPressEvent(QKeyEvent *event)
{
    if (navigateSelection(event->key())) event->accept();
    else QQuickPaintedItem::keyPressEvent(event);
}

} // namespace QindaTK
