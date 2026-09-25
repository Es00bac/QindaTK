// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_sampling.h"

#include <QFont>
#include <QFontInfo>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace QindaTK::ScientificSampling {

QVector<int> traceEnvelope(const QVector<ScientificSample> &samples, int first, int last, int buckets)
{
    QVector<int> out;
    first = std::max(0, first); last = std::min(int(samples.size()), last);
    const int count = last - first;
    if (count <= 0) return out;
    buckets = std::max(1, buckets);
    if (count <= buckets) {
        out.reserve(count);
        for (int i = first; i < last; ++i) out.append(i);
        return out;
    }
    out.reserve(5 * buckets);
    for (int b = 0; b < buckets; ++b) {
        // Integer bucket bounds partition the range exactly.
        const int begin = first + int(qint64(count) * b / buckets);
        const int end = first + int(qint64(count) * (b + 1) / buckets);
        int firstPresent = -1, lastPresent = -1, minimum = -1, maximum = -1, gap = -1;
        for (int i = begin; i < end; ++i) {
            const ScientificSample &s = samples[i];
            if (s.missingY) { if (gap < 0) gap = i; continue; }
            if (firstPresent < 0) firstPresent = i;
            lastPresent = i;
            if (minimum < 0 || s.y < samples[minimum].y) minimum = i;
            if (maximum < 0 || s.y > samples[maximum].y) maximum = i;
        }
        int picks[5] = {firstPresent, minimum, maximum, lastPresent, gap};
        std::sort(std::begin(picks), std::end(picks));
        int previous = -1;
        for (int p : picks)
            if (p >= 0 && p != previous) { out.append(p); previous = p; }
    }
    return out;
}

QVector<int> scatterStride(const QVector<ScientificSample> &samples, const QVector<int> &candidates, int limit)
{
    limit = std::max(8, limit);
    if (candidates.size() <= limit) return candidates;
    int extremes[4] = {-1, -1, -1, -1};
    for (int i : candidates) {
        const ScientificSample &s = samples[i];
        if (extremes[0] < 0 || s.x < samples[extremes[0]].x) extremes[0] = i;
        if (extremes[1] < 0 || s.x > samples[extremes[1]].x) extremes[1] = i;
        if (s.missingY) continue;
        if (extremes[2] < 0 || s.y < samples[extremes[2]].y) extremes[2] = i;
        if (extremes[3] < 0 || s.y > samples[extremes[3]].y) extremes[3] = i;
    }
    const int stride = int((candidates.size() + (limit - 4) - 1) / (limit - 4));
    QVector<int> out;
    out.reserve(limit);
    for (int k = 0; k < candidates.size(); k += stride) out.append(candidates[k]);
    for (int e : extremes) if (e >= 0) out.append(e);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

namespace {
double niceStep(double raw)
{
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double fraction = raw / magnitude;
    const double nice = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10;
    return nice * magnitude;
}
}

QVector<double> ticks(bool logarithmic, double low, double high, int approximate)
{
    QVector<double> out;
    if (!std::isfinite(low) || !std::isfinite(high)) return out;
    if (low > high) std::swap(low, high);
    if (!(high > low)) return out;
    approximate = std::clamp(approximate, 2, 50);
    if (logarithmic) {
        if (!(low > 0)) return out;
        const int first = int(std::floor(std::log10(low))), last = int(std::ceil(std::log10(high)));
        const bool fine = last - first < 3;
        for (int e = first; e <= last && out.size() < 200; ++e) {
            for (double m : {1.0, 2.0, 5.0}) {
                if (m != 1.0 && !fine) continue;
                // AGENT-NOTE: m * 10^e is formed from an exact power so 1e-3
                // compares equal to the literal a host writes.
                const double value = e >= 0 ? m * std::pow(10.0, e) : m / std::pow(10.0, -e);
                const double tolerance = value * 1e-12;
                if (value >= low - tolerance && value <= high + tolerance) out.append(value);
            }
        }
        return out;
    }
    const double step = niceStep((high - low) / (approximate - 1));
    const double tolerance = step * 1e-9;
    for (double k = std::ceil(low / step - 1e-9); out.size() < 200; k += 1) {
        double value = k * step;
        if (value > high + tolerance) break;
        if (std::abs(value) < tolerance) value = 0; // no "-0" or 1e-17 labels
        out.append(value);
    }
    return out;
}

QString tickLabel(double value)
{
    return QString::number(value, 'g', 6);
}

QJsonObject styleJson(const QVariantMap &metrics, const QList<QPair<QString, QColor>> &colors,
                      const QMap<QString, QList<QColor>> &ramps)
{
    // AGENT-NOTE: the requested font and the family fontconfig resolved it to
    // are both recorded: the resolved face is what decides the pixels.
    QFont font;
    font.setFamily(metrics.value(QStringLiteral("fontFamily")).toString());
    font.setPixelSize(std::max(1, metrics.value(QStringLiteral("fontSize")).toInt()));
    QJsonObject out{{QStringLiteral("metrics"), QJsonObject::fromVariantMap(metrics)},
                    {QStringLiteral("resolvedFontFamily"), QFontInfo(font).family()}};
    for (const auto &c : colors) out.insert(c.first, c.second.name(QColor::HexArgb));
    for (auto it = ramps.cbegin(); it != ramps.cend(); ++it) {
        QJsonArray ramp;
        for (const QColor &c : it.value()) ramp.append(c.name(QColor::HexArgb));
        out.insert(it.key(), ramp);
    }
    return out;
}

} // namespace QindaTK::ScientificSampling
