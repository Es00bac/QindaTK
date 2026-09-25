// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_figure_composer.h"
#include "scientific_sampling.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QString panelLabel(int index)
{
    // a..z, then aa, ab, ... (figures rarely need more than 26 panels).
    QString label;
    for (int n = index; ; n = n / 26 - 1) {
        label.prepend(QChar('a' + n % 26));
        if (n < 26) break;
    }
    return label;
}
}

ScientificFigureComposer::ScientificFigureComposer(QObject *parent) : QObject(parent) {}

QQmlListProperty<ScientificPlotRenderer> ScientificFigureComposer::panelList()
{ return {this, nullptr, &appendTo, &countOf, &at, &clearOf}; }
void ScientificFigureComposer::appendTo(QQmlListProperty<ScientificPlotRenderer> *list, ScientificPlotRenderer *value)
{ static_cast<ScientificFigureComposer *>(list->object)->appendPanel(value); }
qsizetype ScientificFigureComposer::countOf(QQmlListProperty<ScientificPlotRenderer> *list)
{ return static_cast<ScientificFigureComposer *>(list->object)->m_panels.size(); }
ScientificPlotRenderer *ScientificFigureComposer::at(QQmlListProperty<ScientificPlotRenderer> *list, qsizetype index)
{ return static_cast<ScientificFigureComposer *>(list->object)->m_panels.value(index); }
void ScientificFigureComposer::clearOf(QQmlListProperty<ScientificPlotRenderer> *list)
{ auto *self = static_cast<ScientificFigureComposer *>(list->object); self->m_panels.clear(); emit self->changed(); }

void ScientificFigureComposer::setColumns(int value)
{ value = std::clamp(value, 1, 8); if (m_columns == value) return; m_columns = value; emit changed(); }

QRectF ScientificFigureComposer::panelRect(int index, const QSize &size) const
{
    const int n = int(m_panels.size());
    if (index < 0 || index >= n || size.isEmpty()) return {};
    const double gap = m_metrics.value(QStringLiteral("gap")).toDouble();
    const double titleHeight = m_title.isEmpty() ? 0 : m_metrics.value(QStringLiteral("fontSize")).toDouble() * 2 + gap;
    const int columns = std::min(m_columns, n), rows = (n + columns - 1) / columns;
    const double cellW = (size.width() - gap * (columns + 1)) / columns;
    const double cellH = (size.height() - titleHeight - gap * (rows + 1)) / rows;
    if (cellW < 1 || cellH < 1) return {};
    // AGENT-NOTE: cells are snapped to whole pixels so each panel renders at
    // an integer size, which keeps its export metadata size exact.
    return {std::floor(gap + (index % columns) * (cellW + gap)), std::floor(titleHeight + gap + (index / columns) * (cellH + gap)),
            std::floor(cellW), std::floor(cellH)};
}

void ScientificFigureComposer::draw(QPainter *p, const QSize &size) const
{
    p->fillRect(QRect(QPoint(), size), m_background);
    QFont font = p->font();
    font.setFamily(m_metrics.value(QStringLiteral("fontFamily")).toString());
    font.setPixelSize(std::max(1, m_metrics.value(QStringLiteral("fontSize")).toInt()));
    const double gap = m_metrics.value(QStringLiteral("gap")).toDouble();
    if (!m_title.isEmpty()) {
        QFont bold = font; bold.setBold(true); p->setFont(bold); p->setPen(m_text);
        p->drawText(QRectF(0, gap, size.width(), font.pixelSize() * 2), Qt::AlignCenter, m_title);
    }
    for (int i = 0; i < m_panels.size(); ++i) {
        const QRectF cell = panelRect(i, size);
        if (!m_panels[i] || cell.isEmpty()) continue;
        p->save();
        p->translate(cell.topLeft());
        p->setClipRect(QRectF(QPointF(), cell.size()));
        m_panels[i]->drawFigure(p, cell.size());
        p->setFont(font); p->setPen(m_text);
        p->drawText(QPointF(gap, QFontMetricsF(font).ascent() + gap), QStringLiteral("(%1)").arg(panelLabel(i)));
        p->restore();
    }
}

QString ScientificFigureComposer::metadataJson(int width, int height) const
{
    const QSize size(width, height);
    QJsonArray panels;
    for (int i = 0; i < m_panels.size(); ++i) {
        const QRectF cell = panelRect(i, size);
        if (!m_panels[i] || cell.isEmpty()) continue;
        panels.append(QJsonObject{{QStringLiteral("label"), panelLabel(i)},
            {QStringLiteral("rect"), QJsonArray{cell.x(), cell.y(), cell.width(), cell.height()}},
            {QStringLiteral("figure"), m_panels[i]->figureMetadata(cell.size().toSize())}});
    }
    const QJsonObject metadata{{QStringLiteral("schema"), QStringLiteral("QindaTK.scientificComposite.v1")},
        {QStringLiteral("title"), m_title}, {QStringLiteral("columns"), m_columns},
        {QStringLiteral("width"), width}, {QStringLiteral("height"), height}, {QStringLiteral("panels"), panels},
        {QStringLiteral("style"), ScientificSampling::styleJson(m_metrics,
            {{QStringLiteral("backgroundColor"), m_background}, {QStringLiteral("textColor"), m_text}})}};
    return QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
}

QImage ScientificFigureComposer::renderImage(const QSize &size) const
{
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image); draw(&painter, size); painter.end();
    image.setText(QStringLiteral("QindaTK.ScientificComposite"), metadataJson(size.width(), size.height()));
    return image;
}
bool ScientificFigureComposer::exportPng(const QString &path, const QSize &size) const
{
    const QImage image = renderImage(size);
    if (image.isNull()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) return false;
    return file.commit();
}
bool ScientificFigureComposer::exportSvg(const QString &path, const QSize &size) const
{
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192) return false;
    QByteArray bytes; QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return false;
    QSvgGenerator svg; svg.setOutputDevice(&buffer); svg.setSize(size); svg.setViewBox(QRect(QPoint(), size));
    svg.setTitle(m_title);
    QPainter painter(&svg); draw(&painter, size); painter.end(); buffer.close();
    const qsizetype end = bytes.lastIndexOf("</svg>");
    if (end < 0) return false;
    bytes.insert(end, "<metadata id=\"qindatk-scientific-composite\" data-encoding=\"base64-json\">"
                 + metadataJson(size.width(), size.height()).toUtf8().toBase64() + "</metadata>");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

} // namespace QindaTK
