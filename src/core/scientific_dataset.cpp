// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_dataset.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace QindaTK {
namespace {
QString csvCell(QString value)
{ value.replace('"', "\"\""); return '"'+value+'"'; }
QString number(double value) { return QString::number(value,'g',17); }
bool numeric(const QVariant &value, double *out)
{
    if (!value.isValid() || value.isNull() || value.metaType().id()==QMetaType::QString
        || value.metaType().id()==QMetaType::Bool)
        return false;
    bool ok=false; *out=value.toDouble(&ok); return ok && std::isfinite(*out);
}
}

ScientificDataset::ScientificDataset(QObject *parent):QAbstractListModel(parent),m_intervals(this)
{
    m_intervals.setMeaning(UncertaintyModel::Domain);
    m_intervals.setMeaningDetail(QStringLiteral("Host supplied ±1σ measurement uncertainty"));
}
void ScientificDataset::setError(const QString &error)
{ if (m_lastError==error) return; m_lastError=error; emit errorChanged(); }
void ScientificDataset::setSourceId(const QString &value)
{ if (m_sourceId==value) return; m_sourceId=value; m_intervals.setSourceDatasetId(value); updateMetadata(); emit changed(); }
void ScientificDataset::setXUnit(const QString &value)
{ if (m_xUnit==value) return; m_xUnit=value; m_intervals.setXUnit(value); updateMetadata(); if (count()) emit dataChanged(index(0),index(count()-1)); emit changed(); }
void ScientificDataset::setYUnit(const QString &value)
{ if (m_yUnit==value) return; m_yUnit=value; m_intervals.setYUnit(value); updateMetadata(); if (count()) emit dataChanged(index(0),index(count()-1)); emit changed(); }
void ScientificDataset::setParameters(const QVariantMap &value)
{ if (m_parameters==value) return; m_parameters=value; updateMetadata(); emit changed(); }

bool ScientificDataset::replaceSamples(const QVariantList &rows)
{
    if (m_sourceId.trimmed().isEmpty()) { setError(QStringLiteral("sourceId is required")); return false; }
    if (rows.size()>100000) { setError(QStringLiteral("Sample limit is 100000")); return false; }
    QVector<Sample> parsed; parsed.reserve(rows.size());
    QVariantList intervals; intervals.reserve(rows.size());
    QHash<QString,int> ids; double previousX=0; int missingCount=0;
    for (const QVariant &entry:rows) {
        if (entry.metaType().id()!=QMetaType::QVariantMap) {
            setError(QStringLiteral("Each sample must be a typed map")); return false;
        }
        const QVariantMap row=entry.toMap(); Sample sample;
        sample.id=row.value(QStringLiteral("id")).toString();
        if (sample.id.isEmpty() || ids.contains(sample.id) || !numeric(row.value(QStringLiteral("x")),&sample.x)
            || (!parsed.isEmpty() && !(sample.x>previousX))) {
            setError(QStringLiteral("Unique sample IDs and strictly increasing finite numeric x required")); return false;
        }
        sample.missingY=row.value(QStringLiteral("missingY")).toBool();
        if (!sample.missingY && !numeric(row.value(QStringLiteral("y")),&sample.y)) {
            setError(QStringLiteral("Present y must be finite and numeric")); return false;
        }
        const QVariant sigma=row.value(QStringLiteral("sigmaY"));
        sample.missingSigma=!sigma.isValid() || sigma.isNull();
        if (!sample.missingY && !sample.missingSigma &&
            (!numeric(sigma,&sample.sigmaY) || sample.sigmaY<0
             || !std::isfinite(sample.y-sample.sigmaY) || !std::isfinite(sample.y+sample.sigmaY))) {
            setError(QStringLiteral("sigmaY must be finite, nonnegative and yield finite endpoints")); return false;
        }
        if (sample.missingY) ++missingCount;
        else intervals.append(QVariantMap{{QStringLiteral("id"),sample.id},
            {QStringLiteral("sourceId"),sample.id},{QStringLiteral("x"),sample.x},
            {QStringLiteral("estimate"),sample.y},
            {QStringLiteral("low"),sample.missingSigma ? QVariant() : QVariant(sample.y-sample.sigmaY)},
            {QStringLiteral("high"),sample.missingSigma ? QVariant() : QVariant(sample.y+sample.sigmaY)}});
        ids.insert(sample.id,parsed.size()); previousX=sample.x; parsed.append(sample);
    }
    // AGENT-GUARD: validate every row before mutating either representation;
    // the interval adapter receives only drawable, finite estimates.
    if (!m_intervals.replaceIntervals(intervals)) { setError(m_intervals.lastError()); return false; }
    beginResetModel(); m_samples=std::move(parsed); m_ids=std::move(ids);
    m_missingCount=missingCount; endResetModel();
    updateMetadata(); setError({}); emit changed(); return true;
}

QVariantMap ScientificDataset::sample(int index) const
{
    if (index<0 || index>=count()) return {};
    const auto &v=m_samples[index];
    return {{QStringLiteral("id"),v.id},{QStringLiteral("x"),v.x},
        {QStringLiteral("y"),v.missingY ? QVariant() : QVariant(v.y)},
        {QStringLiteral("sigmaY"),v.missingSigma || v.missingY ? QVariant() : QVariant(v.sigmaY)},
        {QStringLiteral("missingY"),v.missingY}};
}
QVariantMap ScientificDataset::sampleById(const QString &id) const
{ return sample(m_ids.value(id,-1)); }
int ScientificDataset::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : count(); }
QHash<int,QByteArray> ScientificDataset::roleNames() const
{ return {{IdRole,"id"},{XRole,"x"},{YRole,"y"},{SigmaRole,"sigmaY"},
    {MissingRole,"missingY"},{XUnitRole,"xUnit"},{YUnitRole,"yUnit"}}; }
QVariant ScientificDataset::data(const QModelIndex &index,int role) const
{
    if (!index.isValid() || index.row()<0 || index.row()>=count()) return {};
    if (role==XUnitRole) return m_xUnit;
    if (role==YUnitRole) return m_yUnit;
    return sample(index.row()).value(QString::fromLatin1(roleNames().value(role)));
}
QString ScientificDataset::csvData() const
{
    QString out=QStringLiteral("sourceId,sampleId,x,y,sigmaY,missingY,xUnit,yUnit,precision\n");
    for (const auto &v:m_samples) {
        const QStringList cells={m_sourceId,v.id,number(v.x),v.missingY ? QString() : number(v.y),
            v.missingY || v.missingSigma ? QString() : number(v.sigmaY),
            v.missingY ? QStringLiteral("true") : QStringLiteral("false"),
            m_xUnit,m_yUnit,precision()};
        for (int i=0;i<cells.size();++i) { if (i) out+=','; out+=csvCell(cells[i]); }
        out+='\n';
    }
    return out;
}
QString ScientificDataset::metadataJson() const
{ return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_intervals.figureMetadata())).toJson(QJsonDocument::Compact)); }
void ScientificDataset::updateMetadata()
{
    QJsonArray samples;
    for (const auto &v:m_samples) samples.append(QJsonObject{{QStringLiteral("id"),v.id},
        {QStringLiteral("x"),v.x},
        {QStringLiteral("y"),v.missingY ? QJsonValue(QJsonValue::Null) : QJsonValue(v.y)},
        {QStringLiteral("sigmaY"),v.missingY || v.missingSigma ? QJsonValue(QJsonValue::Null) : QJsonValue(v.sigmaY)},
        {QStringLiteral("missingY"),v.missingY}});
    const QByteArray sampleBytes=QJsonDocument(samples).toJson(QJsonDocument::Compact);
    const QString digest=QString::fromLatin1(QCryptographicHash::hash(sampleBytes,QCryptographicHash::Sha256).toHex());
    const QVariantMap metadata{{QStringLiteral("schema"),QStringLiteral("QindaTK.scientific.v1")},
        {QStringLiteral("sourceId"),m_sourceId},{QStringLiteral("shape"),shape()},
        {QStringLiteral("precision"),precision()},{QStringLiteral("xUnit"),m_xUnit},
        {QStringLiteral("yUnit"),m_yUnit},{QStringLiteral("uncertainty"),QStringLiteral("host supplied 1 sigma")},
        {QStringLiteral("missingYCount"),m_missingCount},{QStringLiteral("parameters"),m_parameters},
        {QStringLiteral("samplesSha256"),digest},{QStringLiteral("samples"),samples.toVariantList()}};
    m_intervals.setFigureMetadata(metadata);
}
} // namespace QindaTK
