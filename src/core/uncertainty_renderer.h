// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "chart_axis.h"
#include "uncertainty_model.h"
#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class UncertaintyRenderer : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(UncertaintyModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(ChartAxis *xAxis READ xAxis WRITE setXAxis NOTIFY axesChanged)
    Q_PROPERTY(ChartAxis *yAxis READ yAxis WRITE setYAxis NOTIFY axesChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY styleChanged)
    Q_PROPERTY(QColor bandColor READ bandColor WRITE setBandColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QColor missingColor READ missingColor WRITE setMissingColor NOTIFY styleChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY styleChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY styleChanged)
    Q_PROPERTY(int paintPointLimit READ paintPointLimit WRITE setPaintPointLimit NOTIFY styleChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedInterval READ selectedInterval NOTIFY selectionChanged)
    Q_PROPERTY(double viewX READ viewX NOTIFY viewportChanged)
    Q_PROPERTY(double viewY READ viewY NOTIFY viewportChanged)
public:
    enum Mode { ErrorBars, Band };
    Q_ENUM(Mode)
    explicit UncertaintyRenderer(QQuickItem *parent=nullptr);
    UncertaintyModel *model() const { return m_model; }
    void setModel(UncertaintyModel *value);
    ChartAxis *xAxis() const { return m_xAxis; }
    void setXAxis(ChartAxis *value);
    ChartAxis *yAxis() const { return m_yAxis; }
    void setYAxis(ChartAxis *value);
    Mode mode() const { return m_mode; }
    void setMode(Mode value);
#define COLOR_ACCESS(Getter,Setter,Field) QColor Getter() const { return m_##Field; } void set##Setter(const QColor &value);
    COLOR_ACCESS(backgroundColor,BackgroundColor,background)
    COLOR_ACCESS(gridColor,GridColor,grid)
    COLOR_ACCESS(lineColor,LineColor,line)
    COLOR_ACCESS(bandColor,BandColor,band)
    COLOR_ACCESS(textColor,TextColor,text)
    COLOR_ACCESS(missingColor,MissingColor,missing)
    COLOR_ACCESS(selectionColor,SelectionColor,selection)
#undef COLOR_ACCESS
    QVariantMap metrics() const { return m_metrics; }
    void setMetrics(const QVariantMap &value);
    QString title() const { return m_title; }
    void setTitle(const QString &value);
    int paintPointLimit() const { return m_paintPointLimit; }
    void setPaintPointLimit(int value);
    QString selectedId() const { return m_selectedId; }
    QVariantMap selectedInterval() const;
    double viewX() const { return m_xEnd-m_xStart; }
    double viewY() const { return m_yEnd-m_yStart; }
    Q_INVOKABLE void setViewport(double xStart,double xEnd,double yStart,double yEnd);
    Q_INVOKABLE void pan(double xFraction,double yFraction);
    Q_INVOKABLE void zoom(double factor,double xAnchor=0.5,double yAnchor=0.5);
    Q_INVOKABLE bool selectId(const QString &id);
    Q_INVOKABLE bool navigateSelection(int key);
    Q_INVOKABLE QString hitAt(double x,double y) const;
    Q_INVOKABLE QVariantMap projected(const QString &id) const;
    Q_INVOKABLE QVariantMap dataAt(double pixelX,double pixelY) const;
    Q_INVOKABLE int visiblePaintCount() const;
    Q_INVOKABLE QString csvData() const;
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path,const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path,const QSize &size) const;
    void paint(QPainter *painter) override;
signals:
    void modelChanged();
    void axesChanged();
    void modeChanged();
    void styleChanged();
    void selectionChanged();
    void intervalSelected(const QString &id);
    void viewportChanged();
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    QRectF plotRect(const QSizeF &size) const;
    bool canDraw() const;
    QPointF point(double x,double y,const QRectF &plot) const;
    QPair<int,int> visibleRange() const;
    QVector<int> sampledIndices(int first,int last) const;
    void draw(QPainter *painter,const QSizeF &size) const;
    QString exportMetadata(const QSize &size) const;
    QPointer<UncertaintyModel> m_model;
    QPointer<ChartAxis> m_xAxis,m_yAxis;
    Mode m_mode=ErrorBars;
    QColor m_background,m_grid,m_line,m_band,m_text,m_missing,m_selection;
    QVariantMap m_metrics;
    QString m_title;
    int m_paintPointLimit=2048;
    QString m_selectedId;
    double m_xStart=0,m_xEnd=1,m_yStart=0,m_yEnd=1;
};
} // namespace QindaTK
