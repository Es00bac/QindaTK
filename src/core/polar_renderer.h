// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "polar_data_model.h"
#include "polar_transform.h"

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class PolarRenderer : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PolarDataModel *model READ model WRITE setModel NOTIFY configurationChanged)
    Q_PROPERTY(AngleMode angleMode READ angleMode WRITE setAngleMode NOTIFY configurationChanged)
    Q_PROPERTY(AngleUnit angleUnit READ angleUnit WRITE setAngleUnit NOTIFY configurationChanged)
    Q_PROPERTY(QStringList categories READ categories WRITE setCategories NOTIFY configurationChanged)
    Q_PROPERTY(double radialMinimum READ radialMinimum WRITE setRadialMinimum NOTIFY configurationChanged)
    Q_PROPERTY(double radialMaximum READ radialMaximum WRITE setRadialMaximum NOTIFY configurationChanged)
    Q_PROPERTY(QString radialUnit READ radialUnit WRITE setRadialUnit NOTIFY configurationChanged)
    Q_PROPERTY(double startAngleDegrees READ startAngleDegrees WRITE setStartAngleDegrees NOTIFY configurationChanged)
    Q_PROPERTY(bool clockwise READ clockwise WRITE setClockwise NOTIFY configurationChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QColor defaultSeriesColor READ defaultSeriesColor WRITE setDefaultSeriesColor NOTIFY styleChanged)
    Q_PROPERTY(QVariantList seriesColors READ seriesColors WRITE setSeriesColors NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY styleChanged)
    Q_PROPERTY(double fillOpacity READ fillOpacity WRITE setFillOpacity NOTIFY styleChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY configurationChanged)
    Q_PROPERTY(QString error READ error NOTIFY configurationChanged)
    Q_PROPERTY(QString selectedSeriesId READ selectedSeriesId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPointId READ selectedPointId NOTIFY selectionChanged)
public:
    enum AngleMode { Numeric, Category };
    Q_ENUM(AngleMode)
    enum AngleUnit { Degrees, Radians };
    Q_ENUM(AngleUnit)
    explicit PolarRenderer(QQuickItem *parent = nullptr);
    PolarDataModel *model() const { return m_model; }
    void setModel(PolarDataModel *value);
    AngleMode angleMode() const { return m_angleMode; }
    void setAngleMode(AngleMode value);
    AngleUnit angleUnit() const { return m_angleUnit; }
    void setAngleUnit(AngleUnit value);
    QStringList categories() const { return m_categories; }
    void setCategories(const QStringList &value);
    double radialMinimum() const { return m_radialMinimum; }
    void setRadialMinimum(double value);
    double radialMaximum() const { return m_radialMaximum; }
    void setRadialMaximum(double value);
    QString radialUnit() const { return m_radialUnit; }
    void setRadialUnit(const QString &value);
    double startAngleDegrees() const { return m_startAngleDegrees; }
    void setStartAngleDegrees(double value);
    bool clockwise() const { return m_clockwise; }
    void setClockwise(bool value);
    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &value);
    QColor gridColor() const { return m_grid; }
    void setGridColor(const QColor &value);
    QColor textColor() const { return m_text; }
    void setTextColor(const QColor &value);
    QColor defaultSeriesColor() const { return m_defaultSeriesColor; }
    void setDefaultSeriesColor(const QColor &value);
    QVariantList seriesColors() const { return m_seriesColors; }
    void setSeriesColors(const QVariantList &value);
    QVariantMap metrics() const { return m_metrics; }
    void setMetrics(const QVariantMap &value);
    double fillOpacity() const { return m_fillOpacity; }
    void setFillOpacity(double value);
    bool valid() const { return error().isEmpty(); }
    QString error() const;
    QString selectedSeriesId() const { return m_selectedSeriesId; }
    QString selectedPointId() const { return m_selectedPointId; }
    Q_INVOKABLE QPointF project(double angle, double radius) const;
    Q_INVOKABLE QPointF projectCategory(const QString &category, double radius) const;
    Q_INVOKABLE QVariantMap unproject(double x, double y) const;
    Q_INVOKABLE QVariantMap hitTest(double x, double y) const;
    Q_INVOKABLE bool selectAt(double x, double y);
    Q_INVOKABLE bool navigateSelection(int key);
    Q_INVOKABLE QVariantMap selectedPoint() const;
    Q_INVOKABLE QString csv() const;
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path, const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path, const QSize &size) const;
    void paint(QPainter *painter) override;
signals:
    void configurationChanged();
    void styleChanged();
    void selectionChanged();
    void pointSelected(const QString &seriesId, const QString &pointId);
    void exportImageReady(const QImage &image);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    void refresh();
    QRectF plotSquare(const QSizeF &size) const;
    QPointF pointPosition(const PolarPoint &point, const QRectF &square) const;
    double pointAngleDegrees(const PolarPoint &point) const;
    double metric(const char *name, double fallback) const;
    QColor seriesColor(int index) const;
    void draw(QPainter *painter, const QSizeF &size) const;
    QPointer<PolarDataModel> m_model;
    AngleMode m_angleMode = Numeric;
    AngleUnit m_angleUnit = Degrees;
    QStringList m_categories;
    double m_radialMinimum = 0;
    double m_radialMaximum = 1;
    QString m_radialUnit;
    double m_startAngleDegrees = 0;
    bool m_clockwise = true;
    QColor m_background, m_grid, m_text, m_defaultSeriesColor;
    QVariantList m_seriesColors;
    QVariantMap m_metrics;
    double m_fillOpacity = 0.35;
    QString m_selectedSeriesId, m_selectedPointId;
};

} // namespace QindaTK
