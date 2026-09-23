// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_histogram.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
ChartHistogramResult initial(const QVector<double> &edges, const QString &channel, const QString &unit)
{
    ChartHistogramResult result;
    result.channel = channel; result.unit = unit;
    if (edges.size() < 2) {
        result.error = QStringLiteral("At least two edges are required"); return result;
    }
    for (int i = 1; i < edges.size(); ++i) {
        if (!std::isfinite(edges.at(i - 1)) || !std::isfinite(edges.at(i)) || edges.at(i - 1) >= edges.at(i)) {
            result.error = QStringLiteral("Bin edges must be finite and strictly increasing"); return result;
        }
        result.bins.append({edges.at(i - 1), edges.at(i), 0, 0});
    }
    return result;
}
void normalize(ChartHistogramResult &result, ChartHistogram::Normalization mode)
{
    for (ChartHistogramBin &bin : result.bins) {
        if (mode == ChartHistogram::Count) bin.displayed = double(bin.count);
        else if (result.inRange == 0) bin.displayed = 0;
        else if (mode == ChartHistogram::Probability)
            bin.displayed = double(bin.count) / double(result.inRange);
        else bin.displayed = double(bin.count) / (double(result.inRange) * (bin.upper - bin.lower));
    }
}
}
ChartHistogramResult ChartHistogram::fromSamples(const QVector<double> &edges,
                                                  const QVector<double> &samples,
                                                  Normalization mode,
                                                  const QString &channel,
                                                  const QString &unit)
{
    ChartHistogramResult result = initial(edges, channel, unit);
    if (!result.valid()) return result;
    result.normalization = int(mode);
    for (double value : samples) {
        if (!std::isfinite(value)) { ++result.missing; continue; }
        if (value < edges.first()) { ++result.underflow; continue; }
        if (value > edges.last()) { ++result.overflow; continue; }
        auto upper = std::upper_bound(edges.cbegin(), edges.cend(), value);
        const int index = std::min(int(upper - edges.cbegin()) - 1, int(result.bins.size() - 1));
        ++result.bins[index].count;
        ++result.inRange;
    }
    normalize(result, mode);
    return result;
}
ChartHistogramResult ChartHistogram::fromCounts(const QVector<double> &edges,
                                                 const QVector<quint64> &counts,
                                                 Normalization mode,
                                                 const QString &channel,
                                                 const QString &unit)
{
    ChartHistogramResult result = initial(edges, channel, unit);
    if (!result.valid()) return result;
    result.normalization = int(mode);
    if (counts.size() != result.bins.size()) {
        result.error = QStringLiteral("One count is required per bin"); return result;
    }
    for (int i = 0; i < counts.size(); ++i) {
        if (std::numeric_limits<quint64>::max() - result.inRange < counts.at(i)) {
            result.error = QStringLiteral("Count total overflows"); return result;
        }
        result.bins[i].count = counts.at(i);
        result.inRange += counts.at(i);
    }
    normalize(result, mode);
    return result;
}
} // namespace QindaTK
