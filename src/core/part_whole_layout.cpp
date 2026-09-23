// SPDX-License-Identifier: LGPL-3.0-or-later
#include "part_whole_layout.h"
#include <algorithm>
#include <functional>

namespace QindaTK {
QVector<PartGeometry> PartWholeLayout::build(const PartWholeModel &model, Mode mode)
{
    QVector<PartGeometry> output;
    const auto &nodes=model.nodes();
    const QVector<int> visible=model.visibleRoots();
    double total=0; for (int i:visible) total+=nodes[i].knownTotal;
    if (!(total>0)) return output;
    output.reserve(nodes.size());
    if (mode==Pie || mode==Donut) {
        double angle=0;
        for (int i:visible) {
            const double span=360.0*nodes[i].knownTotal/total;
            if (span>0) output.append({i,{},angle,span,mode==Donut ? 0.5 : 0.0,1,true});
            angle+=span;
        }
        return output;
    }
    if (mode==Treemap) {
        std::function<void(const QVector<int>&,QRectF,int)> layout=[&](const QVector<int> &indices,QRectF rect,int depth) {
            double sum=0; for (int i:indices) sum+=nodes[i].knownTotal;
            if (!(sum>0) || rect.isEmpty()) return;
            double progress=0;
            for (int i:indices) {
                const double fraction=nodes[i].knownTotal/sum;
                if (!(fraction>0)) continue;
                QRectF child=rect;
                if (depth%2==0) { child.setLeft(rect.left()+progress*rect.width()); child.setWidth(fraction*rect.width()); }
                else { child.setTop(rect.top()+progress*rect.height()); child.setHeight(fraction*rect.height()); }
                output.append({i,child,0,0,0,1,false});
                if (!nodes[i].children.isEmpty()) layout(nodes[i].children,child,depth+1);
                progress+=fraction;
            }
        };
        layout(visible,QRectF(0,0,1,1),0); return output;
    }
    int maxDepth=0;
    std::function<void(int,int)> measure=[&](int i,int depth) {
        if (!(nodes[i].knownTotal>0)) return;
        maxDepth=std::max(maxDepth,depth);
        for (int child:nodes[i].children) measure(child,depth+1);
    };
    for (int i:visible) measure(i,0);
    const double band=1.0/(maxDepth+1);
    std::function<void(const QVector<int>&,double,double,int)> layout=[&](const QVector<int> &indices,double start,double span,int depth) {
        double sum=0; for (int i:indices) sum+=nodes[i].knownTotal;
        if (!(sum>0)) return;
        double angle=start;
        for (int i:indices) {
            const double nodeSpan=span*nodes[i].knownTotal/sum;
            if (nodeSpan>0) {
                output.append({i,{},angle,nodeSpan,depth*band,(depth+1)*band,true});
                if (!nodes[i].children.isEmpty()) layout(nodes[i].children,angle,nodeSpan,depth+1);
            }
            angle+=nodeSpan;
        }
    };
    layout(visible,0,360,0);
    return output;
}
} // namespace QindaTK
