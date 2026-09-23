// SPDX-License-Identifier: LGPL-3.0-or-later
#include "part_whole_model.h"
#include <cmath>
#include <functional>

namespace QindaTK {
PartWholeModel::PartWholeModel(QObject *parent) : QAbstractListModel(parent) {}
void PartWholeModel::setUnit(const QString &v) { if (m_unit==v) return; m_unit=v; emit changed(); }
int PartWholeModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }
QVariant PartWholeModel::data(const QModelIndex &index,int role) const
{
    if (!index.isValid() || index.row()<0 || index.row()>=count()) return {};
    const auto row=record(index.row());
    switch (role) {
    case IdRole: return row.value("id"); case ParentIdRole: return row.value("parentId");
    case LabelRole: return row.value("label"); case ValueRole: return row.value("value");
    case KnownTotalRole: return row.value("knownTotal"); case StatusRole: return row.value("status");
    case DepthRole: return row.value("depth"); case ShareRole: return row.value("share");
    default: return {};
    }
}
QHash<int,QByteArray> PartWholeModel::roleNames() const
{ return {{IdRole,"id"},{ParentIdRole,"parentId"},{LabelRole,"label"},{ValueRole,"value"},
          {KnownTotalRole,"knownTotal"},{StatusRole,"status"},{DepthRole,"depth"},{ShareRole,"share"}}; }
void PartWholeModel::error(const QString &message)
{ if (m_lastError==message) return; m_lastError=message; emit errorChanged(); }
QVariantMap PartWholeModel::record(int index) const
{
    if (index<0 || index>=count()) return {};
    const Node &n=m_nodes[index];
    const double parentTotal=n.parent<0 ? m_knownTotal : m_nodes[n.parent].knownTotal;
    const QString status=!n.complete ? QStringLiteral("missing")
        : n.knownTotal==0 ? QStringLiteral("zero") : QStringLiteral("present");
    return {{"id",n.id},{"parentId",n.parentId},{"label",n.label},
            {"value",n.declared ? QVariant(n.value) : QVariant()},
            {"knownTotal",n.knownTotal},{"status",status},{"depth",n.depth},
            {"share",parentTotal>0 ? n.knownTotal/parentTotal : 0.0},{"unit",m_unit}};
}
QVariantMap PartWholeModel::get(int row) const { return record(row); }
QVariantMap PartWholeModel::node(const QString &id) const { return record(indexOf(id)); }
QVector<int> PartWholeModel::visibleRoots() const
{
    if (m_roots.size()==1 && !m_nodes[m_roots.first()].children.isEmpty())
        return m_nodes[m_roots.first()].children;
    return m_roots;
}
QStringList PartWholeModel::visibleRootIds() const
{ QStringList ids; for (int i:visibleRoots()) ids.append(m_nodes[i].id); return ids; }
bool PartWholeModel::replaceNodes(const QVariantList &rows)
{
    if (rows.size()>4096) { error(QStringLiteral("Node limit is 4096")); return false; }
    QVector<Node> nodes; nodes.reserve(rows.size()); QHash<QString,int> ids;
    for (const QVariant &entry:rows) {
        const QVariantMap row=entry.toMap();
        Node n; n.id=row.value("id").toString(); n.parentId=row.value("parentId").toString();
        n.label=row.value("label").toString();
        if (n.id.isEmpty() || ids.contains(n.id)) { error(QStringLiteral("IDs must be unique and nonempty")); return false; }
        if (n.label.isEmpty()) n.label=n.id;
        const QVariant value=row.value("value"); n.declared=value.isValid() && !value.isNull();
        if (n.declared) {
            bool ok=false; n.value=value.toDouble(&ok);
            if (!ok || !std::isfinite(n.value) || n.value<0) {
                error(QStringLiteral("Values must be finite and nonnegative")); return false;
            }
        }
        ids.insert(n.id,nodes.size()); nodes.append(n);
    }
    QVector<int> roots;
    for (int i=0;i<nodes.size();++i) {
        Node &n=nodes[i];
        if (n.parentId.isEmpty()) { roots.append(i); continue; }
        n.parent=ids.value(n.parentId,-1);
        if (n.parent<0 || n.parent==i) { error(QStringLiteral("Invalid parent ID")); return false; }
        nodes[n.parent].children.append(i);
    }
    QVector<int> state(nodes.size(),0); int missing=0, zero=0; QString failure;
    std::function<bool(int,int)> visit=[&](int i,int depth) {
        if (depth>8) { failure=QStringLiteral("Depth limit is 8"); return false; }
        if (state[i]==1) { failure=QStringLiteral("Cycle in hierarchy"); return false; }
        if (state[i]==2) return true;
        state[i]=1; Node &n=nodes[i]; n.depth=depth;
        if (n.children.isEmpty()) {
            n.complete=n.declared; n.knownTotal=n.declared ? n.value : 0;
            if (!n.declared) ++missing;
            else if (n.value==0) ++zero;
        } else {
            double sum=0; bool complete=true;
            for (int child:n.children) {
                if (!visit(child,depth+1)) return false;
                sum+=nodes[child].knownTotal; complete &= nodes[child].complete;
            }
            if (!std::isfinite(sum)) { failure=QStringLiteral("Total overflow"); return false; }
            if (n.declared && (!complete || std::abs(n.value-sum)>1e-9*std::max(1.0,sum))) {
                failure=QStringLiteral("Declared parent total differs from complete children"); return false;
            }
            n.knownTotal=sum; n.complete=complete;
        }
        state[i]=2; return true;
    };
    for (int root:roots) if (!visit(root,0)) {
        error(failure.isEmpty() ? QStringLiteral("Invalid hierarchy") : failure);
        return false;
    }
    for (int value:state) if (value!=2) { error(QStringLiteral("Cycle in hierarchy")); return false; }
    double total=0; for (int root:roots) total+=nodes[root].knownTotal;
    if (!std::isfinite(total)) { error(QStringLiteral("Total overflow")); return false; }
    beginResetModel(); m_nodes=std::move(nodes); m_roots=std::move(roots); m_ids=std::move(ids);
    m_knownTotal=total; m_missingCount=missing; m_zeroCount=zero; endResetModel();
    error({}); emit changed(); return true;
}
} // namespace QindaTK
