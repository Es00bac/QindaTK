// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "heatmap_model.h"
#include <QColor>
#include <QImage>
#include <QVariantMap>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class HeatmapRenderer : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(HeatmapModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY styleChanged)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY styleChanged)
    Q_PROPERTY(QColor missingColor READ missingColor WRITE setMissingColor NOTIFY styleChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QVariantList categoryColors READ categoryColors WRITE setCategoryColors NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY styleChanged)
    Q_PROPERTY(double horizontalViewFraction READ horizontalViewFraction NOTIFY viewportChanged)
    Q_PROPERTY(double verticalViewFraction READ verticalViewFraction NOTIFY viewportChanged)
    Q_PROPERTY(QVariantMap selectedCell READ selectedCell NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedRegion READ selectedRegion NOTIFY selectionChanged)
    Q_PROPERTY(int paintBinCount READ paintBinCount NOTIFY viewportChanged)
public:
    explicit HeatmapRenderer(QQuickItem *parent = nullptr);
    HeatmapModel *model() const { return m_model; }
    void setModel(HeatmapModel *v);
#define COLOR_PROPERTY(Getter, Setter, Field) QColor Getter() const { return m_##Field; } void set##Setter(const QColor &v);
    COLOR_PROPERTY(lowColor, LowColor, low)
    COLOR_PROPERTY(highColor, HighColor, high)
    COLOR_PROPERTY(missingColor, MissingColor, missing)
    COLOR_PROPERTY(selectionColor, SelectionColor, selection)
    COLOR_PROPERTY(textColor, TextColor, text)
    COLOR_PROPERTY(backgroundColor, BackgroundColor, background)
#undef COLOR_PROPERTY
    QVariantList categoryColors() const { return m_categoryColors; }
    void setCategoryColors(const QVariantList &v);
    QVariantMap metrics() const { return m_metrics; }
    void setMetrics(const QVariantMap &v);
    double horizontalViewFraction() const { return m_x1 - m_x0; }
    double verticalViewFraction() const { return m_y1 - m_y0; }
    QVariantMap selectedCell() const;
    QVariantMap selectedRegion() const;
    int paintBinCount() const { return m_paintBinCount; }
    Q_INVOKABLE void setViewport(double x0, double x1, double y0, double y1);
    Q_INVOKABLE void pan(double xFraction, double yFraction);
    Q_INVOKABLE void zoom(double factor, double xAnchor = 0.5, double yAnchor = 0.5);
    Q_INVOKABLE bool selectCell(int row, int column);
    Q_INVOKABLE bool selectRegion(int firstRow, int lastRow, int firstColumn, int lastColumn);
    Q_INVOKABLE bool navigateSelection(int key, bool extend = false);
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path, const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path, const QSize &size) const;
    void paint(QPainter *painter) override;
signals:
    void modelChanged();
    void styleChanged();
    void viewportChanged();
    void selectionChanged();
    void cellSelected(const QString &id);
    void regionSelected(const QVariantMap &region);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    void draw(QPainter *painter, const QSizeF &size) const;
    void applySelection(int anchorRow, int anchorColumn, int activeRow, int activeColumn, bool emitCell);
    QPair<int,int> cellAtPosition(const QPointF &point) const;
    QRectF plotRect(const QSizeF &size) const;
    QColor binColor(const QVariantMap &bin, bool maximum) const;
    QPointer<HeatmapModel> m_model;
    QColor m_low, m_high, m_missing, m_selection, m_text, m_background;
    QVariantList m_categoryColors;
    QVariantMap m_metrics;
    double m_x0 = 0, m_x1 = 1, m_y0 = 0, m_y1 = 1;
    int m_selectedRow = -1, m_selectedColumn = -1;
    int m_anchorRow = -1, m_anchorColumn = -1;
    int m_regionRow0 = -1, m_regionRow1 = -1, m_regionCol0 = -1, m_regionCol1 = -1;
    bool m_selectDragging = false;
    mutable int m_paintBinCount = 0;
    QPointF m_dragPoint;
};
} // namespace QindaTK
