// SPDX-License-Identifier: LGPL-3.0-or-later
#include "graph.h"

#include "theme_ramp.h"


#include <algorithm>
#include <cmath>

namespace QindaTK {

// ---- GraphSeries ---------------------------------------------------------

GraphSeries::GraphSeries(QObject *parent)
    : QObject(parent)
    , m_color(0x22, 0xd3, 0xee)
{
    m_values.assign(std::size_t(m_capacity), 0.0f);
}

void GraphSeries::setColor(const QColor &color)
{
    if (m_color != color) {
        m_color = color;
        emit styleChanged();
    }
}

void GraphSeries::setRamp(ThemeRamp *ramp)
{
    if (m_ramp == ramp) {
        return;
    }
    if (m_ramp != nullptr) {
        disconnect(m_ramp, &ThemeRamp::changed, this, &GraphSeries::styleChanged);
    }
    m_ramp = ramp;
    if (m_ramp != nullptr) {
        connect(m_ramp, &ThemeRamp::changed, this, &GraphSeries::styleChanged);
    }
    emit styleChanged();
}

void GraphSeries::setFill(bool fill)
{
    if (m_fill != fill) {
        m_fill = fill;
        emit styleChanged();
    }
}

void GraphSeries::setFillOpacity(qreal opacity)
{
    const qreal clamped = std::clamp(opacity, qreal(0), qreal(1));
    if (!qFuzzyCompare(m_fillOpacity, clamped)) {
        m_fillOpacity = clamped;
        emit styleChanged();
    }
}

void GraphSeries::setLineWidth(qreal width)
{
    const qreal clamped = std::max(qreal(0), width);
    if (!qFuzzyCompare(m_lineWidth, clamped)) {
        m_lineWidth = clamped;
        emit styleChanged();
    }
}

void GraphSeries::setVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        emit styleChanged();
    }
}

void GraphSeries::setLabel(const QString &label)
{
    if (m_label != label) {
        m_label = label;
        emit styleChanged();
    }
}

void GraphSeries::setCapacity(int capacity)
{
    const int wanted = std::max(2, capacity);
    if (wanted == m_capacity) {
        return;
    }
    // Keep the newest samples that still fit, oldest-first.
    const int keep = std::min(m_count, wanted);
    std::vector<float> rebuilt(std::size_t(wanted), 0.0f);
    for (int i = 0; i < keep; ++i) {
        rebuilt[std::size_t(i)] = float(at(m_count - keep + i));
    }
    m_values = std::move(rebuilt);
    m_capacity = wanted;
    m_count = keep;
    m_head = keep % wanted;
    emit samplesChanged();
}

void GraphSeries::append(qreal value)
{
    const float sample = std::isfinite(value) ? float(value) : 0.0f;
    m_values[std::size_t(m_head)] = sample;
    m_head = (m_head + 1) % m_capacity;
    if (m_count < m_capacity) {
        ++m_count;
    }
    emit samplesChanged();
}

void GraphSeries::clear()
{
    if (m_count == 0) {
        return;
    }
    std::fill(m_values.begin(), m_values.end(), 0.0f);
    m_count = 0;
    m_head = 0;
    emit samplesChanged();
}

qreal GraphSeries::at(int index) const
{
    if (index < 0 || index >= m_count) {
        return 0;
    }
    // The oldest retained sample sits `m_count` slots behind the write head.
    const int slot = (m_head - m_count + index + m_capacity * 2) % m_capacity;
    return m_values[std::size_t(slot)];
}

QVariantList GraphSeries::values() const
{
    QVariantList list;
    list.reserve(m_count);
    for (int i = 0; i < m_count; ++i) {
        list.append(at(i));
    }
    return list;
}

void GraphSeries::setValues(const QVariantList &values)
{
    std::fill(m_values.begin(), m_values.end(), 0.0f);
    const int take = int(std::min<qsizetype>(values.size(), m_capacity));
    const qsizetype skip = values.size() - take;
    for (int i = 0; i < take; ++i) {
        const qreal v = values.at(skip + i).toDouble();
        m_values[std::size_t(i)] = std::isfinite(v) ? float(v) : 0.0f;
    }
    m_count = take;
    m_head = take % m_capacity;
    emit samplesChanged();
}

qreal GraphSeries::last() const
{
    return m_count > 0 ? at(m_count - 1) : 0;
}

qreal GraphSeries::peak() const
{
    qreal peak = 0;
    for (int i = 0; i < m_count; ++i) {
        peak = std::max(peak, at(i));
    }
    return peak;
}

// ---- Graph ---------------------------------------------------------------

Graph::Graph(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    // AGENT-NOTE: a plot has no content to measure, but Flex/Grid/Box size
    // their children from the implicit size, so a Graph without one collapses
    // to nothing inside a Panel. These are the smallest sizes at which a
    // 120-sample trace still reads; callers override them constantly.
    setImplicitSize(160, 48);
}

Graph::~Graph() = default;

QQmlListProperty<GraphSeries> Graph::seriesList()
{
    return {this, this, &Graph::appendSeries, &Graph::seriesCountFn, &Graph::seriesAtFn,
            &Graph::clearSeries};
}

void Graph::appendSeries(QQmlListProperty<GraphSeries> *list, GraphSeries *series)
{
    auto *graph = static_cast<Graph *>(list->object);
    if (graph != nullptr && series != nullptr) {
        graph->adopt(series);
    }
}

qsizetype Graph::seriesCountFn(QQmlListProperty<GraphSeries> *list)
{
    return static_cast<Graph *>(list->object)->m_series.size();
}

GraphSeries *Graph::seriesAtFn(QQmlListProperty<GraphSeries> *list, qsizetype index)
{
    return static_cast<Graph *>(list->object)->m_series.value(index, nullptr);
}

void Graph::clearSeries(QQmlListProperty<GraphSeries> *list)
{
    auto *graph = static_cast<Graph *>(list->object);
    for (GraphSeries *series : std::as_const(graph->m_series)) {
        series->disconnect(graph);
    }
    graph->m_series.clear();
    emit graph->seriesChanged();
    graph->markDirty();
}

void Graph::adopt(GraphSeries *series)
{
    series->setCapacity(m_capacity);
    connect(series, &GraphSeries::samplesChanged, this, &Graph::markDirty);
    connect(series, &GraphSeries::styleChanged, this, &Graph::markDirty);
    // An auto-scaled graph republishes effectiveMax whenever a sample lands.
    connect(series, &GraphSeries::samplesChanged, this, [this] {
        if (m_autoScale) {
            emit scaleChanged();
        }
    });
    m_series.append(series);
    emit seriesChanged();
    markDirty();
}

void Graph::markDirty()
{
    update();
}

GraphSeries *Graph::seriesAt(int index) const
{
    return m_series.value(index, nullptr);
}

void Graph::appendTo(int index, qreal value)
{
    if (GraphSeries *series = seriesAt(index)) {
        series->append(value);
    }
}

void Graph::append(qreal value)
{
    appendTo(0, value);
}

void Graph::appendRow(const QVariantList &values)
{
    for (int i = 0; i < m_series.size(); ++i) {
        if (i < values.size()) {
            m_series.at(i)->append(values.at(i).toDouble());
        } else {
            m_series.at(i)->append(m_series.at(i)->last());
        }
    }
}

void Graph::clear()
{
    for (GraphSeries *series : std::as_const(m_series)) {
        series->clear();
    }
}

void Graph::setCapacity(int capacity)
{
    const int wanted = std::max(2, capacity);
    if (wanted == m_capacity) {
        return;
    }
    m_capacity = wanted;
    for (GraphSeries *series : std::as_const(m_series)) {
        series->setCapacity(wanted);
    }
    emit capacityChanged();
    markDirty();
}

void Graph::setMinValue(qreal value)
{
    if (!qFuzzyCompare(m_minValue, value)) {
        m_minValue = value;
        emit scaleChanged();
        markDirty();
    }
}

void Graph::setMaxValue(qreal value)
{
    if (!qFuzzyCompare(m_maxValue, value)) {
        m_maxValue = value;
        emit scaleChanged();
        markDirty();
    }
}

void Graph::setAutoScale(bool autoScale)
{
    if (m_autoScale != autoScale) {
        m_autoScale = autoScale;
        emit scaleChanged();
        markDirty();
    }
}

void Graph::setAutoScaleFloor(qreal value)
{
    if (!qFuzzyCompare(m_autoScaleFloor, value)) {
        m_autoScaleFloor = value;
        emit scaleChanged();
        markDirty();
    }
}

void Graph::setHeadroom(qreal headroom)
{
    const qreal clamped = std::max(qreal(1), headroom);
    if (!qFuzzyCompare(m_headroom, clamped)) {
        m_headroom = clamped;
        emit scaleChanged();
        markDirty();
    }
}

qreal Graph::effectiveMax() const
{
    if (!m_autoScale) {
        return m_maxValue;
    }
    qreal peak = 0;
    for (const GraphSeries *series : m_series) {
        if (series->visible()) {
            peak = std::max(peak, series->peak());
        }
    }
    return std::max(m_autoScaleFloor, peak * m_headroom);
}

void Graph::setMirrored(bool mirrored)
{
    if (m_mirrored != mirrored) {
        m_mirrored = mirrored;
        emit styleChanged();
        markDirty();
    }
}

void Graph::setGridRows(int rows)
{
    const int wanted = std::max(0, rows);
    if (m_gridRows != wanted) {
        m_gridRows = wanted;
        emit styleChanged();
        markDirty();
    }
}

void Graph::setGridColumns(int columns)
{
    const int wanted = std::max(0, columns);
    if (m_gridColumns != wanted) {
        m_gridColumns = wanted;
        emit styleChanged();
        markDirty();
    }
}

void Graph::setGridColor(const QColor &color)
{
    if (m_gridColor != color) {
        m_gridColor = color;
        emit styleChanged();
        markDirty();
    }
}

void Graph::setBaselineColor(const QColor &color)
{
    if (m_baselineColor != color) {
        m_baselineColor = color;
        emit styleChanged();
        markDirty();
    }
}

void Graph::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

} // namespace QindaTK
