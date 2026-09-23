// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "chart_axis.h"
#include "chart_histogram.h"
#include "chart_data.h"
#include "chart_dataset.h"
#include "chart_table_model.h"
#include "chart_annotation_model.h"
#include "chart_histogram_selection.h"

#include <QColor>
#include <QImage>
#include <QKeyEvent>
#include <QQuickPaintedItem>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class ChartRenderer : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_CLASSINFO("DefaultProperty", "datasets")
    Q_PROPERTY(ChartAxis *xAxis READ xAxis WRITE setXAxis NOTIFY axesChanged)
    Q_PROPERTY(ChartAxis *yAxis READ yAxis WRITE setYAxis NOTIFY axesChanged)
    Q_PROPERTY(QQmlListProperty<QindaTK::ChartDataset> datasets READ datasets NOTIFY datasetsChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
    Q_PROPERTY(QColor axisColor READ axisColor WRITE setAxisColor NOTIFY styleChanged)
    Q_PROPERTY(QColor defaultSeriesColor READ defaultSeriesColor WRITE setDefaultSeriesColor NOTIFY styleChanged)
    Q_PROPERTY(QColor histogramColor READ histogramColor WRITE setHistogramColor NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY styleChanged)
    Q_PROPERTY(int paintPointLimit READ paintPointLimit WRITE setPaintPointLimit NOTIFY paintPointLimitChanged)
    Q_PROPERTY(QString selectedSeriesId READ selectedSeriesId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPointId READ selectedPointId NOTIFY selectionChanged)
    Q_PROPERTY(double viewX READ viewX NOTIFY viewportChanged)
    Q_PROPERTY(double viewY READ viewY NOTIFY viewportChanged)
    Q_PROPERTY(ChartTableModel *tableModel READ tableModel CONSTANT)
    Q_PROPERTY(QString histogramError READ histogramError NOTIFY histogramChanged)
    Q_PROPERTY(QString histogramChannel READ histogramChannel NOTIFY histogramChanged)
    Q_PROPERTY(QString histogramUnit READ histogramUnit NOTIFY histogramChanged)
    Q_PROPERTY(ChartAnnotationModel *annotationModel READ annotationModel WRITE setAnnotationModel NOTIFY annotationsChanged)
    Q_PROPERTY(QString annotationError READ annotationError NOTIFY annotationsChanged)
    Q_PROPERTY(QString selectedAnnotationId READ selectedAnnotationId NOTIFY annotationSelectionChanged)
    Q_PROPERTY(ChartHistogramSelection *histogramSelection READ histogramSelection CONSTANT)
public:
    explicit ChartRenderer(QQuickItem *parent = nullptr);
    ChartAxis *xAxis() const { return m_xAxis; }
    void setXAxis(ChartAxis *axis);
    ChartAxis *yAxis() const { return m_yAxis; }
    void setYAxis(ChartAxis *axis);
    QQmlListProperty<ChartDataset> datasets();
    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &v);
    QColor gridColor() const { return m_grid; }
    void setGridColor(const QColor &v);
    QColor axisColor() const { return m_axis; }
    void setAxisColor(const QColor &v);
    QColor defaultSeriesColor() const { return m_defaultSeriesColor; }
    void setDefaultSeriesColor(const QColor &v);
    QColor histogramColor() const { return m_histogramColor; }
    void setHistogramColor(const QColor &v);
    QVariantMap metrics() const { return m_metrics; }
    void setMetrics(const QVariantMap &v);
    int paintPointLimit() const { return m_paintPointLimit; }
    void setPaintPointLimit(int v);
    QString selectedSeriesId() const { return m_selectedSeriesId; }
    QString selectedPointId() const { return m_selectedPointId; }
    double viewX() const { return m_xEnd - m_xStart; }
    double viewY() const { return m_yEnd - m_yStart; }
    Q_INVOKABLE void setViewport(double xStart, double xEnd, double yStart, double yEnd);
    Q_INVOKABLE void pan(double xFraction, double yFraction);
    Q_INVOKABLE void zoom(double factor, double xAnchor = 0.5, double yAnchor = 0.5);
    Q_INVOKABLE QString csv() const;
    Q_INVOKABLE QVariantMap selectedPoint() const;
    Q_INVOKABLE bool navigateSelection(int key);
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path, const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path, const QSize &size) const;
    ChartTableModel *tableModel() { return &m_tableModel; }
    QString histogramError() const { return m_histogram.error; }
    QString histogramChannel() const { return m_histogram.channel; }
    QString histogramUnit() const { return m_histogram.unit; }
    ChartAnnotationModel *annotationModel() const { return m_annotationModel; }
    void setAnnotationModel(ChartAnnotationModel *model);
    QString annotationError() const;
    QString selectedAnnotationId() const { return m_selectedAnnotationId; }
    ChartHistogramSelection *histogramSelection() { return &m_histogramSelection; }
    Q_INVOKABLE QVariantMap annotationAt(double x, double y) const;
    Q_INVOKABLE bool selectAnnotationAt(double x, double y);
    Q_INVOKABLE QString annotationsCsv() const;
    Q_INVOKABLE bool selectHistogramBins(int first, int last);
    Q_INVOKABLE bool setHistogram(const QVariantList &edges, const QVariantList &counts,
                                  int normalization = ChartHistogram::Count,
                                  const QString &channel = {}, const QString &unit = {});
    void paint(QPainter *painter) override;
signals:
    void axesChanged();
    void datasetsChanged();
    void styleChanged();
    void paintPointLimitChanged();
    void selectionChanged();
    void pointSelected(const QString &seriesId, const QString &pointId);
    void viewportChanged();
    void exportImageReady(const QImage &image);
    void histogramChanged();
    void annotationsChanged();
    void annotationSelectionChanged();
    void annotationSelected(const QString &id);
    void histogramRangeSelected(ChartHistogramSelection *range);
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    static void appendDataset(QQmlListProperty<ChartDataset> *, ChartDataset *);
    static qsizetype datasetCount(QQmlListProperty<ChartDataset> *);
    static ChartDataset *datasetAt(QQmlListProperty<ChartDataset> *, qsizetype);
    static void clearDatasets(QQmlListProperty<ChartDataset> *);
    void connectDataset(ChartDataset *dataset);
    void syncViewportFromAxes();
    double annotationX(const ChartAnnotation &annotation) const;
    double annotationY(const ChartAnnotation &annotation) const;
    int histogramBinAt(const QPointF &position) const;
    void drawAnnotations(QPainter *painter, const QRectF &rect) const;
    ChartPlot plot() const;
    QRectF plotRect() const;
    void draw(QPainter *painter, const QSizeF &size) const;
    void rebuildHitIndex() const;
    qreal metric(const char *name) const;
    QVector<ChartSeries *> seriesPointers() const;
    QList<ChartDataset *> m_datasets;
    QPointer<ChartAxis> m_xAxis;
    QPointer<ChartAxis> m_yAxis;
    QColor m_background, m_grid, m_axis, m_histogramColor, m_defaultSeriesColor;
    QVariantMap m_metrics;
    mutable ChartHitIndex m_hitIndex;
    ChartTableModel m_tableModel;
    ChartHistogramResult m_histogram;
    QPointer<ChartAnnotationModel> m_annotationModel;
    ChartHistogramSelection m_histogramSelection;
    QString m_selectedAnnotationId;
    mutable bool m_hitDirty = true;
    QString m_selectedSeriesId, m_selectedPointId;
    double m_xStart = 0, m_xEnd = 1, m_yStart = 0, m_yEnd = 1;
    QPointF m_dragStart;
    bool m_dragging = false;
    int m_histogramDragStart = -1;
    int m_paintPointLimit = 2000;
};

} // namespace QindaTK
