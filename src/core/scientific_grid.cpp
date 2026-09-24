// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_grid.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
QString number(double value) { return QString::number(value, 'g', 17); }
QString csvCell(QString value) { value.replace('"', "\"\""); return '"' + value + '"'; }
bool finiteNumber(const QVariant &value, double *out)
{
    if (!value.isValid() || value.isNull() || value.metaType().id() == QMetaType::QString
        || value.metaType().id() == QMetaType::Bool)
        return false;
    bool ok = false; *out = value.toDouble(&ok); return ok && std::isfinite(*out);
}
bool axis(const QVariantList &in, QVector<double> *out)
{
    if (in.size() < 2 || in.size() > ScientificGrid::AxisLimit) return false;
    out->reserve(in.size());
    for (const QVariant &value : in) {
        double v = 0;
        if (!finiteNumber(value, &v) || (!out->isEmpty() && !(v > out->last()))) return false;
        out->append(v);
    }
    return true;
}
// Index i with xs[i] <= v <= xs[i+1] and the fraction t in [0,1]; false outside.
bool locate(const QVector<double> &xs, double v, int *i, double *t)
{
    if (!(v >= xs.first() && v <= xs.last())) return false;
    int hi = int(std::upper_bound(xs.cbegin(), xs.cend(), v) - xs.cbegin());
    hi = std::clamp(hi, 1, int(xs.size()) - 1);
    *i = hi - 1;
    *t = (v - xs[*i]) / (xs[hi] - xs[*i]);
    return true;
}
}

ScientificGrid::ScientificGrid(QObject *parent) : QAbstractListModel(parent) {}
void ScientificGrid::setError(const QString &value)
{ if (m_lastError == value) return; m_lastError = value; emit errorChanged(); }
#define GRID_SETTER(Name, Field, Type) \
    void ScientificGrid::set##Name(const Type &value) \
    { \
        if (Field == value) return; \
        Field = value; \
        if (count()) emit dataChanged(index(0), index(count() - 1)); \
        emit changed(); \
    }
GRID_SETTER(SourceId, m_sourceId, QString)
GRID_SETTER(Label, m_label, QString)
GRID_SETTER(XUnit, m_xUnit, QString)
GRID_SETTER(YUnit, m_yUnit, QString)
GRID_SETTER(ZUnit, m_zUnit, QString)
GRID_SETTER(Parameters, m_parameters, QVariantMap)
#undef GRID_SETTER

bool ScientificGrid::replaceGrid(const QVariantList &xs, const QVariantList &ys, const QVariantList &values)
{
    if (m_sourceId.trimmed().isEmpty()) { setError(QStringLiteral("sourceId is required")); return false; }
    QVector<double> px, py, pv;
    if (!axis(xs, &px) || !axis(ys, &py)) {
        setError(QStringLiteral("Grid axes need 2 to 4096 finite, strictly increasing coordinates")); return false;
    }
    if (qint64(px.size()) * py.size() > NodeLimit || values.size() != px.size() * py.size()) {
        setError(QStringLiteral("Values must be rows x columns (at most 1048576 nodes)")); return false;
    }
    pv.reserve(values.size());
    int missing = 0;
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const QVariant &value : values) {
        if (!value.isValid() || value.isNull()) { pv.append(NaN); ++missing; continue; }
        double v = 0;
        if (!finiteNumber(value, &v)) {
            setError(QStringLiteral("Grid values must be finite numbers or null (missing)")); return false;
        }
        lo = std::min(lo, v); hi = std::max(hi, v); pv.append(v);
    }
    beginResetModel();
    m_xs = std::move(px); m_ys = std::move(py); m_values = std::move(pv); m_missing = missing;
    m_zMin = missing == values.size() ? 0 : lo; m_zMax = missing == values.size() ? 0 : hi;
    endResetModel();
    setError({}); emit changed(); return true;
}

double ScientificGrid::sampleAt(double x, double y, Interpolation mode) const
{
    if (m_values.isEmpty() || !std::isfinite(x) || !std::isfinite(y)) return NaN;
    int c = 0, r = 0; double tx = 0, ty = 0;
    if (!locate(m_xs, x, &c, &tx) || !locate(m_ys, y, &r, &ty)) return NaN;
    if (mode == Nearest) {
        // AGENT-NOTE: an exact midpoint goes to the lower index, so results
        // are deterministic across platforms.
        return at(ty > 0.5 ? r + 1 : r, tx > 0.5 ? c + 1 : c);
    }
    double sum = 0;
    const double weights[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
    const int nodes[4][2] = {{r, c}, {r, c + 1}, {r + 1, c}, {r + 1, c + 1}};
    for (int k = 0; k < 4; ++k) {
        if (weights[k] == 0) continue;
        const double v = at(nodes[k][0], nodes[k][1]);
        if (std::isnan(v)) return NaN;
        sum += weights[k] * v;
    }
    return sum;
}

QVariantMap ScientificGrid::node(int row, int column) const
{
    if (row < 0 || column < 0 || row >= rows() || column >= columns()) return {};
    const double v = at(row, column);
    return {{QStringLiteral("id"), QStringLiteral("r%1c%2").arg(row).arg(column)},
            {QStringLiteral("row"), row}, {QStringLiteral("column"), column},
            {QStringLiteral("x"), m_xs[column]}, {QStringLiteral("y"), m_ys[row]},
            {QStringLiteral("z"), std::isnan(v) ? QVariant() : QVariant(v)},
            {QStringLiteral("missing"), std::isnan(v)}};
}

QVariantMap ScientificGrid::interpolate(double x, double y, Interpolation mode) const
{
    const bool inside = !m_values.isEmpty() && x >= m_xs.first() && x <= m_xs.last()
        && y >= m_ys.first() && y <= m_ys.last();
    const double v = sampleAt(x, y, mode);
    return {{QStringLiteral("inside"), inside}, {QStringLiteral("missing"), std::isnan(v)},
            {QStringLiteral("z"), std::isnan(v) ? QVariant() : QVariant(v)},
            {QStringLiteral("interpolation"), mode == Nearest ? QStringLiteral("nearest") : QStringLiteral("bilinear")},
            {QStringLiteral("zUnit"), m_zUnit}};
}

QVariantMap ScientificGrid::window(int firstRow, int lastRow, int firstColumn, int lastColumn,
                                   int maxRows, int maxColumns) const
{
    if (m_values.isEmpty()) return {};
    firstRow = std::clamp(firstRow, 0, rows() - 1); lastRow = std::clamp(lastRow, firstRow, rows() - 1);
    firstColumn = std::clamp(firstColumn, 0, columns() - 1);
    lastColumn = std::clamp(lastColumn, firstColumn, columns() - 1);
    maxRows = std::clamp(maxRows, 1, 512); maxColumns = std::clamp(maxColumns, 1, 512);
    const int spanR = lastRow - firstRow + 1, spanC = lastColumn - firstColumn + 1;
    const int strideR = (spanR + maxRows - 1) / maxRows, strideC = (spanC + maxColumns - 1) / maxColumns;
    QVariantList values, xs, ys;
    for (int c = firstColumn; c <= lastColumn; c += strideC) xs.append(m_xs[c]);
    for (int r = firstRow; r <= lastRow; r += strideR) {
        ys.append(m_ys[r]);
        QVariantList line;
        for (int c = firstColumn; c <= lastColumn; c += strideC) {
            const double v = at(r, c);
            line.append(std::isnan(v) ? QVariant() : QVariant(v));
        }
        values.append(QVariant(line));
    }
    return {{QStringLiteral("rowStride"), strideR}, {QStringLiteral("columnStride"), strideC},
            {QStringLiteral("rows"), ys.size()}, {QStringLiteral("columns"), xs.size()},
            {QStringLiteral("xs"), xs}, {QStringLiteral("ys"), ys}, {QStringLiteral("values"), values},
            {QStringLiteral("method"), QStringLiteral("stride-nodes")}};
}

int ScientificGrid::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }
QHash<int, QByteArray> ScientificGrid::roleNames() const
{
    return {{IdRole, "id"}, {RowRole, "row"}, {ColumnRole, "column"}, {XRole, "x"}, {YRole, "y"},
            {ZRole, "z"}, {MissingRole, "missing"}, {XUnitRole, "xUnit"}, {YUnitRole, "yUnit"},
            {ZUnitRole, "zUnit"}};
}
QVariant ScientificGrid::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    if (role == XUnitRole) return m_xUnit;
    if (role == YUnitRole) return m_yUnit;
    if (role == ZUnitRole) return m_zUnit;
    return node(index.row() / columns(), index.row() % columns())
        .value(QString::fromLatin1(roleNames().value(role)));
}

QString ScientificGrid::csvData() const
{
    QString out = QStringLiteral("sourceId,row,column,x,y,z,missing,xUnit,yUnit,zUnit,precision\n");
    for (int r = 0; r < rows(); ++r)
        for (int c = 0; c < columns(); ++c) {
            const double v = at(r, c);
            const QStringList cells = {m_sourceId, QString::number(r), QString::number(c), number(m_xs[c]),
                number(m_ys[r]), std::isnan(v) ? QString() : number(v),
                std::isnan(v) ? QStringLiteral("true") : QStringLiteral("false"),
                m_xUnit, m_yUnit, m_zUnit, precision()};
            for (int i = 0; i < cells.size(); ++i) { if (i) out += ','; out += csvCell(cells[i]); }
            out += '\n';
        }
    return out;
}

QString ScientificGrid::digest() const
{
    // AGENT-NOTE: hashed as canonical JSON (null for missing) so the digest
    // is independent of byte order and matches the exported sidecar text.
    QJsonArray xs, ys, values;
    for (double v : m_xs) xs.append(v);
    for (double v : m_ys) ys.append(v);
    for (double v : m_values) values.append(std::isnan(v) ? QJsonValue(QJsonValue::Null) : QJsonValue(v));
    const QByteArray bytes = QJsonDocument(QJsonArray{xs, ys, values}).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QVariantMap ScientificGrid::metadata() const
{
    return {{QStringLiteral("schema"), QStringLiteral("QindaTK.scientificGrid.v1")},
            {QStringLiteral("sourceId"), m_sourceId}, {QStringLiteral("label"), label()},
            {QStringLiteral("shape"), shape()}, {QStringLiteral("precision"), precision()},
            {QStringLiteral("xUnit"), m_xUnit}, {QStringLiteral("yUnit"), m_yUnit},
            {QStringLiteral("zUnit"), m_zUnit}, {QStringLiteral("missingCount"), m_missing},
            {QStringLiteral("zMinimum"), m_zMin}, {QStringLiteral("zMaximum"), m_zMax},
            {QStringLiteral("parameters"), m_parameters}, {QStringLiteral("valuesSha256"), digest()}};
}
QString ScientificGrid::metadataJson() const
{ return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(metadata())).toJson(QJsonDocument::Compact)); }

} // namespace QindaTK
