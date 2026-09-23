// SPDX-License-Identifier: LGPL-3.0-or-later
#include "heatmap_renderer.h"
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace QindaTK {
HeatmapRenderer::HeatmapRenderer(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setActiveFocusOnTab(true);
    setFlag(ItemHasContents, true);
}
void HeatmapRenderer::setModel(HeatmapModel *v)
{
    if (m_model == v) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = v;
    if (v) {
        connect(v, &HeatmapModel::changed, this, [this] { update(); emit selectionChanged(); });
        connect(v, &QObject::destroyed, this, [this] { m_model = nullptr; update(); emit modelChanged(); });
    }
    m_selectedRow = m_selectedColumn = m_anchorRow = m_anchorColumn = -1;
    m_regionRow0 = m_regionRow1 = m_regionCol0 = m_regionCol1 = -1;
    setViewport(0, 1, 0, 1);
    emit modelChanged(); emit selectionChanged(); update();
}
#define COLOR_SETTER(Name, Field) void HeatmapRenderer::set##Name(const QColor &v) { if (m_##Field == v) return; m_##Field = v; emit styleChanged(); update(); }
COLOR_SETTER(LowColor, low)
COLOR_SETTER(HighColor, high)
COLOR_SETTER(MissingColor, missing)
COLOR_SETTER(SelectionColor, selection)
COLOR_SETTER(TextColor, text)
COLOR_SETTER(BackgroundColor, background)
#undef COLOR_SETTER
void HeatmapRenderer::setCategoryColors(const QVariantList &v) { m_categoryColors = v; emit styleChanged(); update(); }
void HeatmapRenderer::setMetrics(const QVariantMap &v) { m_metrics = v; emit styleChanged(); update(); }
QVariantMap HeatmapRenderer::selectedCell() const
{ return m_model ? m_model->cellAt(m_selectedRow, m_selectedColumn) : QVariantMap(); }
void HeatmapRenderer::setViewport(double x0, double x1, double y0, double y1)
{
    if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1) ||
        x0 < 0 || y0 < 0 || x1 > 1 || y1 > 1 || x1 <= x0 || y1 <= y0 ||
        x1-x0 < 1e-9 || y1-y0 < 1e-9) return;
    m_x0=x0; m_x1=x1; m_y0=y0; m_y1=y1;
    emit viewportChanged(); update();
}
void HeatmapRenderer::pan(double dx, double dy)
{
    const double sx = m_x1-m_x0, sy = m_y1-m_y0;
    const double x0 = std::clamp(m_x0+dx*sx, 0.0, 1.0-sx);
    const double y0 = std::clamp(m_y0+dy*sy, 0.0, 1.0-sy);
    setViewport(x0, x0+sx, y0, y0+sy);
}
void HeatmapRenderer::zoom(double factor, double ax, double ay)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    const double sx = std::clamp((m_x1-m_x0)/factor, 1e-9, 1.0);
    const double sy = std::clamp((m_y1-m_y0)/factor, 1e-9, 1.0);
    const double cx = m_x0 + std::clamp(ax,0.0,1.0)*(m_x1-m_x0);
    const double cy = m_y0 + std::clamp(ay,0.0,1.0)*(m_y1-m_y0);
    const double x0 = std::clamp(cx-std::clamp(ax,0.0,1.0)*sx,0.0,1.0-sx);
    const double y0 = std::clamp(cy-std::clamp(ay,0.0,1.0)*sy,0.0,1.0-sy);
    setViewport(x0,x0+sx,y0,y0+sy);
}
QVariantMap HeatmapRenderer::selectedRegion() const
{
    if (m_regionRow0 < 0) return {};
    return {{"firstRow",m_regionRow0},{"lastRow",m_regionRow1},
            {"firstColumn",m_regionCol0},{"lastColumn",m_regionCol1}};
}
void HeatmapRenderer::applySelection(int anchorRow, int anchorColumn, int activeRow, int activeColumn, bool emitCell)
{
    m_anchorRow=anchorRow; m_anchorColumn=anchorColumn;
    m_selectedRow=activeRow; m_selectedColumn=activeColumn;
    m_regionRow0=std::min(anchorRow,activeRow); m_regionRow1=std::max(anchorRow,activeRow)+1;
    m_regionCol0=std::min(anchorColumn,activeColumn); m_regionCol1=std::max(anchorColumn,activeColumn)+1;
    emit selectionChanged();
    if (emitCell) emit cellSelected(selectedCell().value("id").toString());
    emit regionSelected(selectedRegion()); update();
}
bool HeatmapRenderer::selectCell(int row, int column)
{
    if (!m_model || row < 0 || column < 0 || row >= m_model->rows() || column >= m_model->columns()) return false;
    applySelection(row,column,row,column,true); return true;
}
bool HeatmapRenderer::selectRegion(int firstRow, int lastRow, int firstColumn, int lastColumn)
{
    if (!m_model || firstRow < 0 || firstColumn < 0 || lastRow > m_model->rows() ||
        lastColumn > m_model->columns() || firstRow >= lastRow || firstColumn >= lastColumn) return false;
    applySelection(firstRow,firstColumn,lastRow-1,lastColumn-1,false); return true;
}
bool HeatmapRenderer::navigateSelection(int key, bool extend)
{
    if (!m_model || !m_model->rows() || !m_model->columns()) return false;
    int r = m_selectedRow < 0 ? 0 : m_selectedRow;
    int c = m_selectedColumn < 0 ? 0 : m_selectedColumn;
    switch (key) {
    case Qt::Key_Left: --c; break; case Qt::Key_Right: ++c; break;
    case Qt::Key_Up: --r; break; case Qt::Key_Down: ++r; break;
    default: return false;
    }
    r=std::clamp(r,0,m_model->rows()-1); c=std::clamp(c,0,m_model->columns()-1);
    if (extend && m_anchorRow >= 0) applySelection(m_anchorRow,m_anchorColumn,r,c,false);
    else applySelection(r,c,r,c,true);
    return true;
}
QRectF HeatmapRenderer::plotRect(const QSizeF &size) const
{
    const qreal left = m_metrics.value("leftMargin").toDouble();
    const qreal right = m_metrics.value("rightMargin").toDouble();
    const qreal top = m_metrics.value("topMargin").toDouble();
    const qreal bottom = m_metrics.value("bottomMargin").toDouble();
    return {left, top, qMax<qreal>(0,size.width()-left-right), qMax<qreal>(0,size.height()-top-bottom)};
}
QColor HeatmapRenderer::binColor(const QVariantMap &bin, bool maximum) const
{
    if (!m_model || !bin.value("present").toInt()) return m_missing;
    if (!m_model->valueCategories().isEmpty()) {
        const QString category = maximum && bin.value("mixed").toBool()
            ? bin.value("categoryHigh").toString() : bin.value("category").toString();
        const int index=m_model->valueCategories().indexOf(category);
        if (index < 0 || m_categoryColors.isEmpty()) return m_high;
        return m_categoryColors.at(index % m_categoryColors.size()).value<QColor>();
    }
    const double lo=m_model->colorMinimum(), hi=m_model->colorMaximum();
    const double value=bin.value(maximum ? "maximum" : "minimum").toDouble();
    const double t=hi>lo ? std::clamp((value-lo)/(hi-lo),0.0,1.0) : 0.0;
    return QColor::fromRgbF(m_low.redF()*(1-t)+m_high.redF()*t,
        m_low.greenF()*(1-t)+m_high.greenF()*t,
        m_low.blueF()*(1-t)+m_high.blueF()*t);
}
void HeatmapRenderer::draw(QPainter *p, const QSizeF &size) const
{
    p->fillRect(QRectF(QPointF(0,0),size),m_background);
    if (!m_model || !m_model->isValid()) return;
    const QRectF plot=plotRect(size);
    if (plot.isEmpty()) return;
    const int rows=m_model->rows(), cols=m_model->columns();
    const int r0=std::clamp(int(std::floor(m_y0*rows)),0,rows-1);
    const int r1=std::clamp(int(std::ceil(m_y1*rows)),r0+1,rows);
    const int c0=std::clamp(int(std::floor(m_x0*cols)),0,cols-1);
    const int c1=std::clamp(int(std::ceil(m_x1*cols)),c0+1,cols);
    const int bx=std::min(64,c1-c0), by=std::min(64,r1-r0);
    const QVariantList bins=m_model->tile(r0,r1,c0,c1,by,bx);
    m_paintBinCount=bins.size();
    p->save(); p->setClipRect(plot); p->setPen(Qt::NoPen);
    for (const QVariant &entry : bins) {
        const QVariantMap b=entry.toMap();
        const double x0=plot.left()+(b.value("column").toInt()/double(cols)-m_x0)/(m_x1-m_x0)*plot.width();
        const double x1=plot.left()+(b.value("columnEnd").toInt()/double(cols)-m_x0)/(m_x1-m_x0)*plot.width();
        const double y0=plot.top()+(b.value("row").toInt()/double(rows)-m_y0)/(m_y1-m_y0)*plot.height();
        const double y1=plot.top()+(b.value("rowEnd").toInt()/double(rows)-m_y0)/(m_y1-m_y0)*plot.height();
        QRectF rect(QPointF(x0,y0),QPointF(x1,y1));
        const qint64 present=b.value("present").toLongLong(), missing=b.value("missing").toLongLong();
        if (!present) p->fillRect(rect,m_missing);
        else {
            const bool split=b.value("mixed").toBool() || (m_model->valueCategories().isEmpty() && b.value("minimum") != b.value("maximum"));
            if (split) {
                QRectF left=rect; left.setWidth(rect.width()/2);
                QRectF right=rect; right.setLeft(left.right());
                p->fillRect(left,binColor(b,false)); p->fillRect(right,binColor(b,true));
            } else p->fillRect(rect,binColor(b,false));
        }
        // Missing data receives a patterned overlay even in a mixed aggregate.
        if (missing) { p->setBrush(QBrush(m_background,Qt::BDiagPattern)); p->drawRect(rect); p->setBrush(Qt::NoBrush); }
    }
    if (m_regionRow0 >= 0) {
        const double x=plot.left()+(m_regionCol0/double(cols)-m_x0)/(m_x1-m_x0)*plot.width();
        const double y=plot.top()+(m_regionRow0/double(rows)-m_y0)/(m_y1-m_y0)*plot.height();
        const double w=(m_regionCol1-m_regionCol0)*plot.width()/cols/(m_x1-m_x0);
        const double h=(m_regionRow1-m_regionRow0)*plot.height()/rows/(m_y1-m_y0);
        p->setPen(QPen(m_selection,m_metrics.value("selectionWidth").toDouble()));
        p->setBrush(Qt::NoBrush); p->drawRect(QRectF(x,y,w,h));
    }
    p->restore();
    p->setPen(m_text);
    QFont font=p->font(); font.setPixelSize(m_metrics.value("fontSize").toInt()); p->setFont(font);
    const QString xLabel=m_model->xCategories().isEmpty()
        ? QStringLiteral("%1 … %2 %3")
            .arg(m_model->xMinimum()+m_x0*(m_model->xMaximum()-m_model->xMinimum()))
            .arg(m_model->xMinimum()+m_x1*(m_model->xMaximum()-m_model->xMinimum()))
            .arg(m_model->xUnit())
        : QStringLiteral("%1 … %2").arg(m_model->xCategories().value(c0)).arg(m_model->xCategories().value(c1-1));
    const QString yLabel=m_model->yCategories().isEmpty()
        ? QStringLiteral("%1 … %2 %3")
            .arg(m_model->yMaximum()-m_y0*(m_model->yMaximum()-m_model->yMinimum()))
            .arg(m_model->yMaximum()-m_y1*(m_model->yMaximum()-m_model->yMinimum()))
            .arg(m_model->yUnit())
        : QStringLiteral("%1 … %2").arg(m_model->yCategories().value(r0)).arg(m_model->yCategories().value(r1-1));
    p->drawText(QRectF(plot.left(),plot.bottom(),plot.width(),size.height()-plot.bottom()),Qt::AlignCenter,xLabel);
    p->drawText(QRectF(0,0,plot.left(),size.height()),Qt::AlignCenter,yLabel);
    const qreal swatch=m_metrics.value("legendSwatch").toDouble();
    const qreal gap=m_metrics.value("legendGap").toDouble();
    if (swatch > 0 && plot.top() >= swatch) {
        qreal x=plot.left(); const qreal y=(plot.top()-swatch)/2;
        const auto legendPart = [&](const QColor &color, const QString &label) {
            p->fillRect(QRectF(x,y,swatch,swatch),color); x += swatch+gap;
            const qreal textWidth=QFontMetricsF(p->font()).horizontalAdvance(label)+gap;
            p->drawText(QRectF(x,0,textWidth,plot.top()),Qt::AlignVCenter,label); x += textWidth+gap;
        };
        if (m_model->valueCategories().isEmpty()) {
            legendPart(m_low,QString::number(m_model->colorMinimum())+" "+m_model->valueUnit());
            legendPart(m_high,QString::number(m_model->colorMaximum())+" "+m_model->valueUnit());
        } else {
            for (int i=0; i<m_model->valueCategories().size() && i<m_categoryColors.size(); ++i)
                legendPart(m_categoryColors[i].value<QColor>(),m_model->valueCategories()[i]);
        }
        legendPart(m_missing,QStringLiteral("Missing"));
    }
}
void HeatmapRenderer::paint(QPainter *p) { draw(p,QSizeF(width(),height())); }
QImage HeatmapRenderer::renderImage(const QSize &size) const
{
    if (size.width()<1 || size.height()<1 || size.width()>8192 || size.height()>8192) return {};
    QImage image(size,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); draw(&painter,size); return image;
}
bool HeatmapRenderer::exportPng(const QString &path,const QSize &size) const
{
    const QImage image=renderImage(size); if (image.isNull()) return false;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || !image.save(&file,"PNG")) return false;
    return file.commit();
}
bool HeatmapRenderer::exportSvg(const QString &path,const QSize &size) const
{
    if (size.width()<1 || size.height()<1 || size.width()>8192 || size.height()>8192) return false;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    QSvgGenerator svg; svg.setOutputDevice(&file); svg.setSize(size); svg.setViewBox(QRect(QPoint(),size));
    QPainter painter(&svg); draw(&painter,size); painter.end(); return file.commit();
}
QPair<int,int> HeatmapRenderer::cellAtPosition(const QPointF &point) const
{
    if (!m_model || !m_model->rows() || !m_model->columns()) return {-1,-1};
    const QRectF plot=plotRect(QSizeF(width(),height()));
    if (!plot.contains(point) || plot.isEmpty()) return {-1,-1};
    const double x=m_x0+(point.x()-plot.left())/plot.width()*(m_x1-m_x0);
    const double y=m_y0+(point.y()-plot.top())/plot.height()*(m_y1-m_y0);
    return {std::clamp(int(y*m_model->rows()),0,m_model->rows()-1),
            std::clamp(int(x*m_model->columns()),0,m_model->columns()-1)};
}
void HeatmapRenderer::mousePressEvent(QMouseEvent *e)
{
    if (!m_model) return;
    forceActiveFocus(); m_dragPoint=e->position();
    if (e->button()==Qt::LeftButton) {
        const auto [row,column]=cellAtPosition(m_dragPoint);
        if (row >= 0) {
            if ((e->modifiers() & Qt::ShiftModifier) && m_anchorRow >= 0)
                applySelection(m_anchorRow,m_anchorColumn,row,column,false);
            else selectCell(row,column);
            m_selectDragging=true;
        }
    }
    e->accept();
}
void HeatmapRenderer::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::MiddleButton) {
        const QRectF plot=plotRect(QSizeF(width(),height()));
        if (!plot.isEmpty()) pan((m_dragPoint.x()-e->position().x())/plot.width(),
                                (m_dragPoint.y()-e->position().y())/plot.height());
        m_dragPoint=e->position(); e->accept();
    } else if (m_selectDragging && (e->buttons() & Qt::LeftButton)) {
        const auto [row,column]=cellAtPosition(e->position());
        if (row >= 0) applySelection(m_anchorRow,m_anchorColumn,row,column,false);
        e->accept();
    }
}
void HeatmapRenderer::mouseReleaseEvent(QMouseEvent *e)
{ m_selectDragging=false; e->accept(); }
void HeatmapRenderer::wheelEvent(QWheelEvent *e)
{
    const QRectF plot=plotRect(QSizeF(width(),height()));
    if (!plot.isEmpty()) zoom(std::pow(1.2,e->angleDelta().y()/120.0),
        (e->position().x()-plot.left())/plot.width(),(e->position().y()-plot.top())/plot.height());
    e->accept();
}
void HeatmapRenderer::keyPressEvent(QKeyEvent *e)
{ if (navigateSelection(e->key(),e->modifiers() & Qt::ShiftModifier)) e->accept(); else QQuickPaintedItem::keyPressEvent(e); }
} // namespace QindaTK
