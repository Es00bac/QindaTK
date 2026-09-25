// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_dataset.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
QString csvCell(QString value)
{ value.replace('"', "\"\""); return '"'+value+'"'; }
QString number(double value) { return QString::number(value,'g',17); }
bool present(const QVariant &value) { return value.isValid() && !value.isNull(); }
bool numeric(const QVariant &value, double *out)
{
    if (!present(value) || value.metaType().id()==QMetaType::QString
        || value.metaType().id()==QMetaType::Bool)
        return false;
    bool ok=false; *out=value.toDouble(&ok); return ok && std::isfinite(*out);
}
// Reads either a symmetric `name` or a `nameLow`+`nameHigh` pair.
bool offsets(const QVariantMap &row, const QString &name, double *low, double *high,
             bool *has, bool *symmetric, QString *error)
{
    const QVariant both=row.value(name), lo=row.value(name+QStringLiteral("Low")),
        hi=row.value(name+QStringLiteral("High"));
    *has=false; *symmetric=true; *low=*high=0;
    if (present(both) && (present(lo) || present(hi))) {
        *error=QStringLiteral("%1 cannot be combined with %1Low/%1High").arg(name); return false;
    }
    if (present(lo)!=present(hi)) {
        *error=QStringLiteral("%1Low and %1High are required together").arg(name); return false;
    }
    if (present(both)) {
        if (!numeric(both,low) || *low<0) {
            *error=QStringLiteral("%1 must be finite and nonnegative").arg(name); return false;
        }
        *high=*low; *has=true; return true;
    }
    if (present(lo)) {
        if (!numeric(lo,low) || !numeric(hi,high) || *low<0 || *high<0) {
            *error=QStringLiteral("%1Low/%1High must be finite and nonnegative").arg(name); return false;
        }
        *has=true; *symmetric=false;
    }
    return true;
}
QJsonValue maybe(bool has, double value) { return has ? QJsonValue(value) : QJsonValue(QJsonValue::Null); }
QVariant maybeVariant(bool has, double value) { return has ? QVariant(value) : QVariant(); }
}

ScientificDataset::ScientificDataset(QObject *parent):QAbstractListModel(parent),m_intervals(this)
{
    m_intervals.setMeaning(UncertaintyModel::Domain);
    m_intervals.setMeaningDetail(QStringLiteral("Host supplied ±1σ measurement uncertainty"));
    updateMetadata();
}
void ScientificDataset::setError(const QString &error)
{ if (m_lastError==error) return; m_lastError=error; emit errorChanged(); }
void ScientificDataset::setSourceId(const QString &value)
{ if (m_sourceId==value) return; m_sourceId=value; m_intervals.setSourceDatasetId(value); updateMetadata(); emit changed(); }
void ScientificDataset::setLabel(const QString &value)
{ if (m_label==value) return; m_label=value; updateMetadata(); emit changed(); }
void ScientificDataset::setKind(Kind value)
{
    // AGENT-GUARD: kind changes the ordering contract, so existing samples
    // are only kept when they still satisfy it; otherwise refuse.
    if (m_kind==value) return;
    if (value==Trace)
        for (int i=1;i<m_samples.size();++i)
            if (!(m_samples[i].x>m_samples[i-1].x)) {
                setError(QStringLiteral("Trace requires strictly increasing x; replace samples first")); return;
            }
    m_kind=value; updateMetadata(); emit changed();
}
void ScientificDataset::setDimensions(int value)
{
    if (m_dimensions==value) return;
    if ((value!=2 && value!=3) || !m_samples.isEmpty()) {
        setError(QStringLiteral("Dimensions are 2 or 3 and are declared before samples")); return;
    }
    m_dimensions=value; updateMetadata(); emit changed();
}
void ScientificDataset::setXUnit(const QString &value)
{ if (m_xUnit==value) return; m_xUnit=value; m_intervals.setXUnit(value); updateMetadata(); if (count()) emit dataChanged(index(0),index(count()-1)); emit changed(); }
void ScientificDataset::setYUnit(const QString &value)
{ if (m_yUnit==value) return; m_yUnit=value; m_intervals.setYUnit(value); updateMetadata(); if (count()) emit dataChanged(index(0),index(count()-1)); emit changed(); }
void ScientificDataset::setZUnit(const QString &value)
{ if (m_zUnit==value) return; m_zUnit=value; updateMetadata(); if (count()) emit dataChanged(index(0),index(count()-1)); emit changed(); }
void ScientificDataset::setParameters(const QVariantMap &value)
{ if (m_parameters==value) return; m_parameters=value; updateMetadata(); emit changed(); }

bool ScientificDataset::parseRow(const QVariantMap &row, ScientificSample *s, QString *error) const
{
    s->id=row.value(QStringLiteral("id")).toString();
    if (s->id.isEmpty() || !numeric(row.value(QStringLiteral("x")),&s->x)) {
        *error=QStringLiteral("Every sample needs an id and a finite numeric x"); return false;
    }
    s->missingY=row.value(QStringLiteral("missingY")).toBool();
    if (!s->missingY && !numeric(row.value(QStringLiteral("y")),&s->y)) {
        *error=QStringLiteral("Present y must be finite and numeric; declare missingY instead"); return false;
    }
    const QVariant z=row.value(QStringLiteral("z"));
    if (m_dimensions==2 && present(z)) {
        *error=QStringLiteral("z requires dimensions 3"); return false;
    }
    if (m_dimensions==3 && !s->missingY && !numeric(z,&s->z)) {
        *error=QStringLiteral("Present 3D samples need a finite z"); return false;
    }
    if (!offsets(row,QStringLiteral("sigmaY"),&s->sigmaYLow,&s->sigmaYHigh,&s->hasSigmaY,&s->symmetricY,error)
        || !offsets(row,QStringLiteral("sigmaX"),&s->sigmaXLow,&s->sigmaXHigh,&s->hasSigmaX,&s->symmetricX,error))
        return false;
    if (s->missingY) { s->hasSigmaY=false; s->sigmaYLow=s->sigmaYHigh=0; }
    if (!std::isfinite(s->y-s->sigmaYLow) || !std::isfinite(s->y+s->sigmaYHigh)
        || !std::isfinite(s->x-s->sigmaXLow) || !std::isfinite(s->x+s->sigmaXHigh)) {
        *error=QStringLiteral("Uncertainty endpoints must be finite"); return false;
    }
    return true;
}

bool ScientificDataset::replaceSamples(const QVariantList &rows)
{
    if (m_sourceId.trimmed().isEmpty()) { setError(QStringLiteral("sourceId is required")); return false; }
    if (rows.size()>SampleLimit) { setError(QStringLiteral("Sample limit is 100000")); return false; }
    QVector<ScientificSample> parsed; parsed.reserve(rows.size());
    QVariantList intervals; if (m_kind==Trace) intervals.reserve(rows.size());
    QHash<QString,int> ids; int missingCount=0; QString error;
    for (const QVariant &entry:rows) {
        if (entry.metaType().id()!=QMetaType::QVariantMap) {
            setError(QStringLiteral("Each sample must be a typed map")); return false;
        }
        ScientificSample sample;
        if (!parseRow(entry.toMap(),&sample,&error)) { setError(error); return false; }
        if (ids.contains(sample.id) || (m_kind==Trace && !parsed.isEmpty() && !(sample.x>parsed.last().x))) {
            setError(QStringLiteral("Unique sample IDs and strictly increasing finite numeric x required")); return false;
        }
        if (sample.missingY) ++missingCount;
        else if (m_kind==Trace) intervals.append(QVariantMap{{QStringLiteral("id"),sample.id},
            {QStringLiteral("sourceId"),sample.id},{QStringLiteral("x"),sample.x},
            {QStringLiteral("estimate"),sample.y},
            {QStringLiteral("low"),maybeVariant(sample.hasSigmaY,sample.y-sample.sigmaYLow)},
            {QStringLiteral("high"),maybeVariant(sample.hasSigmaY,sample.y+sample.sigmaYHigh)}});
        ids.insert(sample.id,parsed.size()); parsed.append(sample);
    }
    // AGENT-GUARD: validate every row before mutating either representation;
    // the interval adapter receives only drawable, finite estimates.
    if (!m_intervals.replaceIntervals(intervals)) { setError(m_intervals.lastError()); return false; }
    beginResetModel(); m_samples=std::move(parsed); m_ids=std::move(ids);
    m_missingCount=missingCount; endResetModel();
    updateMetadata(); setError({}); emit changed(); return true;
}

int ScientificDataset::lowerBound(double x) const
{ return int(std::lower_bound(m_samples.cbegin(),m_samples.cend(),x,
        [](const ScientificSample &v,double value){return v.x<value;})-m_samples.cbegin()); }
int ScientificDataset::upperBound(double x) const
{ return int(std::upper_bound(m_samples.cbegin(),m_samples.cend(),x,
        [](double value,const ScientificSample &v){return value<v.x;})-m_samples.cbegin()); }

QVariantMap ScientificDataset::sample(int index) const
{
    if (index<0 || index>=count()) return {};
    const auto &v=m_samples[index];
    const bool sy=v.hasSigmaY && !v.missingY;
    QVariantMap out{{QStringLiteral("id"),v.id},{QStringLiteral("x"),v.x},
        {QStringLiteral("y"),v.missingY ? QVariant() : QVariant(v.y)},
        {QStringLiteral("sigmaY"),maybeVariant(sy && v.symmetricY,v.sigmaYLow)},
        {QStringLiteral("sigmaYLow"),maybeVariant(sy,v.sigmaYLow)},
        {QStringLiteral("sigmaYHigh"),maybeVariant(sy,v.sigmaYHigh)},
        {QStringLiteral("sigmaXLow"),maybeVariant(v.hasSigmaX,v.sigmaXLow)},
        {QStringLiteral("sigmaXHigh"),maybeVariant(v.hasSigmaX,v.sigmaXHigh)},
        {QStringLiteral("missingY"),v.missingY},{QStringLiteral("index"),index}};
    if (m_dimensions==3) out.insert(QStringLiteral("z"),maybeVariant(!v.missingY,v.z));
    return out;
}
QVariantMap ScientificDataset::sampleById(const QString &id) const
{ return sample(indexOf(id)); }
QVariantList ScientificDataset::samplesInRange(double xMin, double xMax, int limit) const
{
    QVariantList out;
    if (!std::isfinite(xMin) || !std::isfinite(xMax) || xMax<xMin) return out;
    limit=std::clamp(limit,0,10000);
    if (m_kind==Trace) {
        for (int i=lowerBound(xMin),end=upperBound(xMax);i<end && out.size()<limit;++i) out.append(sample(i));
    } else {
        for (int i=0;i<count() && out.size()<limit;++i)
            if (m_samples[i].x>=xMin && m_samples[i].x<=xMax) out.append(sample(i));
    }
    return out;
}
QVariantMap ScientificDataset::extent() const
{
    constexpr double inf=std::numeric_limits<double>::infinity();
    double x0=inf,x1=-inf,y0=inf,y1=-inf,z0=inf,z1=-inf; int n=0;
    for (const auto &v:m_samples) {
        x0=std::min(x0,v.x-v.sigmaXLow); x1=std::max(x1,v.x+v.sigmaXHigh);
        if (v.missingY) continue;
        ++n; y0=std::min(y0,v.y-v.sigmaYLow); y1=std::max(y1,v.y+v.sigmaYHigh);
        z0=std::min(z0,v.z); z1=std::max(z1,v.z);
    }
    if (!n) return {{QStringLiteral("valid"),false}};
    QVariantMap out{{QStringLiteral("valid"),true},{QStringLiteral("xMinimum"),x0},{QStringLiteral("xMaximum"),x1},
        {QStringLiteral("yMinimum"),y0},{QStringLiteral("yMaximum"),y1},{QStringLiteral("presentCount"),n}};
    if (m_dimensions==3) { out.insert(QStringLiteral("zMinimum"),z0); out.insert(QStringLiteral("zMaximum"),z1); }
    return out;
}
int ScientificDataset::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : count(); }
QHash<int,QByteArray> ScientificDataset::roleNames() const
{ return {{IdRole,"id"},{XRole,"x"},{YRole,"y"},{SigmaRole,"sigmaY"},
    {MissingRole,"missingY"},{XUnitRole,"xUnit"},{YUnitRole,"yUnit"},
    {SigmaYLowRole,"sigmaYLow"},{SigmaYHighRole,"sigmaYHigh"},{SigmaXLowRole,"sigmaXLow"},
    {SigmaXHighRole,"sigmaXHigh"},{ZRole,"z"},{ZUnitRole,"zUnit"}}; }
QVariant ScientificDataset::data(const QModelIndex &index,int role) const
{
    if (!index.isValid() || index.row()<0 || index.row()>=count()) return {};
    if (role==XUnitRole) return m_xUnit;
    if (role==YUnitRole) return m_yUnit;
    if (role==ZUnitRole) return m_zUnit;
    return sample(index.row()).value(QString::fromLatin1(roleNames().value(role)));
}
QString ScientificDataset::csvData() const
{
    // AGENT-NOTE: the first six columns keep the WP-085 first-slice order.
    QString out=QStringLiteral("sourceId,sampleId,x,y,sigmaY,missingY,sigmaYLow,sigmaYHigh,"
                               "sigmaXLow,sigmaXHigh,z,xUnit,yUnit,zUnit,precision\n");
    for (const auto &v:m_samples) {
        const bool sy=v.hasSigmaY && !v.missingY;
        const QStringList cells={m_sourceId,v.id,number(v.x),v.missingY ? QString() : number(v.y),
            sy && v.symmetricY ? number(v.sigmaYLow) : QString(),
            v.missingY ? QStringLiteral("true") : QStringLiteral("false"),
            sy ? number(v.sigmaYLow) : QString(),sy ? number(v.sigmaYHigh) : QString(),
            v.hasSigmaX ? number(v.sigmaXLow) : QString(),v.hasSigmaX ? number(v.sigmaXHigh) : QString(),
            m_dimensions==3 && !v.missingY ? number(v.z) : QString(),
            m_xUnit,m_yUnit,m_zUnit,precision()};
        for (int i=0;i<cells.size();++i) { if (i) out+=','; out+=csvCell(cells[i]); }
        out+='\n';
    }
    return out;
}
QString ScientificDataset::metadataJson() const
{ return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_metadata)).toJson(QJsonDocument::Compact)); }
void ScientificDataset::updateMetadata()
{
    QJsonArray samples;
    for (const auto &v:m_samples) {
        const bool sy=v.hasSigmaY && !v.missingY;
        QJsonObject row{{QStringLiteral("id"),v.id},{QStringLiteral("x"),v.x},
            {QStringLiteral("y"),v.missingY ? QJsonValue(QJsonValue::Null) : QJsonValue(v.y)},
            {QStringLiteral("sigmaY"),maybe(sy && v.symmetricY,v.sigmaYLow)},
            {QStringLiteral("missingY"),v.missingY}};
        if (sy && !v.symmetricY) {
            row.insert(QStringLiteral("sigmaYLow"),v.sigmaYLow); row.insert(QStringLiteral("sigmaYHigh"),v.sigmaYHigh);
        }
        if (v.hasSigmaX) {
            row.insert(QStringLiteral("sigmaXLow"),v.sigmaXLow); row.insert(QStringLiteral("sigmaXHigh"),v.sigmaXHigh);
        }
        if (m_dimensions==3) row.insert(QStringLiteral("z"),maybe(!v.missingY,v.z));
        samples.append(row);
    }
    const QByteArray sampleBytes=QJsonDocument(samples).toJson(QJsonDocument::Compact);
    const QString digest=QString::fromLatin1(QCryptographicHash::hash(sampleBytes,QCryptographicHash::Sha256).toHex());
    m_metadata={{QStringLiteral("schema"),QStringLiteral("QindaTK.scientific.v1")},
        {QStringLiteral("sourceId"),m_sourceId},{QStringLiteral("label"),label()},
        {QStringLiteral("kind"),m_kind==Trace ? QStringLiteral("trace") : QStringLiteral("scatter")},
        {QStringLiteral("shape"),shape()},{QStringLiteral("dimensions"),m_dimensions},
        {QStringLiteral("precision"),precision()},{QStringLiteral("xUnit"),m_xUnit},
        {QStringLiteral("yUnit"),m_yUnit},{QStringLiteral("zUnit"),m_zUnit},
        {QStringLiteral("uncertainty"),QStringLiteral("host supplied 1 sigma offsets")},
        {QStringLiteral("missingYCount"),m_missingCount},{QStringLiteral("parameters"),m_parameters},
        {QStringLiteral("samplesSha256"),digest},{QStringLiteral("samples"),samples.toVariantList()}};
    m_intervals.setFigureMetadata(m_metadata);
}
} // namespace QindaTK
