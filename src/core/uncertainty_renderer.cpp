// SPDX-License-Identifier: LGPL-3.0-or-later
#include "uncertainty_renderer.h"
#include <QBuffer>
#include <QFontMetricsF>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QSvgGenerator>
#include <algorithm>
#include <cmath>

namespace QindaTK {
UncertaintyRenderer::UncertaintyRenderer(QQuickItem *parent):QQuickPaintedItem(parent)
{ setAcceptedMouseButtons(Qt::LeftButton); setActiveFocusOnTab(true); }
void UncertaintyRenderer::setModel(UncertaintyModel *value)
{
    if (m_model==value) return;
    if (m_model) disconnect(m_model,nullptr,this,nullptr);
    m_model=value;
    if (value) {
        connect(value,&UncertaintyModel::changed,this,[this] {
            if (!m_selectedId.isEmpty() && m_model->indexOf(m_selectedId)<0) {
                m_selectedId.clear(); emit selectionChanged();
            }
            update();
        });
        connect(value,&QObject::destroyed,this,[this] {
            m_model=nullptr; m_selectedId.clear(); emit modelChanged(); emit selectionChanged(); update();
        });
    }
    m_selectedId.clear(); emit modelChanged(); emit selectionChanged(); update();
}
void UncertaintyRenderer::setXAxis(ChartAxis *value)
{
    if (m_xAxis==value) return;
    if (m_xAxis) disconnect(m_xAxis,nullptr,this,nullptr);
    m_xAxis=value;
    if (value) {
        connect(value,&ChartAxis::changed,this,[this] { update(); });
        connect(value,&QObject::destroyed,this,[this] { m_xAxis=nullptr; emit axesChanged(); update(); });
    }
    emit axesChanged(); update();
}
void UncertaintyRenderer::setYAxis(ChartAxis *value)
{
    if (m_yAxis==value) return;
    if (m_yAxis) disconnect(m_yAxis,nullptr,this,nullptr);
    m_yAxis=value;
    if (value) {
        connect(value,&ChartAxis::changed,this,[this] { update(); });
        connect(value,&QObject::destroyed,this,[this] { m_yAxis=nullptr; emit axesChanged(); update(); });
    }
    emit axesChanged(); update();
}
void UncertaintyRenderer::setMode(Mode value)
{ if (m_mode==value) return; m_mode=value; emit modeChanged(); update(); }
#define SET_COLOR(Name,Field) void UncertaintyRenderer::set##Name(const QColor &value) { if (m_##Field==value) return; m_##Field=value; emit styleChanged(); update(); }
SET_COLOR(BackgroundColor,background)
SET_COLOR(GridColor,grid)
SET_COLOR(LineColor,line)
SET_COLOR(BandColor,band)
SET_COLOR(TextColor,text)
SET_COLOR(MissingColor,missing)
SET_COLOR(SelectionColor,selection)
#undef SET_COLOR
void UncertaintyRenderer::setMetrics(const QVariantMap &value)
{ m_metrics=value; emit styleChanged(); update(); }
void UncertaintyRenderer::setTitle(const QString &value)
{ if (m_title==value) return; m_title=value; emit styleChanged(); update(); }
void UncertaintyRenderer::setPaintPointLimit(int value)
{ value=qBound(32,value,4096); if (m_paintPointLimit==value) return; m_paintPointLimit=value; emit styleChanged(); update(); }
QVariantMap UncertaintyRenderer::selectedInterval() const
{ return m_model ? m_model->interval(m_selectedId) : QVariantMap(); }
bool UncertaintyRenderer::selectId(const QString &id)
{
    if (!m_model || m_model->indexOf(id)<0) return false;
    if (m_selectedId==id) return true;
    m_selectedId=id; emit selectionChanged(); emit intervalSelected(id); update(); return true;
}
bool UncertaintyRenderer::navigateSelection(int key)
{
    if (!m_model || !m_model->count()) return false;
    int direction=0;
    if (key==Qt::Key_Right || key==Qt::Key_Down) direction=1;
    else if (key==Qt::Key_Left || key==Qt::Key_Up) direction=-1;
    else return false;
    int index=m_model->indexOf(m_selectedId);
    if (index<0) index=direction>0 ? -1 : m_model->count();
    index=std::clamp(index+direction,0,m_model->count()-1);
    return selectId(m_model->intervals()[index].id);
}
void UncertaintyRenderer::setViewport(double xs,double xe,double ys,double ye)
{
    if (!std::isfinite(xs) || !std::isfinite(xe) || !std::isfinite(ys) || !std::isfinite(ye)
        || xs<0 || xe>1 || ys<0 || ye>1 || xe-xs<1e-6 || ye-ys<1e-6) return;
    if (m_xStart==xs && m_xEnd==xe && m_yStart==ys && m_yEnd==ye) return;
    m_xStart=xs; m_xEnd=xe; m_yStart=ys; m_yEnd=ye;
    emit viewportChanged(); update();
}
void UncertaintyRenderer::pan(double dx,double dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    const double x=std::clamp(m_xStart+dx,0.0,1.0-viewX());
    const double y=std::clamp(m_yStart+dy,0.0,1.0-viewY());
    setViewport(x,x+viewX(),y,y+viewY());
}
void UncertaintyRenderer::zoom(double factor,double xAnchor,double yAnchor)
{
    if (!std::isfinite(factor) || factor<=0 || !std::isfinite(xAnchor) || !std::isfinite(yAnchor)) return;
    const double nx=std::clamp(viewX()/factor,1e-6,1.0);
    const double ny=std::clamp(viewY()/factor,1e-6,1.0);
    const double xs=std::clamp(m_xStart+viewX()*xAnchor-nx*xAnchor,0.0,1.0-nx);
    const double ys=std::clamp(m_yStart+viewY()*yAnchor-ny*yAnchor,0.0,1.0-ny);
    setViewport(xs,xs+nx,ys,ys+ny);
}
QRectF UncertaintyRenderer::plotRect(const QSizeF &size) const
{
    const qreal left=m_metrics.value("leftMargin").toDouble();
    const qreal right=m_metrics.value("rightMargin").toDouble();
    const qreal top=m_metrics.value("topMargin").toDouble();
    const qreal bottom=m_metrics.value("bottomMargin").toDouble();
    return {left,top,qMax<qreal>(0,size.width()-left-right),qMax<qreal>(0,size.height()-top-bottom)};
}
bool UncertaintyRenderer::canDraw() const
{
    return m_model && m_xAxis && m_yAxis && m_xAxis->isValid() && m_yAxis->isValid()
        && m_xAxis->kind()!=ChartAxis::Category && m_xAxis->kind()!=ChartAxis::DateTime
        && m_yAxis->kind()!=ChartAxis::Category && m_yAxis->kind()!=ChartAxis::DateTime
        && m_xAxis->unit()==m_model->xUnit() && m_yAxis->unit()==m_model->yUnit();
}
QPointF UncertaintyRenderer::point(double x,double y,const QRectF &plot) const
{
    const double nx=(m_xAxis->map(x)-m_xStart)/viewX();
    const double ny=(m_yAxis->map(y)-m_yStart)/viewY();
    return {plot.left()+nx*plot.width(),plot.bottom()-ny*plot.height()};
}
QPair<int,int> UncertaintyRenderer::visibleRange() const
{
    if (!canDraw()) return {0,0};
    const double a=m_xAxis->unmap(m_xStart),b=m_xAxis->unmap(m_xEnd);
    int first=m_model->lowerBound(std::min(a,b));
    int last=m_model->upperBound(std::max(a,b));
    if (m_mode==Band) { first=qMax(0,first-1); last=qMin(m_model->count(),last+1); }
    return {first,last};
}
QVector<int> UncertaintyRenderer::sampledIndices(int first,int last) const
{
    QVector<int> out; const int count=last-first;
    if (count<=0) return out;
    const auto &rows=m_model->intervals();
    if (count<=m_paintPointLimit) {
        out.reserve(count);
        for (int i=first;i<last;++i) out.append(i);
        return out;
    }
    const int stride=(count+m_paintPointLimit-1)/m_paintPointLimit;
    out.reserve(qMin(count,3*m_paintPointLimit));
    for (int begin=first;begin<last;begin+=stride) {
        const int end=qMin(last,begin+stride);
        int best=-1,firstGap=-1,lastGap=-1; double width=-1;
        for (int i=begin;i<end;++i) {
            const auto &v=rows[i];
            if (v.missing) { if (firstGap<0) firstGap=i; lastGap=i; continue; }
            const double span=v.high-v.low;
            if (span>width) { width=span; best=i; }
        }
        if (m_mode==Band) {
            QVector<int> bucket;
            if (firstGap>=0) bucket.append(firstGap);
            if (best>=0) bucket.append(best);
            if (lastGap>=0 && lastGap!=firstGap) bucket.append(lastGap);
            std::sort(bucket.begin(),bucket.end());
            for (int index:bucket) out.append(index);
        } else out.append(best>=0 ? best : begin);
    }
    return out;
}
QVariantMap UncertaintyRenderer::projected(const QString &id) const
{
    if (!canDraw()) return {};
    const int index=m_model->indexOf(id);
    if (index<0) return {};
    const auto &v=m_model->intervals()[index];
    const QRectF plot=plotRect({width(),height()});
    if (plot.isEmpty()) return {};
    const QPointF estimate=point(v.x,v.estimate,plot);
    QVariantMap result{{"x",estimate.x()},{"estimateY",estimate.y()}};
    if (!v.missing) {
        result.insert("lowY",point(v.x,v.low,plot).y());
        result.insert("highY",point(v.x,v.high,plot).y());
    }
    return result;
}
QVariantMap UncertaintyRenderer::dataAt(double pixelX,double pixelY) const
{
    if (!canDraw() || !std::isfinite(pixelX) || !std::isfinite(pixelY)) return {};
    const QRectF plot=plotRect({width(),height()});
    if (plot.isEmpty() || !plot.contains(pixelX,pixelY)) return {};
    const double xf=m_xStart+viewX()*(pixelX-plot.left())/plot.width();
    const double yf=m_yStart+viewY()*(plot.bottom()-pixelY)/plot.height();
    const double x=m_xAxis->unmap(xf),y=m_yAxis->unmap(yf);
    if (!std::isfinite(x) || !std::isfinite(y)) return {};
    return {{"x",x},{"y",y},{"xUnit",m_xAxis->unit()},{"yUnit",m_yAxis->unit()}};
}
int UncertaintyRenderer::visiblePaintCount() const
{
    const auto [first,last]=visibleRange();
    return sampledIndices(first,last).size();
}
QString UncertaintyRenderer::hitAt(double x,double y) const
{
    if (!canDraw() || !std::isfinite(x) || !std::isfinite(y)) return {};
    const QRectF plot=plotRect({width(),height()});
    if (!plot.contains(x,y)) return {};
    const auto [first,last]=visibleRange();
    const qreal radius=m_metrics.value("hitRadius").toDouble();
    double best=radius+1; QString id;
    for (int i:sampledIndices(first,last)) {
        if (i<0) continue;
        const auto &v=m_model->intervals()[i];
        const QPointF center=point(v.x,v.estimate,plot);
        if (!std::isfinite(center.x()) || !std::isfinite(center.y())) continue;
        const double dx=std::abs(center.x()-x);
        if (dx>radius) continue;
        double dy=std::abs(center.y()-y);
        if (!v.missing) {
            const double lo=point(v.x,v.low,plot).y(),hi=point(v.x,v.high,plot).y();
            if (std::isfinite(lo) && std::isfinite(hi))
                dy=std::max(0.0,std::max(std::min(lo,hi)-y,y-std::max(lo,hi)));
        }
        const double distance=std::hypot(dx,dy);
        if (distance<best && distance<=radius) { best=distance; id=v.id; }
    }
    return id;
}
QString UncertaintyRenderer::csvData() const
{ return m_model ? m_model->csvData() : QString(); }
void UncertaintyRenderer::draw(QPainter *p,const QSizeF &size) const
{
    p->fillRect(QRectF(QPointF(),size),m_background);
    const QRectF plot=plotRect(size);
    if (plot.isEmpty()) return;
    QFont font=p->font(); font.setFamily(m_metrics.value("fontFamily").toString());
    font.setPixelSize(m_metrics.value("fontSize").toInt()); p->setFont(font);
    if (!m_title.isEmpty()) {
        p->setPen(m_text);
        p->drawText(QRectF(plot.left(),0,plot.width(),plot.top()),Qt::AlignCenter,m_title);
    }
    const qreal lineWidth=m_metrics.value("lineWidth").toDouble();
    p->setPen(QPen(m_grid,lineWidth)); p->drawRect(plot);
    if (!canDraw()) {
        p->setPen(m_text); p->drawText(plot,Qt::AlignCenter,tr("Declare matching numeric axes and units"));
        return;
    }
    p->setPen(QPen(m_grid,lineWidth));
    for (int step=1;step<4;++step) {
        const qreal x=plot.left()+plot.width()*step/4;
        const qreal y=plot.top()+plot.height()*step/4;
        p->drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
        p->drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
    }
    const QFontMetricsF fontMetrics(font);
    const qreal labelGap=m_metrics.value("labelGap").toDouble();
    const qreal tickWidth=m_metrics.value("tickLabelWidth").toDouble();
    p->setPen(m_text);
    for (int step=0;step<=4;++step) {
        const double fraction=step/4.0;
        const qreal x=plot.left()+plot.width()*fraction;
        const qreal y=plot.bottom()-plot.height()*fraction;
        const QString xLabel=QString::number(m_xAxis->unmap(m_xStart+viewX()*fraction),'g',4);
        const QString yLabel=QString::number(m_yAxis->unmap(m_yStart+viewY()*fraction),'g',4);
        p->drawText(QRectF(x-tickWidth/2,plot.bottom()+labelGap,tickWidth,fontMetrics.height()),
            Qt::AlignCenter,xLabel);
        p->drawText(QRectF(0,y-fontMetrics.height()/2,plot.left()-labelGap,fontMetrics.height()),
            Qt::AlignRight|Qt::AlignVCenter,yLabel);
    }
    const auto [first,last]=visibleRange();
    const auto indices=sampledIndices(first,last);
    const auto &rows=m_model->intervals();
    p->save(); p->setClipRect(plot);
    const qreal radius=m_metrics.value("pointRadius").toDouble();
    const qreal cap=m_metrics.value("capHalfWidth").toDouble();
    if (m_mode==Band) {
        QVector<QPointF> upper,lower,center;
        auto flush=[&] {
            if (center.size()>1) {
                QPainterPath area; area.moveTo(upper.first());
                for (int i=1;i<upper.size();++i) area.lineTo(upper[i]);
                for (int i=lower.size()-1;i>=0;--i) area.lineTo(lower[i]);
                area.closeSubpath(); p->fillPath(area,m_band);
                p->setPen(QPen(m_line,lineWidth));
                p->drawPolyline(upper.constData(),upper.size());
                p->drawPolyline(lower.constData(),lower.size());
                p->drawPolyline(center.constData(),center.size());
            } else if (center.size()==1) {
                p->setPen(QPen(m_line,lineWidth));
                p->drawLine(upper.first(),lower.first()); p->drawEllipse(center.first(),radius,radius);
            }
            upper.clear(); lower.clear(); center.clear();
        };
        for (int i:indices) {
            if (rows[i].missing) {
                flush();
                const QPointF c=point(rows[i].x,rows[i].estimate,plot);
                if (std::isfinite(c.x()) && std::isfinite(c.y())) {
                    p->setPen(QPen(m_missing,lineWidth)); p->setBrush(Qt::NoBrush);
                    p->drawEllipse(c,radius,radius);
                    p->drawLine(c-QPointF(radius,radius),c+QPointF(radius,radius));
                }
                continue;
            }
            const auto &v=rows[i];
            const QPointF u=point(v.x,v.high,plot),l=point(v.x,v.low,plot),c=point(v.x,v.estimate,plot);
            if (!std::isfinite(u.y()) || !std::isfinite(l.y()) || !std::isfinite(c.y())) { flush(); continue; }
            upper.append(u); lower.append(l); center.append(c);
        }
        flush();
    } else {
        for (int i:indices) {
            if (i<0) continue;
            const auto &v=rows[i];
            const QPointF c=point(v.x,v.estimate,plot);
            if (!std::isfinite(c.x()) || !std::isfinite(c.y())) continue;
            p->setPen(QPen(v.missing ? m_missing : m_line,lineWidth));
            if (!v.missing) {
                const QPointF low=point(v.x,v.low,plot),high=point(v.x,v.high,plot);
                if (!std::isfinite(low.y()) || !std::isfinite(high.y())) continue;
                p->drawLine(low,high);
                p->drawLine(QPointF(low.x()-cap,low.y()),QPointF(low.x()+cap,low.y()));
                p->drawLine(QPointF(high.x()-cap,high.y()),QPointF(high.x()+cap,high.y()));
                p->setBrush(m_line); p->drawEllipse(c,radius,radius);
            } else {
                p->setBrush(Qt::NoBrush); p->drawEllipse(c,radius,radius);
                p->drawLine(c-QPointF(radius,radius),c+QPointF(radius,radius));
            }
        }
    }
    if (!m_selectedId.isEmpty()) {
        const int i=m_model->indexOf(m_selectedId);
        if (i>=first && i<last) {
            const auto &v=rows[i]; const QPointF c=point(v.x,v.estimate,plot);
            if (std::isfinite(c.x()) && std::isfinite(c.y())) {
                p->setPen(QPen(m_selection,lineWidth*2)); p->setBrush(Qt::NoBrush);
                p->drawEllipse(c,radius*2,radius*2);
            }
        }
    }
    p->restore();
    p->setPen(m_text);
    const QString footer=m_model->meaningLabel()
        +tr(" · Y %1 · X %2 · %3 missing intervals")
            .arg(m_model->yUnit()).arg(m_model->xUnit()).arg(m_model->missingCount());
    const qreal footerTop=plot.bottom()+fontMetrics.height()+labelGap;
    p->drawText(QRectF(plot.left(),footerTop,plot.width(),size.height()-footerTop),Qt::AlignCenter,footer);
}
void UncertaintyRenderer::paint(QPainter *p) { draw(p,{width(),height()}); }
QString UncertaintyRenderer::exportMetadata(const QSize &size) const
{
    QJsonObject metadata=m_model
        ? QJsonDocument::fromJson(m_model->metadataJson().toUtf8()).object() : QJsonObject();
    auto axis=[](const ChartAxis *value) {
        if (!value) return QJsonObject();
        return QJsonObject{{"kind",int(value->kind())},{"minimum",value->minimum()},
            {"maximum",value->maximum()},{"unit",value->unit()},{"reversed",value->reversed()}};
    };
    metadata.insert("render",QJsonObject{{"title",m_title},{"mode",int(m_mode)},
        {"width",size.width()},{"height",size.height()},
        {"xAxis",axis(m_xAxis)},{"yAxis",axis(m_yAxis)},
        {"xViewportStart",m_xStart},{"xViewportEnd",m_xEnd},
        {"yViewportStart",m_yStart},{"yViewportEnd",m_yEnd}});
    return QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
}
QImage UncertaintyRenderer::renderImage(const QSize &size) const
{
    if (size.width()<1 || size.height()<1 || size.width()>8192 || size.height()>8192) return {};
    QImage image(size,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); draw(&painter,size); painter.end();
    image.setText(QStringLiteral("QindaTK.IntervalData"),exportMetadata(size));
    return image;
}
bool UncertaintyRenderer::exportPng(const QString &path,const QSize &size) const
{
    const QImage image=renderImage(size); if (image.isNull()) return false;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || !image.save(&file,"PNG")) return false;
    return file.commit();
}
bool UncertaintyRenderer::exportSvg(const QString &path,const QSize &size) const
{
    if (size.width()<1 || size.height()<1 || size.width()>8192 || size.height()>8192) return false;
    QByteArray bytes; QBuffer buffer(&bytes); if (!buffer.open(QIODevice::WriteOnly)) return false;
    QSvgGenerator svg; svg.setOutputDevice(&buffer); svg.setSize(size); svg.setViewBox(QRect(QPoint(),size));
    QPainter painter(&svg); draw(&painter,size); painter.end(); buffer.close();
    const qsizetype end=bytes.lastIndexOf("</svg>"); if (end<0) return false;
    const QByteArray metadata=exportMetadata(size).toUtf8().toBase64();
    bytes.insert(end,"<metadata id=\"qindatk-intervals\" data-encoding=\"base64-json\">"+metadata+"</metadata>");
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()) return false;
    return file.commit();
}
void UncertaintyRenderer::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(); const QString id=hitAt(event->position().x(),event->position().y());
    if (!id.isEmpty()) selectId(id);
    event->accept();
}
void UncertaintyRenderer::keyPressEvent(QKeyEvent *event)
{ if (navigateSelection(event->key())) event->accept(); else QQuickPaintedItem::keyPressEvent(event); }
} // namespace QindaTK
