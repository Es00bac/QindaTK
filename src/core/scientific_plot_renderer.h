// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "chart_annotation_model.h"
#include "chart_axis.h"
#include "scientific_dataset.h"
#include "scientific_grid.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QQmlListProperty>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A 2D scientific figure: any number of Trace/Scatter series with X and
// asymmetric Y error bars, an optional sampled surface drawn as a colour map,
// numeric annotations, calibrated linear/log axes and provenance export.
// AGENT-CONTRACT: the renderer never rescales data. Units of every series,
// the surface and every annotation must equal the axis units or the figure
// refuses to draw and says why (`problem`). ScientificPlot.qml supplies
// theme colours and metrics; ScientificFigureComposer calls drawFigure().
class ScientificPlotRenderer : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQmlListProperty<QindaTK::ScientificDataset> series READ seriesList NOTIFY seriesChanged)
    Q_PROPERTY(ScientificGrid *surface READ surface WRITE setSurface NOTIFY seriesChanged)
    Q_PROPERTY(QindaTK::ScientificGrid::Interpolation surfaceInterpolation READ surfaceInterpolation WRITE setSurfaceInterpolation NOTIFY styleChanged)
    Q_PROPERTY(ChartAnnotationModel *annotations READ annotations WRITE setAnnotations NOTIFY seriesChanged)
    Q_PROPERTY(ChartAxis *xAxis READ xAxis WRITE setXAxis NOTIFY axesChanged)
    Q_PROPERTY(ChartAxis *yAxis READ yAxis WRITE setYAxis NOTIFY axesChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_background NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor MEMBER m_grid NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor MEMBER m_text NOTIFY styleChanged)
    Q_PROPERTY(QColor missingColor MEMBER m_missing NOTIFY styleChanged)
    Q_PROPERTY(QColor selectionColor MEMBER m_selection NOTIFY styleChanged)
    Q_PROPERTY(QColor annotationColor MEMBER m_annotation NOTIFY styleChanged)
    Q_PROPERTY(QList<QColor> seriesColors MEMBER m_seriesColors NOTIFY styleChanged)
    Q_PROPERTY(QList<QColor> surfaceColors MEMBER m_surfaceColors NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap metrics MEMBER m_metrics NOTIFY styleChanged)
    Q_PROPERTY(QString title MEMBER m_title NOTIFY styleChanged)
    Q_PROPERTY(QString xLabel MEMBER m_xLabel NOTIFY styleChanged)
    Q_PROPERTY(QString yLabel MEMBER m_yLabel NOTIFY styleChanged)
    Q_PROPERTY(int paintPointLimit READ paintPointLimit WRITE setPaintPointLimit NOTIFY styleChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY seriesChanged)
    // The textual alternative (see textSummary()); re-read on any change.
    Q_PROPERTY(QString summary READ textSummary NOTIFY summaryChanged)
    Q_PROPERTY(int selectedSeries READ selectedSeries NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedSampleId READ selectedSampleId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedSample READ selectedSample NOTIFY selectionChanged)
    Q_PROPERTY(double viewX READ viewX NOTIFY viewportChanged)
    Q_PROPERTY(double viewY READ viewY NOTIFY viewportChanged)
public:
    enum Mode { ErrorBars, Band };
    Q_ENUM(Mode)
    explicit ScientificPlotRenderer(QQuickItem *parent = nullptr);
    QQmlListProperty<ScientificDataset> seriesList();
    const QList<QPointer<ScientificDataset>> &series() const { return m_series; }
    void appendSeries(ScientificDataset *dataset);
    void clearSeries();
    ScientificGrid *surface() const { return m_surface; }
    void setSurface(ScientificGrid *value);
    ScientificGrid::Interpolation surfaceInterpolation() const { return m_interpolation; }
    void setSurfaceInterpolation(ScientificGrid::Interpolation value);
    ChartAnnotationModel *annotations() const { return m_annotations; }
    void setAnnotations(ChartAnnotationModel *value);
    ChartAxis *xAxis() const { return m_xAxis; }
    void setXAxis(ChartAxis *value);
    ChartAxis *yAxis() const { return m_yAxis; }
    void setYAxis(ChartAxis *value);
    Mode mode() const { return m_mode; }
    void setMode(Mode value);
    int paintPointLimit() const { return m_paintPointLimit; }
    void setPaintPointLimit(int value);
    QString problem() const;
    int selectedSeries() const { return m_selectedSeries; }
    QString selectedSampleId() const { return m_selectedId; }
    QVariantMap selectedSample() const;
    double viewX() const { return m_xEnd - m_xStart; }
    double viewY() const { return m_yEnd - m_yStart; }
    Q_INVOKABLE void setViewport(double xStart, double xEnd, double yStart, double yEnd);
    Q_INVOKABLE void pan(double xFraction, double yFraction);
    Q_INVOKABLE void zoom(double factor, double xAnchor = 0.5, double yAnchor = 0.5);
    Q_INVOKABLE bool selectSample(int series, const QString &id);
    Q_INVOKABLE bool navigateSelection(int key);
    // {series, id} of the nearest painted sample within hitRadius, or {}.
    Q_INVOKABLE QVariantMap hitAt(double x, double y) const;
    Q_INVOKABLE QVariantMap projected(int series, const QString &id) const;
    // Data coordinates and units under a pixel, plus the surface value there.
    Q_INVOKABLE QVariantMap dataAt(double pixelX, double pixelY) const;
    // Indices painted for one series at the current size and viewport.
    Q_INVOKABLE QVariantList paintedIndices(int series) const;
    // Samples that cannot be projected (non-positive on a log axis).
    Q_INVOKABLE int omittedCount() const;
    Q_INVOKABLE QVariantList ticks(const QString &axis) const;
    Q_INVOKABLE QString csvData() const;
    Q_INVOKABLE QString textSummary() const;
    Q_INVOKABLE QString metadataJson(int width, int height) const;
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path, const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path, const QSize &size) const;
    void drawFigure(QPainter *painter, const QSizeF &size) const;
    QJsonObject figureMetadata(const QSize &size) const;
    void paint(QPainter *painter) override;
signals:
    void seriesChanged();
    void axesChanged();
    void styleChanged();
    void selectionChanged();
    void viewportChanged();
    void sampleSelected(int series, const QString &id);
    void summaryChanged();
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    static void appendSeriesTo(QQmlListProperty<ScientificDataset> *list, ScientificDataset *value);
    static qsizetype countSeries(QQmlListProperty<ScientificDataset> *list);
    static ScientificDataset *seriesAt(QQmlListProperty<ScientificDataset> *list, qsizetype index);
    static void clearSeriesOf(QQmlListProperty<ScientificDataset> *list);
    void watch(QObject *object);
    void dataChangedSlot();
    bool axesDrawable() const;
    QRectF plotRect(const QSizeF &size) const;
    // Pixel position of a data point; NaN coordinates when not projectable.
    QPointF point(double x, double y, const QRectF &plot) const;
    QVector<int> painted(int series) const;
    QString axisTitle(const ChartAxis *axis, const QString &label, const char *fallback) const;
    QColor seriesColor(int series) const;
    QColor surfaceColor(double z) const;
    double metric(const char *name) const { return m_metrics.value(QLatin1String(name)).toDouble(); }
    void drawSurface(QPainter *p, const QRectF &plot) const;
    void drawAxes(QPainter *p, const QRectF &plot, const QSizeF &size) const;
    void drawSeries(QPainter *p, const QRectF &plot, int series) const;
    void drawAnnotations(QPainter *p, const QRectF &plot) const;
    void drawLegend(QPainter *p, const QRectF &plot) const;
    QList<QPointer<ScientificDataset>> m_series;
    QPointer<ScientificGrid> m_surface;
    QPointer<ChartAnnotationModel> m_annotations;
    QPointer<ChartAxis> m_xAxis, m_yAxis;
    ScientificGrid::Interpolation m_interpolation = ScientificGrid::Bilinear;
    Mode m_mode = ErrorBars;
    QColor m_background, m_grid, m_text, m_missing, m_selection, m_annotation;
    QList<QColor> m_seriesColors, m_surfaceColors;
    QVariantMap m_metrics;
    QString m_title, m_xLabel, m_yLabel;
    int m_paintPointLimit = 2048;
    int m_selectedSeries = -1;
    QString m_selectedId;
    double m_xStart = 0, m_xEnd = 1, m_yStart = 0, m_yEnd = 1;
};

} // namespace QindaTK
