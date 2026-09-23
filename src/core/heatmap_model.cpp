// SPDX-License-Identifier: LGPL-3.0-or-later
#include "heatmap_model.h"

#include <QTimer>
#include <QSet>
#include <cmath>
#include <limits>

namespace QindaTK {
HeatmapModel::HeatmapModel(QObject *parent) : QObject(parent) {}
void HeatmapModel::touch() { ++m_revision; ++m_request; emit changed(); }
#define SETTER(Type, Name, Field) void HeatmapModel::set##Name(Type v) { if (m_##Field == v) return; m_##Field = v; touch(); }
SETTER(double, XMinimum, xMin)
SETTER(double, XMaximum, xMax)
SETTER(double, YMinimum, yMin)
SETTER(double, YMaximum, yMax)
SETTER(double, ColorMinimum, colorMin)
SETTER(double, ColorMaximum, colorMax)
SETTER(const QString &, XUnit, xUnit)
SETTER(const QString &, YUnit, yUnit)
SETTER(const QString &, ValueUnit, valueUnit)
SETTER(const QStringList &, XCategories, xCategories)
SETTER(const QStringList &, YCategories, yCategories)
SETTER(const QStringList &, ValueCategories, valueCategories)
#undef SETTER

QString HeatmapModel::error() const
{
    if (!m_rows || !m_columns) return QStringLiteral("Matrix dimensions must be positive");
    const auto uniqueLabels = [](const QStringList &labels, int expected) {
        QSet<QString> values;
        for (const QString &label : labels) values.insert(label);
        return labels.size() == expected && values.size() == expected && !values.contains(QString());
    };
    if (m_xCategories.isEmpty()) {
        if (!std::isfinite(m_xMin) || !std::isfinite(m_xMax) || m_xMax <= m_xMin)
            return QStringLiteral("Invalid X domain");
    } else if (!uniqueLabels(m_xCategories, m_columns)) return QStringLiteral("Invalid X categories");
    if (m_yCategories.isEmpty()) {
        if (!std::isfinite(m_yMin) || !std::isfinite(m_yMax) || m_yMax <= m_yMin)
            return QStringLiteral("Invalid Y domain");
    } else if (!uniqueLabels(m_yCategories, m_rows)) return QStringLiteral("Invalid Y categories");
    if (!m_valueCategories.isEmpty()) {
        QSet<QString> values;
        for (const QString &category : m_valueCategories) values.insert(category);
        if (values.size() != m_valueCategories.size() || values.contains(QString()))
            return QStringLiteral("Invalid value categories");
    } else if (!std::isfinite(m_colorMin) || !std::isfinite(m_colorMax) || m_colorMax <= m_colorMin)
        return QStringLiteral("Invalid color scale");
    return {};
}

bool HeatmapModel::setDimensions(int rows, int columns)
{
    if (rows < 0 || columns < 0 || quint64(rows) * quint64(columns) > 1000000000ULL) return false;
    if (m_rows == rows && m_columns == columns) return true;
    m_rows = rows; m_columns = columns; m_cells.clear(); m_ids.clear(); touch();
    return true;
}

bool HeatmapModel::setCell(int row, int column, const QString &id, const QVariant &value)
{
    if (row < 0 || row >= m_rows || column < 0 || column >= m_columns || id.isEmpty()) return false;
    const quint64 k = key(row, column);
    if (m_ids.contains(id) && m_ids.value(id) != k) return false;
    if (value.isValid() && !value.isNull()) {
        if (m_valueCategories.isEmpty()) {
            bool ok = false; const double numeric = value.toDouble(&ok);
            if (!ok || !std::isfinite(numeric)) return false;
        } else if (!m_valueCategories.contains(value.toString())) return false;
    }
    auto old = m_cells.constFind(k);
    if (old != m_cells.cend()) m_ids.remove(old->id);
    m_cells[k] = {id, value}; m_ids[id] = k; touch();
    return true;
}

QVariantMap HeatmapModel::record(int row, int column, const Cell *cell) const
{
    const bool missing = !cell || !cell->value.isValid() || cell->value.isNull();
    const QString x = m_xCategories.isEmpty()
        ? QString::number(m_xMin + (column + 0.5) * (m_xMax - m_xMin) / m_columns, 'g', 15)
        : m_xCategories.value(column);
    const QString y = m_yCategories.isEmpty()
        ? QString::number(m_yMax - (row + 0.5) * (m_yMax - m_yMin) / m_rows, 'g', 15)
        : m_yCategories.value(row);
    return {{"row", row}, {"column", column}, {"id", cell ? cell->id : QStringLiteral("%1:%2").arg(row).arg(column)},
            {"value", missing ? QVariant() : cell->value}, {"missing", missing},
            {"x", x}, {"y", y}, {"xUnit", m_xUnit}, {"yUnit", m_yUnit}, {"valueUnit", m_valueUnit}};
}
QVariantMap HeatmapModel::cellAt(int row, int column) const
{
    if (row < 0 || row >= m_rows || column < 0 || column >= m_columns) return {};
    auto it = m_cells.constFind(key(row, column));
    return record(row, column, it == m_cells.cend() ? nullptr : &it.value());
}

QVariantList HeatmapModel::tile(int firstRow, int lastRow, int firstColumn,
                                int lastColumn, int binsY, int binsX) const
{
    QVariantList out;
    if (!isValid() || firstRow < 0 || firstColumn < 0 || lastRow > m_rows || lastColumn > m_columns ||
        lastRow <= firstRow || lastColumn <= firstColumn || binsX < 1 || binsY < 1 ||
        quint64(binsX) * quint64(binsY) > 4096) return out;
    binsX = qMin(binsX, lastColumn - firstColumn);
    binsY = qMin(binsY, lastRow - firstRow);
    struct Bin { double low = std::numeric_limits<double>::infinity(); double high = -std::numeric_limits<double>::infinity();
                 int present = 0; int missingExplicit = 0; QString category; QString categoryHigh; bool mixed = false; };
    QVector<Bin> bins(binsX * binsY);
    // AGENT-GUARD: walk stored cells only. Iterating the declared matrix would
    // make a sparse billion-cell source proportional to offscreen data.
    for (auto it = m_cells.lowerBound(key(firstRow, 0)); it != m_cells.cend() && int(it.key() >> 32) < lastRow; ++it) {
        const int row = int(it.key() >> 32), col = int(quint32(it.key()));
        if (col < firstColumn || col >= lastColumn) continue;
        const int by = int((qint64(row - firstRow + 1) * binsY - 1) / (lastRow - firstRow));
        const int bx = int((qint64(col - firstColumn + 1) * binsX - 1) / (lastColumn - firstColumn));
        Bin &b = bins[by * binsX + bx];
        if (!it->value.isValid() || it->value.isNull()) { ++b.missingExplicit; continue; }
        ++b.present;
        if (m_valueCategories.isEmpty()) {
            const double v = it->value.toDouble(); b.low = qMin(b.low, v); b.high = qMax(b.high, v);
        } else {
            const QString v = it->value.toString();
            if (b.category.isEmpty()) b.category = v;
            else if (b.category != v) { b.categoryHigh = v; b.mixed = true; }
        }
    }
    out.reserve(bins.size());
    for (int by = 0; by < binsY; ++by) for (int bx = 0; bx < binsX; ++bx) {
        const int r0 = firstRow + int(qint64(by) * (lastRow - firstRow) / binsY);
        const int r1 = firstRow + int(qint64(by + 1) * (lastRow - firstRow) / binsY);
        const int c0 = firstColumn + int(qint64(bx) * (lastColumn - firstColumn) / binsX);
        const int c1 = firstColumn + int(qint64(bx + 1) * (lastColumn - firstColumn) / binsX);
        const Bin &b = bins[by * binsX + bx];
        const qint64 missing = qint64(r1-r0) * (c1-c0) - b.present;
        out.append(QVariantMap{{"row", r0}, {"rowEnd", r1}, {"column", c0}, {"columnEnd", c1},
                               {"minimum", b.present && m_valueCategories.isEmpty() ? QVariant(b.low) : QVariant()},
                               {"maximum", b.present && m_valueCategories.isEmpty() ? QVariant(b.high) : QVariant()},
                               {"category", b.category}, {"categoryHigh", b.categoryHigh}, {"mixed", b.mixed},
                               {"present", b.present}, {"missing", missing}});
    }
    return out;
}
quint64 HeatmapModel::requestTile(int r0, int r1, int c0, int c1, int by, int bx)
{
    const quint64 request = ++m_request, revision = m_revision;
    QTimer::singleShot(0, this, [this, request, revision, r0, r1, c0, c1, by, bx] {
        if (request != m_request || revision != m_revision) return;
        emit tileReady(request, revision, tile(r0, r1, c0, c1, by, bx));
    });
    return request;
}
void HeatmapModel::cancelTile() { ++m_request; }
} // namespace QindaTK
