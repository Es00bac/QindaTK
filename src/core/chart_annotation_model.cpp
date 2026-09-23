// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_annotation_model.h"

#include <QSet>
#include <cmath>

namespace QindaTK {

ChartAnnotationModel::ChartAnnotationModel(QObject *parent) : QAbstractListModel(parent) {}
int ChartAnnotationModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_annotations.size(); }
QHash<int, QByteArray> ChartAnnotationModel::roleNames() const
{
    return {{IdRole, "id"}, {LabelRole, "label"}, {KindRole, "kind"},
            {XRole, "x"}, {UtcRole, "utcMs"}, {CategoryRole, "category"},
            {YRole, "y"}, {XUnitRole, "xUnit"}, {YUnitRole, "yUnit"}};
}
QVariant ChartAnnotationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    return get(index.row()).value(QString::fromLatin1(roleNames().value(role)));
}
QVariantMap ChartAnnotationModel::get(int row) const
{
    if (row < 0 || row >= count()) return {};
    const auto &a = m_annotations.at(row);
    return {{QStringLiteral("id"), a.id}, {QStringLiteral("label"), a.label},
            {QStringLiteral("kind"), int(a.kind)},
            {QStringLiteral("x"), a.kind == ChartAnnotation::HorizontalLine || a.hasUtc || a.hasCategory
                ? QVariant{} : QVariant(a.x)},
            {QStringLiteral("utcMs"), a.hasUtc ? QVariant(QString::number(a.utcMs)) : QVariant{}},
            {QStringLiteral("category"), a.hasCategory ? QVariant(a.category) : QVariant{}},
            {QStringLiteral("y"), a.kind == ChartAnnotation::VerticalLine ? QVariant{} : QVariant(a.y)},
            {QStringLiteral("xUnit"), a.xUnit}, {QStringLiteral("yUnit"), a.yUnit}};
}
void ChartAnnotationModel::setError(const QString &error)
{ if (m_error != error) { m_error = error; emit errorChanged(); } }
bool ChartAnnotationModel::replaceAnnotations(const QVariantList &rows)
{
    if (rows.size() > 1024) { setError(QStringLiteral("Chart annotations exceed 1024 records")); return false; }
    QVector<ChartAnnotation> proposed; proposed.reserve(rows.size());
    QSet<QString> ids;
    for (const QVariant &value : rows) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            setError(QStringLiteral("Every annotation must be a record")); return false;
        }
        const QVariantMap row = value.toMap();
        ChartAnnotation a;
        a.id = row.value(QStringLiteral("id")).toString();
        a.label = row.value(QStringLiteral("label")).toString();
        a.xUnit = row.value(QStringLiteral("xUnit")).toString();
        a.yUnit = row.value(QStringLiteral("yUnit")).toString();
        if (a.id.isEmpty() || a.id.size() > 128 || ids.contains(a.id)
            || a.label.size() > 256 || a.xUnit.size() > 128 || a.yUnit.size() > 128) {
            setError(QStringLiteral("Annotation IDs must be unique and fields bounded")); return false;
        }
        ids.insert(a.id);
        bool kindOk = false;
        const int kind = row.value(QStringLiteral("kind"), int(Point)).toInt(&kindOk);
        if (!kindOk || kind < Point || kind > HorizontalLine) {
            setError(QStringLiteral("Unknown annotation kind")); return false;
        }
        a.kind = ChartAnnotation::Kind(kind);
        const bool hasX = row.contains(QStringLiteral("x"));
        a.hasUtc = row.contains(QStringLiteral("utcMs"));
        a.hasCategory = row.contains(QStringLiteral("category"));
        const int xKinds = int(hasX) + int(a.hasUtc) + int(a.hasCategory);
        if (xKinds != (a.kind == ChartAnnotation::HorizontalLine ? 0 : 1)
            || row.contains(QStringLiteral("y")) != (a.kind != ChartAnnotation::VerticalLine)) {
            setError(QStringLiteral("Annotation requires exactly its declared coordinates")); return false;
        }
        if (hasX) {
            bool ok = false; a.x = row.value(QStringLiteral("x")).toDouble(&ok);
            if (!ok || !std::isfinite(a.x) || std::abs(a.x) > 1e12) {
                setError(QStringLiteral("Annotation X must be finite and bounded")); return false;
            }
        }
        if (a.hasUtc) {
            const QVariant utc = row.value(QStringLiteral("utcMs"));
            bool ok = false;
            if (utc.metaType().id() != QMetaType::QString) {
                setError(QStringLiteral("Annotation UTC milliseconds must be a decimal string")); return false;
            }
            a.utcMs = utc.toString().toLongLong(&ok);
            if (!ok) { setError(QStringLiteral("Annotation UTC milliseconds are invalid")); return false; }
        }
        if (a.hasCategory) {
            a.category = row.value(QStringLiteral("category")).toString();
            if (a.category.isEmpty() || a.category.size() > 128) {
                setError(QStringLiteral("Annotation category is invalid")); return false;
            }
        }
        if (a.kind != ChartAnnotation::VerticalLine) {
            bool ok = false; a.y = row.value(QStringLiteral("y")).toDouble(&ok);
            if (!ok || !std::isfinite(a.y) || std::abs(a.y) > 1e12) {
                setError(QStringLiteral("Annotation Y must be finite and bounded")); return false;
            }
        }
        proposed.append(std::move(a));
    }
    beginResetModel(); m_annotations = std::move(proposed); endResetModel();
    setError({}); emit annotationsChanged(); return true;
}

} // namespace QindaTK
