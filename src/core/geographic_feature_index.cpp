// SPDX-License-Identifier: LGPL-3.0-or-later
#include "geographic_feature_model.h"
#include <QBitArray>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int columns = 36, rows = 18, maxCellsPerFeature = 64;
int column(double longitude)
{ return std::clamp(static_cast<int>(std::floor((longitude + 180.0) / 10.0)), 0, columns - 1); }
int row(double latitude)
{ return std::clamp(static_cast<int>(std::floor((latitude + 90.0) / 10.0)), 0, rows - 1); }
void extend(const QJsonValue &value, double &west, double &east, double &south, double &north)
{
    const QJsonArray array = value.toArray();
    if (array.size() >= 2 && array[0].isDouble() && array[1].isDouble()) {
        const double lon = array[0].toDouble(), lat = array[1].toDouble();
        west = std::min(west, lon); east = std::max(east, lon);
        south = std::min(south, lat); north = std::max(north, lat);
    } else for (const auto &part : array) extend(part, west, east, south, north);
}
}

void GeographicFeatureModel::rebuildSpatialIndex() const
{
    for (auto &cell : m_spatialCells) cell.clear();
    m_spatialOverflow.clear();
    m_spatialBounds.clear();
    m_spatialBounds.reserve(m_features.size());
    for (int index = 0; index < m_features.size(); ++index) {
        Bounds bounds{180, -180, 90, -90};
        extend(m_features[index].geometry.value("coordinates"), bounds.west,
               bounds.east, bounds.south, bounds.north);
        // A shortest-path segment spanning the seam can occupy ±180° while
        // its raw WGS84 endpoints bound only the interior longitude interval.
        if (bounds.east - bounds.west > 180.0) { bounds.west = -180; bounds.east = 180; }
        m_spatialBounds.append(bounds);
        const int x0 = column(bounds.west), x1 = column(bounds.east);
        const int y0 = row(bounds.south), y1 = row(bounds.north);
        if ((x1 - x0 + 1) * (y1 - y0 + 1) > maxCellsPerFeature) {
            m_spatialOverflow.append(index);
            continue;
        }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                m_spatialCells[y * columns + x].append(index);
    }
    m_spatialDirty = false;
}

QVector<int> GeographicFeatureModel::queryFeatureIndices(double west, double east,
                                                            double south, double north) const
{
    if (!std::isfinite(west) || !std::isfinite(east) || !std::isfinite(south) ||
        !std::isfinite(north) || west > east || south > north || north < -90 || south > 90)
        return {};
    if (m_spatialDirty) rebuildSpatialIndex();
    south = std::max(-90.0, south); north = std::min(90.0, north);
    QBitArray seen(m_features.size());
    QVector<int> result;
    const auto query = [&](double a, double b) {
        if (a > b) return;
        const auto visit = [&](int index) {
            if (seen.testBit(index)) return;
            const auto &box = m_spatialBounds[index];
            if (box.west <= b && box.east >= a && box.south <= north && box.north >= south) {
                seen.setBit(index); result.append(index);
            }
        };
        for (int index : m_spatialOverflow) visit(index);
        for (int y = row(south); y <= row(north); ++y)
            for (int x = column(a); x <= column(b); ++x)
                for (int index : m_spatialCells[y * columns + x]) visit(index);
    };
    if (east - west >= 360.0) query(-180, 180);
    else {
        const double firstShift = std::floor((-180.0 - west) / 360.0) * 360.0;
        for (int copy = 0; copy < 2; ++copy) {
            const double shift = firstShift + 360.0 * copy;
            query(std::max(-180.0, west + shift), std::min(180.0, east + shift));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}
}
