// SPDX-License-Identifier: LGPL-3.0-or-later
#include "uncertainty_model.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
QString csvCell(QString value)
{ value.replace('"',"\"\""); return '"'+value+'"'; }
QString number(double value) { return QString::number(value,'g',17); }
}
UncertaintyModel::UncertaintyModel(QObject *parent):QAbstractListModel(parent) {}
void UncertaintyModel::setError(const QString &value)
{ if (m_lastError==value) return; m_lastError=value; emit errorChanged(); }
void UncertaintyModel::notifyMetadata()
{
    if (count()) emit dataChanged(index(0),index(count()-1));
    emit changed();
}
void UncertaintyModel::setMeaning(Meaning value)
{ if (m_meaning==value) return; m_meaning=value; notifyMetadata(); }
void UncertaintyModel::setCoverage(double value)
{
    if (!std::isfinite(value) || value<=0 || value>=1) {
        setError(QStringLiteral("Coverage must be between zero and one")); return;
    }
    setError({});
    if (m_coverage==value) return;
    m_coverage=value; notifyMetadata();
}
void UncertaintyModel::setMeaningDetail(const QString &value)
{ if (m_meaningDetail==value) return; m_meaningDetail=value; notifyMetadata(); }
void UncertaintyModel::setSourceDatasetId(const QString &value)
{ if (m_sourceDatasetId==value) return; m_sourceDatasetId=value; notifyMetadata(); }
void UncertaintyModel::setXUnit(const QString &value)
{ if (m_xUnit==value) return; m_xUnit=value; notifyMetadata(); }
void UncertaintyModel::setYUnit(const QString &value)
{ if (m_yUnit==value) return; m_yUnit=value; notifyMetadata(); }
QString UncertaintyModel::meaningName() const
{
    switch (m_meaning) {
    case Confidence: return QStringLiteral("confidence");
    case Credible: return QStringLiteral("credible");
    case Domain: return QStringLiteral("domain");
    }
    return {};
}
QString UncertaintyModel::meaningLabel() const
{
    if (m_meaning==Domain) return m_meaningDetail;
    const QString prefix=QString::number(m_coverage*100,'g',5)+QStringLiteral("% ")+meaningName()+QStringLiteral(" interval");
    return m_meaningDetail.isEmpty() ? prefix : prefix+QStringLiteral(" — ")+m_meaningDetail;
}
int UncertaintyModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : count(); }
QHash<int,QByteArray> UncertaintyModel::roleNames() const
{
    return {{IdRole,"id"},{SourceIdRole,"sourceId"},{XRole,"x"},
        {EstimateRole,"estimate"},{LowRole,"low"},{HighRole,"high"},
        {StatusRole,"status"},{XUnitRole,"xUnit"},{YUnitRole,"yUnit"},
        {MeaningRole,"meaning"},{CoverageRole,"coverage"},{DetailRole,"meaningDetail"}};
}
QVariantMap UncertaintyModel::get(int row) const
{
    if (row<0 || row>=count()) return {};
    const auto &v=m_rows[row];
    return {{"id",v.id},{"sourceId",v.sourceId},{"x",v.x},
        {"estimate",v.estimate},{"low",v.missing ? QVariant() : QVariant(v.low)},
        {"high",v.missing ? QVariant() : QVariant(v.high)},
        {"status",v.missing ? QStringLiteral("missing") : QStringLiteral("present")},
        {"xUnit",m_xUnit},{"yUnit",m_yUnit},{"meaning",meaningName()},
        {"coverage",m_meaning==Domain ? QVariant() : QVariant(m_coverage)},
        {"meaningDetail",m_meaningDetail}};
}
QVariant UncertaintyModel::data(const QModelIndex &index,int role) const
{
    if (!index.isValid()) return {};
    const auto names=roleNames();
    return get(index.row()).value(QString::fromLatin1(names.value(role)));
}
QVariantMap UncertaintyModel::interval(const QString &id) const
{ return get(indexOf(id)); }
bool UncertaintyModel::replaceIntervals(const QVariantList &rows)
{
    if (rows.size()>100000) { setError(QStringLiteral("Interval limit is 100000")); return false; }
    if (m_meaning==Domain && m_meaningDetail.trimmed().isEmpty()) {
        setError(QStringLiteral("Domain intervals require a meaning detail")); return false;
    }
    QVector<Interval> parsed; parsed.reserve(rows.size()); QHash<QString,int> ids;
    int missingCount=0; double previousX=0;
    for (const QVariant &entry:rows) {
        const QVariantMap row=entry.toMap(); Interval v;
        v.id=row.value("id").toString(); v.sourceId=row.value("sourceId").toString();
        if (v.id.isEmpty() || ids.contains(v.id) || v.sourceId.isEmpty()) {
            setError(QStringLiteral("Unique interval ID and sourceId required")); return false;
        }
        bool xOk=false,estimateOk=false;
        v.x=row.value("x").toDouble(&xOk);
        v.estimate=row.value("estimate").toDouble(&estimateOk);
        if (!xOk || !estimateOk || !std::isfinite(v.x) || !std::isfinite(v.estimate)
            || (!parsed.isEmpty() && !(v.x>previousX))) {
            setError(QStringLiteral("Finite x/estimate and strictly increasing x required")); return false;
        }
        const QVariant low=row.value("low"),high=row.value("high");
        const bool hasLow=low.isValid() && !low.isNull();
        const bool hasHigh=high.isValid() && !high.isNull();
        v.missing=row.value("missing").toBool() || (!hasLow && !hasHigh);
        if (hasLow!=hasHigh || (v.missing && (hasLow || hasHigh))) {
            setError(QStringLiteral("Missing intervals have neither endpoint")); return false;
        }
        if (!v.missing) {
            bool lowOk=false,highOk=false;
            v.low=low.toDouble(&lowOk); v.high=high.toDouble(&highOk);
            if (!lowOk || !highOk || !std::isfinite(v.low) || !std::isfinite(v.high) || v.low>v.high) {
                setError(QStringLiteral("Interval endpoints must be finite and ordered")); return false;
            }
        } else ++missingCount;
        previousX=v.x; ids.insert(v.id,parsed.size()); parsed.append(v);
    }
    beginResetModel(); m_rows=std::move(parsed); m_ids=std::move(ids);
    m_missingCount=missingCount; endResetModel(); setError({}); emit changed(); return true;
}
int UncertaintyModel::lowerBound(double x) const
{ return int(std::lower_bound(m_rows.cbegin(),m_rows.cend(),x,[](const Interval &v,double x){return v.x<x;})-m_rows.cbegin()); }
int UncertaintyModel::upperBound(double x) const
{ return int(std::upper_bound(m_rows.cbegin(),m_rows.cend(),x,[](double x,const Interval &v){return x<v.x;})-m_rows.cbegin()); }
QString UncertaintyModel::csvData() const
{
    QString out=QStringLiteral("datasetId,intervalId,sourceId,x,estimate,low,high,status,xUnit,yUnit,meaning,coverage,meaningDetail\n");
    for (const auto &v:m_rows) {
        const QStringList cells={m_sourceDatasetId,v.id,v.sourceId,number(v.x),number(v.estimate),
            v.missing ? QString() : number(v.low),v.missing ? QString() : number(v.high),
            v.missing ? QStringLiteral("missing") : QStringLiteral("present"),m_xUnit,m_yUnit,
            meaningName(),m_meaning==Domain ? QString() : number(m_coverage),m_meaningDetail};
        for (int i=0;i<cells.size();++i) { if (i) out+=','; out+=csvCell(cells[i]); }
        out+='\n';
    }
    return out;
}
QString UncertaintyModel::metadataJson() const
{
    QJsonObject metadata{{"schema","QindaTK.uncertainty.v1"},{"datasetId",m_sourceDatasetId},
        {"xUnit",m_xUnit},{"yUnit",m_yUnit},{"meaning",meaningName()},
        {"meaningDetail",m_meaningDetail},{"coverage",m_meaning==Domain ? QJsonValue(QJsonValue::Null) : QJsonValue(m_coverage)}};
    QJsonArray intervals;
    for (const auto &v:m_rows) intervals.append(QJsonObject{{"id",v.id},{"sourceId",v.sourceId},
        {"x",v.x},{"estimate",v.estimate},
        {"low",v.missing ? QJsonValue(QJsonValue::Null) : QJsonValue(v.low)},
        {"high",v.missing ? QJsonValue(QJsonValue::Null) : QJsonValue(v.high)},
        {"missing",v.missing}});
    metadata.insert("intervals",intervals);
    return QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
}
} // namespace QindaTK
