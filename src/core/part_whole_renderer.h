// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "part_whole_layout.h"
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QPainterPath>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class PartWholeRenderer : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PartWholeModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QVariantList sliceColors READ sliceColors WRITE setSliceColors NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY styleChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics WRITE setMetrics NOTIFY styleChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedNode READ selectedNode NOTIFY selectionChanged)
public:
    enum Mode { Pie, Donut, Treemap, Sunburst };
    Q_ENUM(Mode)
    explicit PartWholeRenderer(QQuickItem *parent=nullptr);
    PartWholeModel *model() const { return m_model; }
    void setModel(PartWholeModel *v);
    Mode mode() const { return m_mode; }
    void setMode(Mode v);
    QVariantList sliceColors() const { return m_sliceColors; }
    void setSliceColors(const QVariantList &v);
#define COLOR_PROPERTY(Getter,Setter,Field) QColor Getter() const { return m_##Field; } void set##Setter(const QColor &v);
    COLOR_PROPERTY(backgroundColor,BackgroundColor,background)
    COLOR_PROPERTY(borderColor,BorderColor,border)
    COLOR_PROPERTY(textColor,TextColor,text)
    COLOR_PROPERTY(selectionColor,SelectionColor,selection)
#undef COLOR_PROPERTY
    QVariantMap metrics() const { return m_metrics; }
    void setMetrics(const QVariantMap &v);
    QString selectedId() const { return m_selectedId; }
    QVariantMap selectedNode() const;
    Q_INVOKABLE bool selectId(const QString &id);
    Q_INVOKABLE bool navigateSelection(int key);
    Q_INVOKABLE QString hitAt(double x,double y) const;
    Q_INVOKABLE QVariantMap geometryFor(const QString &id) const;
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path,const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path,const QSize &size) const;
    void paint(QPainter *painter) override;
signals:
    void modelChanged();
    void modeChanged();
    void styleChanged();
    void selectionChanged();
    void itemSelected(const QString &id);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    QRectF plotRect(const QSizeF &size) const;
    QColor nodeColor(int node, const QHash<int,int> &visibleIndex) const;
    void draw(QPainter *painter,const QSizeF &size) const;
    QPainterPath sectorPath(const QRectF &plot,const PartGeometry &geometry) const;
    QPointer<PartWholeModel> m_model;
    Mode m_mode = Pie;
    QVariantList m_sliceColors;
    QColor m_background,m_border,m_text,m_selection;
    QVariantMap m_metrics;
    QString m_selectedId;
};
} // namespace QindaTK
