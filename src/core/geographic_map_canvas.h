// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "geographic_feature_model.h"
#include "geographic_tile_source.h"
#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QPointer>
#include <QPainterPath>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class GeographicMapCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(GeographicFeatureModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(double centerLongitude READ centerLongitude WRITE setCenterLongitude NOTIFY viewportChanged)
    Q_PROPERTY(double centerLatitude READ centerLatitude WRITE setCenterLatitude NOTIFY viewportChanged)
    Q_PROPERTY(double zoomLevel READ zoomLevel WRITE setZoomLevel NOTIFY viewportChanged)
    Q_PROPERTY(QString selectedFeatureId READ selectedFeatureId NOTIFY selectionChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_background NOTIFY styleChanged)
    Q_PROPERTY(QColor featureColor MEMBER m_feature NOTIFY styleChanged)
    Q_PROPERTY(QColor selectionColor MEMBER m_selection NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor MEMBER m_grid NOTIFY styleChanged)
    Q_PROPERTY(double pointRadius MEMBER m_pointRadius NOTIFY styleChanged)
    Q_PROPERTY(double lineWidth MEMBER m_lineWidth NOTIFY styleChanged)
    Q_PROPERTY(double gridWidth MEMBER m_gridWidth NOTIFY styleChanged)
    Q_PROPERTY(double hitRadius MEMBER m_hitRadius NOTIFY styleChanged)
    Q_PROPERTY(double fillOpacity MEMBER m_fillOpacity NOTIFY styleChanged)
    Q_PROPERTY(double selectedFillOpacity MEMBER m_selectedFillOpacity NOTIFY styleChanged)
    Q_PROPERTY(GeographicTileSource *tileSource READ tileSource WRITE setTileSource NOTIFY tileSourceChanged)
    Q_PROPERTY(QString tileStatus READ tileStatus NOTIFY tileStatusChanged)
public:
    explicit GeographicMapCanvas(QQuickItem *parent = nullptr);
    GeographicFeatureModel *model() const { return m_model; }
    void setModel(GeographicFeatureModel *value);
    double centerLongitude() const;
    void setCenterLongitude(double value);
    double centerLatitude() const;
    void setCenterLatitude(double value);
    double zoomLevel() const { return m_zoom; }
    void setZoomLevel(double value);
    QString selectedFeatureId() const { return m_selectedId; }
    GeographicTileSource *tileSource() const { return m_tileSource; }
    void setTileSource(GeographicTileSource *source);
    QString tileStatus() const { return m_tileStatus; }
    Q_INVOKABLE QVariantMap coordinateAt(double x, double y) const;
    Q_INVOKABLE QVariantMap screenPosition(double longitude, double latitude) const;
    Q_INVOKABLE QString featureIdAt(double x, double y) const;
    Q_INVOKABLE bool selectAt(double x, double y);
    Q_INVOKABLE bool selectFeature(const QString &id);
    Q_INVOKABLE void panPixels(double dx, double dy);
    Q_INVOKABLE void zoomAt(double factor, double x, double y);
    void paint(QPainter *painter) override;
signals:
    void modelChanged();
    void viewportChanged();
    void selectionChanged();
    void featureSelected(const QString &id);
    void styleChanged();
    void tileSourceChanged();
    void tileStatusChanged();
protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    struct VisibleTile { QString key; QRectF rect; QImage image; };
    void refreshTiles();
    void paintTiles(QPainter *painter);
    QPointF screen(double lon, double lat) const;
    QPainterPath featurePath(const GeographicFeatureModel::Feature &feature, int worldCopy) const;
    double scale() const;
    static double wrap(double x);
    QPointer<GeographicFeatureModel> m_model;
    QPointer<GeographicTileSource> m_tileSource;
    QVector<VisibleTile> m_visibleTiles;
    QString m_tileStatus = QStringLiteral("unavailable");
    QColor m_background, m_feature, m_selection, m_grid;
    double m_centerX = 0.5, m_centerY = 0.5, m_zoom = 2;
    double m_pointRadius = 0, m_lineWidth = 0, m_gridWidth = 0, m_hitRadius = 0;
    double m_fillOpacity = 1, m_selectedFillOpacity = 1;
    QString m_selectedId;
    QPointF m_press, m_last;
    bool m_dragged = false;
};
}
