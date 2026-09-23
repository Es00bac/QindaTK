// SPDX-License-Identifier: LGPL-3.0-or-later
#include "part_whole_renderer.h"
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSvgGenerator>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace QindaTK {
PartWholeRenderer::PartWholeRenderer(QQuickItem *parent):QQuickPaintedItem(parent)
{ setAcceptedMouseButtons(Qt::LeftButton); setActiveFocusOnTab(true); setFlag(ItemHasContents,true); }
void PartWholeRenderer::setModel(PartWholeModel *v)
{
    if (m_model==v) return;
    if (m_model) disconnect(m_model,nullptr,this,nullptr);
    m_model=v;
    if (v) {
        connect(v,&PartWholeModel::changed,this,[this] {
            if (!m_selectedId.isEmpty() && m_model->indexOf(m_selectedId)<0) {
                m_selectedId.clear(); emit selectionChanged();
            }
            update();
        });
        connect(v,&QObject::destroyed,this,[this] { m_model=nullptr; m_selectedId.clear(); emit modelChanged(); emit selectionChanged(); update(); });
    }
    m_selectedId.clear(); emit modelChanged(); emit selectionChanged(); update();
}
void PartWholeRenderer::setMode(Mode v)
{ if (m_mode==v) return; m_mode=v; emit modeChanged(); update(); }
void PartWholeRenderer::setSliceColors(const QVariantList &v)
{ m_sliceColors=v; emit styleChanged(); update(); }
#define COLOR_SETTER(Name,Field) void PartWholeRenderer::set##Name(const QColor &v) { if (m_##Field==v) return; m_##Field=v; emit styleChanged(); update(); }
COLOR_SETTER(BackgroundColor,background)
COLOR_SETTER(BorderColor,border)
COLOR_SETTER(TextColor,text)
COLOR_SETTER(SelectionColor,selection)
#undef COLOR_SETTER
void PartWholeRenderer::setMetrics(const QVariantMap &v)
{ m_metrics=v; emit styleChanged(); update(); }
QVariantMap PartWholeRenderer::selectedNode() const
{ return m_model ? m_model->node(m_selectedId) : QVariantMap(); }
bool PartWholeRenderer::selectId(const QString &id)
{
    if (!m_model || m_model->indexOf(id)<0) return false;
    m_selectedId=id; emit selectionChanged(); emit itemSelected(id); update(); return true;
}
bool PartWholeRenderer::navigateSelection(int key)
{
    if (!m_model || !m_model->count()) return false;
    int direction=0;
    if (key==Qt::Key_Right || key==Qt::Key_Down) direction=1;
    else if (key==Qt::Key_Left || key==Qt::Key_Up) direction=-1;
    else return false;
    const auto &nodes=m_model->nodes();
    int index=m_model->indexOf(m_selectedId);
    if (index<0) index=direction>0 ? -1 : nodes.size();
    index=std::clamp(index+direction,0,int(nodes.size())-1);
    return selectId(nodes[index].id);
}
QRectF PartWholeRenderer::plotRect(const QSizeF &size) const
{
    const qreal margin=m_metrics.value("margin").toDouble();
    const qreal bottom=m_metrics.value("bottomMargin").toDouble();
    return {margin,margin,qMax<qreal>(0,size.width()-2*margin),qMax<qreal>(0,size.height()-margin-bottom)};
}
QColor PartWholeRenderer::nodeColor(int node, const QHash<int,int> &visibleIndex) const
{
    if (m_sliceColors.isEmpty() || !m_model) return m_border;
    const auto &nodes=m_model->nodes();
    int root=node;
    while (root>=0 && !visibleIndex.contains(root)) root=nodes[root].parent;
    int index=visibleIndex.value(root,-1);
    if (index<0) index=node;
    return m_sliceColors[index%m_sliceColors.size()].value<QColor>();
}
QPainterPath PartWholeRenderer::sectorPath(const QRectF &plot,const PartGeometry &g) const
{
    const QPointF center=plot.center(); const qreal r=std::min(plot.width(),plot.height())/2;
    const QRectF outer(center.x()-r*g.outerRadius,center.y()-r*g.outerRadius,
                       2*r*g.outerRadius,2*r*g.outerRadius);
    const double qStart=90-g.angleStart;
    QPainterPath path;
    if (g.innerRadius<=0) path.moveTo(center);
    else {
        const double a=g.angleStart*std::numbers::pi/180.0;
        path.moveTo(center.x()+r*g.innerRadius*std::sin(a),center.y()-r*g.innerRadius*std::cos(a));
    }
    path.arcTo(outer,qStart,-g.angleSpan);
    if (g.innerRadius>0) {
        const qreal inner=r*g.innerRadius;
        const QRectF innerRect(center.x()-inner,center.y()-inner,2*inner,2*inner);
        path.arcTo(innerRect,qStart-g.angleSpan,g.angleSpan);
    }
    path.closeSubpath(); return path;
}
void PartWholeRenderer::draw(QPainter *p,const QSizeF &size) const
{
    p->fillRect(QRectF(QPointF(0,0),size),m_background);
    if (!m_model || !m_model->count()) return;
    const QRectF plot=plotRect(size);
    if (plot.isEmpty()) return;
    const auto geometry=PartWholeLayout::build(*m_model,PartWholeLayout::Mode(m_mode));
    const qreal line=m_metrics.value("lineWidth").toDouble();
    p->save(); p->setClipRect(plot);
    const auto &nodes=m_model->nodes();
    QHash<int,int> visibleIndex;
    const auto visible=m_model->visibleRoots();
    for (int i=0;i<visible.size();++i) visibleIndex.insert(visible[i],i);
    QFont font=p->font(); font.setPixelSize(m_metrics.value("fontSize").toInt()); p->setFont(font);
    const QFontMetricsF fontMetrics(font);
    const qreal padding=m_metrics.value("labelPadding").toDouble();
    for (const PartGeometry &g:geometry) {
        const QColor fill=nodeColor(g.node,visibleIndex);
        const bool selected=nodes[g.node].id==m_selectedId;
        p->setPen(QPen(selected ? m_selection : m_border,selected ? line*2 : line));
        p->setBrush(fill);
        QRectF itemRect;
        if (g.sector) p->drawPath(sectorPath(plot,g));
        else {
            itemRect=QRectF(plot.left()+g.rect.left()*plot.width(),
                plot.top()+g.rect.top()*plot.height(),g.rect.width()*plot.width(),g.rect.height()*plot.height());
            p->drawRect(itemRect);
        }
        const QString label=nodes[g.node].label;
        const qreal labelWidth=fontMetrics.horizontalAdvance(label)+2*padding;
        const qreal labelHeight=fontMetrics.height()+2*padding;
        QPointF center;
        bool fits=false;
        if (g.sector) {
            const qreal r=std::min(plot.width(),plot.height())/2;
            const qreal middle=(g.innerRadius+g.outerRadius)/2;
            const double angle=(g.angleStart+g.angleSpan/2)*std::numbers::pi/180.0;
            center=plot.center()+QPointF(r*middle*std::sin(angle),-r*middle*std::cos(angle));
            fits=(g.outerRadius-g.innerRadius)*r>=labelHeight &&
                middle*r*g.angleSpan*std::numbers::pi/180.0>=labelWidth;
        } else { center=itemRect.center(); fits=itemRect.width()>=labelWidth && itemRect.height()>=labelHeight; }
        if (fits) {
            const QRectF labelRect(center.x()-labelWidth/2,center.y()-labelHeight/2,labelWidth,labelHeight);
            p->fillRect(labelRect,m_background);
            p->setPen(m_text); p->drawText(labelRect,Qt::AlignCenter,label);
        }
    }
    p->restore();
    p->setFont(font); p->setPen(m_text);
    const QString total=tr("Known %1 %2 · %3 zero · %4 missing excluded")
        .arg(m_model->knownTotal()).arg(m_model->unit())
        .arg(m_model->zeroCount()).arg(m_model->missingCount());
    p->drawText(QRectF(plot.left(),plot.bottom(),plot.width(),size.height()-plot.bottom()),Qt::AlignCenter,total);
}
void PartWholeRenderer::paint(QPainter *p) { draw(p,{width(),height()}); }
QString PartWholeRenderer::hitAt(double x,double y) const
{
    if (!m_model || !std::isfinite(x) || !std::isfinite(y)) return {};
    const QRectF plot=plotRect({width(),height()});
    if (!plot.contains(x,y)) return {};
    const auto geometry=PartWholeLayout::build(*m_model,PartWholeLayout::Mode(m_mode));
    for (auto it=geometry.crbegin();it!=geometry.crend();++it) {
        bool hit=false;
        if (it->sector) hit=sectorPath(plot,*it).contains({x,y});
        else hit=QRectF(plot.left()+it->rect.left()*plot.width(),plot.top()+it->rect.top()*plot.height(),
                        it->rect.width()*plot.width(),it->rect.height()*plot.height()).contains(x,y);
        if (hit) return m_model->nodes()[it->node].id;
    }
    return {};
}
QVariantMap PartWholeRenderer::geometryFor(const QString &id) const
{
    if (!m_model) return {};
    for (const auto &g:PartWholeLayout::build(*m_model,PartWholeLayout::Mode(m_mode)))
        if (m_model->nodes()[g.node].id==id)
            return {{"angleStart",g.angleStart},{"angleSpan",g.angleSpan},
                    {"x",g.rect.x()},{"y",g.rect.y()},
                    {"width",g.rect.width()},{"height",g.rect.height()},
                    {"innerRadius",g.innerRadius},{"outerRadius",g.outerRadius}};
    return {};
}
QImage PartWholeRenderer::renderImage(const QSize &size) const
{
    if (size.width()<1 || size.height()<1 || size.width()>8192 || size.height()>8192) return {};
    QImage image(size,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); draw(&painter,size); return image;
}
bool PartWholeRenderer::exportPng(const QString &path,const QSize &size) const
{
    const QImage image=renderImage(size); if (image.isNull()) return false;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || !image.save(&file,"PNG")) return false;
    return file.commit();
}
bool PartWholeRenderer::exportSvg(const QString &path,const QSize &size) const
{
    if (size.width()<1 || size.height()<1 || size.width()>8192 || size.height()>8192) return false;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    QSvgGenerator svg; svg.setOutputDevice(&file); svg.setSize(size); svg.setViewBox(QRect(QPoint(),size));
    QPainter painter(&svg); draw(&painter,size); painter.end(); return file.commit();
}
void PartWholeRenderer::mousePressEvent(QMouseEvent *e)
{
    forceActiveFocus(); const QString id=hitAt(e->position().x(),e->position().y());
    if (!id.isEmpty()) selectId(id);
    e->accept();
}
void PartWholeRenderer::keyPressEvent(QKeyEvent *e)
{ if (navigateSelection(e->key())) e->accept(); else QQuickPaintedItem::keyPressEvent(e); }
} // namespace QindaTK
