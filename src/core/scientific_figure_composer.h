// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "scientific_plot_renderer.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Composes several ScientificPlotRenderer panels into one exported figure:
// a title row, then panels row-major in `columns` equal cells, each labelled
// (a), (b), ... in its top-left corner. Every panel is drawn by its own
// drawFigure() at the cell size, so a panel exports exactly as it would alone.
// AGENT-CONTRACT: the composite metadata embeds each panel's complete
// figureMetadata() (sources, digests, axes, sampling) under its label.
class ScientificFigureComposer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQmlListProperty<QindaTK::ScientificPlotRenderer> panels READ panelList NOTIFY changed)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY changed)
    Q_PROPERTY(QString title MEMBER m_title NOTIFY changed)
    Q_PROPERTY(QColor backgroundColor MEMBER m_background NOTIFY changed)
    Q_PROPERTY(QColor textColor MEMBER m_text NOTIFY changed)
    // fontFamily, fontSize, gap (pixels between cells and around the title).
    Q_PROPERTY(QVariantMap metrics MEMBER m_metrics NOTIFY changed)
public:
    explicit ScientificFigureComposer(QObject *parent = nullptr);
    QQmlListProperty<ScientificPlotRenderer> panelList();
    void appendPanel(ScientificPlotRenderer *panel) { m_panels.append(panel); emit changed(); }
    int columns() const { return m_columns; }
    void setColumns(int value);
    // Cell rectangle of panel `index` in an output of `size`.
    Q_INVOKABLE QRectF panelRect(int index, const QSize &size) const;
    Q_INVOKABLE QString metadataJson(int width, int height) const;
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path, const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path, const QSize &size) const;
signals:
    void changed();
private:
    static void appendTo(QQmlListProperty<ScientificPlotRenderer> *list, ScientificPlotRenderer *value);
    static qsizetype countOf(QQmlListProperty<ScientificPlotRenderer> *list);
    static ScientificPlotRenderer *at(QQmlListProperty<ScientificPlotRenderer> *list, qsizetype index);
    static void clearOf(QQmlListProperty<ScientificPlotRenderer> *list);
    void draw(QPainter *painter, const QSize &size) const;
    QList<QPointer<ScientificPlotRenderer>> m_panels;
    int m_columns = 2;
    QString m_title;
    QColor m_background, m_text;
    QVariantMap m_metrics;
};

} // namespace QindaTK
