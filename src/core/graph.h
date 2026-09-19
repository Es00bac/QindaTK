// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QQmlListProperty>
#include <QQuickItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

// The ramp type is a Q_PROPERTY of GraphSeries; moc needs it fully defined.
#include "graph_mesh.h"
#include "theme_ramp.h"

namespace QindaTK {

class Graph;

// AGENT-CONTRACT: one plotted history inside a Graph. The samples live in a
// fixed-capacity ring buffer owned here; the owning Graph sets the capacity
// so every series on one graph shares an x axis. Appending is O(1) and never
// reallocates, which is what lets a system monitor keep thirty of these
// updating without a visible cost.
//
// AGENT-GUARD: `at(0)` is the OLDEST retained sample and `at(count()-1)` the
// newest. Drawing code depends on that order; reading m_values directly by
// index gives ring order, which is not the same thing.
class GraphSeries : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(QindaTK::ThemeRamp *ramp READ ramp WRITE setRamp NOTIFY styleChanged)
    Q_PROPERTY(bool fill READ fill WRITE setFill NOTIFY styleChanged)
    Q_PROPERTY(qreal fillOpacity READ fillOpacity WRITE setFillOpacity NOTIFY styleChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY styleChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY styleChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY styleChanged)
    Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY samplesChanged)
    Q_PROPERTY(int count READ count NOTIFY samplesChanged)
    Q_PROPERTY(qreal last READ last NOTIFY samplesChanged)
    Q_PROPERTY(qreal peak READ peak NOTIFY samplesChanged)
    // Read-only: the owning Graph sets this so every series on one
    // graph shares an x axis.
    Q_PROPERTY(int capacity READ capacity NOTIFY capacityChanged)

public:
    explicit GraphSeries(QObject *parent = nullptr);

    [[nodiscard]] QColor color() const { return m_color; }
    void setColor(const QColor &color);
    [[nodiscard]] ThemeRamp *ramp() const { return m_ramp; }
    void setRamp(ThemeRamp *ramp);
    [[nodiscard]] bool fill() const { return m_fill; }
    void setFill(bool fill);
    [[nodiscard]] qreal fillOpacity() const { return m_fillOpacity; }
    void setFillOpacity(qreal opacity);
    [[nodiscard]] qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal width);
    [[nodiscard]] bool visible() const { return m_visible; }
    void setVisible(bool visible);
    [[nodiscard]] QString label() const { return m_label; }
    void setLabel(const QString &label);

    // Appends one reading, discarding the oldest when full.
    Q_INVOKABLE void append(qreal value);
    // Forgets every retained sample.
    Q_INVOKABLE void clear();
    // Oldest-first; `at()` outside [0, count) returns 0.
    Q_INVOKABLE [[nodiscard]] qreal at(int index) const;

    [[nodiscard]] QVariantList values() const;
    void setValues(const QVariantList &values);
    [[nodiscard]] int count() const { return m_count; }
    [[nodiscard]] qreal last() const;
    [[nodiscard]] qreal peak() const;

    // Called by the owning Graph. Retains the newest samples that fit.
    void setCapacity(int capacity);
    [[nodiscard]] int capacity() const { return m_capacity; }

signals:
    void styleChanged();
    void samplesChanged();
    void capacityChanged();

private:
    QColor m_color;
    ThemeRamp *m_ramp = nullptr;
    QString m_label;
    std::vector<float> m_values;
    int m_capacity = 120;
    int m_head = 0;
    int m_count = 0;
    qreal m_fillOpacity = 0.45;
    qreal m_lineWidth = 1.5;
    bool m_fill = true;
    bool m_visible = true;
};

// AGENT-CONTRACT: a time-series plot drawn on the scene graph (no raster
// repaint, no QPainter), so many instances stay cheap. Samples enter at the
// right and scroll left. Series are the default property:
//
//     Tk.Graph { capacity: 120; maxValue: 100
//         Tk.GraphSeries { color: Tk.Theme.color.accent } }
//
// AGENT-NOTE: a series' fill is drawn with per-vertex colour taken from its
// `ramp` at that sample's own magnitude, which is why a busy column reads
// hot and a quiet one does not -- the same trick btop plays with character
// colour, done with vertices instead.
class Graph : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_CLASSINFO("DefaultProperty", "series")
    Q_PROPERTY(QQmlListProperty<QindaTK::GraphSeries> series READ seriesList NOTIFY seriesChanged)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged)
    Q_PROPERTY(qreal minValue READ minValue WRITE setMinValue NOTIFY scaleChanged)
    Q_PROPERTY(qreal maxValue READ maxValue WRITE setMaxValue NOTIFY scaleChanged)
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale NOTIFY scaleChanged)
    Q_PROPERTY(qreal autoScaleFloor READ autoScaleFloor WRITE setAutoScaleFloor NOTIFY scaleChanged)
    Q_PROPERTY(qreal headroom READ headroom WRITE setHeadroom NOTIFY scaleChanged)
    Q_PROPERTY(qreal effectiveMax READ effectiveMax NOTIFY scaleChanged)
    Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored NOTIFY styleChanged)
    Q_PROPERTY(int gridRows READ gridRows WRITE setGridRows NOTIFY styleChanged)
    Q_PROPERTY(int gridColumns READ gridColumns WRITE setGridColumns NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
    Q_PROPERTY(QColor baselineColor READ baselineColor WRITE setBaselineColor NOTIFY styleChanged)

public:
    explicit Graph(QQuickItem *parent = nullptr);
    ~Graph() override;

    [[nodiscard]] QQmlListProperty<GraphSeries> seriesList();
    [[nodiscard]] const QList<GraphSeries *> &seriesItems() const { return m_series; }
    Q_INVOKABLE [[nodiscard]] QindaTK::GraphSeries *seriesAt(int index) const;
    Q_INVOKABLE [[nodiscard]] int seriesCount() const { return int(m_series.size()); }

    // Appends one reading to series `index`.
    Q_INVOKABLE void appendTo(int index, qreal value);
    // Appends to the first series; the single-series convenience.
    Q_INVOKABLE void append(qreal value);
    // Appends one reading to each series in order, keeping them aligned on
    // the x axis. Extra values are ignored; missing ones repeat the last.
    Q_INVOKABLE void appendRow(const QVariantList &values);
    Q_INVOKABLE void clear();

    [[nodiscard]] int capacity() const { return m_capacity; }
    void setCapacity(int capacity);
    [[nodiscard]] qreal minValue() const { return m_minValue; }
    void setMinValue(qreal value);
    [[nodiscard]] qreal maxValue() const { return m_maxValue; }
    void setMaxValue(qreal value);
    [[nodiscard]] bool autoScale() const { return m_autoScale; }
    void setAutoScale(bool autoScale);
    [[nodiscard]] qreal autoScaleFloor() const { return m_autoScaleFloor; }
    void setAutoScaleFloor(qreal value);
    [[nodiscard]] qreal headroom() const { return m_headroom; }
    void setHeadroom(qreal headroom);
    // The upper bound actually plotted: maxValue, or the auto-scaled peak.
    [[nodiscard]] qreal effectiveMax() const;
    [[nodiscard]] bool mirrored() const { return m_mirrored; }
    void setMirrored(bool mirrored);
    [[nodiscard]] int gridRows() const { return m_gridRows; }
    void setGridRows(int rows);
    [[nodiscard]] int gridColumns() const { return m_gridColumns; }
    void setGridColumns(int columns);
    [[nodiscard]] QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &color);
    [[nodiscard]] QColor baselineColor() const { return m_baselineColor; }
    void setBaselineColor(const QColor &color);

signals:
    void seriesChanged();
    void capacityChanged();
    void scaleChanged();
    void styleChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    // Software-adaptation path; see graph_nodes.cpp.
    QSGNode *updateRasterNode(QSGNode *oldNode, const GraphMesh::Plot &plot);
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    static void appendSeries(QQmlListProperty<GraphSeries> *list, GraphSeries *series);
    static qsizetype seriesCountFn(QQmlListProperty<GraphSeries> *list);
    static GraphSeries *seriesAtFn(QQmlListProperty<GraphSeries> *list, qsizetype index);
    static void clearSeries(QQmlListProperty<GraphSeries> *list);
    void adopt(GraphSeries *series);
    void markDirty();

    QList<GraphSeries *> m_series;
    QColor m_gridColor;
    QColor m_baselineColor;
    qreal m_minValue = 0;
    qreal m_maxValue = 100;
    qreal m_autoScaleFloor = 1;
    qreal m_headroom = 1.15;
    int m_capacity = 120;
    int m_gridRows = 0;
    int m_gridColumns = 0;
    bool m_autoScale = false;
    bool m_rasterNode = false;
    bool m_mirrored = false;
};

} // namespace QindaTK
