// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "scientific_dataset.h"

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

namespace QindaTK::ScientificSampling {

// AGENT-CONTRACT: painting and hit testing in ScientificPlotRenderer use only
// the indices these functions return, so a claim about what a figure shows
// (tests, export metadata "sampling") is a claim about these functions.

// Trace downsampling over [first, last): when the range holds more than
// `buckets` samples it is split into `buckets` equal index buckets and each
// keeps its first, minimum-Y, maximum-Y and last present sample plus its
// first missing sample (an M4 envelope that never drops a peak or a gap).
// At most 5 * buckets indices, ascending and unique.
QVector<int> traceEnvelope(const QVector<ScientificSample> &samples, int first, int last, int buckets);

// Scatter thinning: all indices when count <= limit, otherwise every k-th
// index plus the samples holding the minimum/maximum X and Y. Ascending.
QVector<int> scatterStride(const QVector<ScientificSample> &samples, const QVector<int> &candidates, int limit);

// Calibrated tick values inside [low, high]. Linear: 1/2/5 x 10^n steps
// giving about `approximate` ticks. Logarithmic (low > 0): every decade, with
// 2 and 5 multiples added while fewer than three decades are in view.
QVector<double> ticks(bool logarithmic, double low, double high, int approximate);

// Shortest decimal text that round-trips to 6 significant digits.
QString tickLabel(double value);

// Export provenance of the render style (D-141): the metrics map, the font
// family it resolves to, each named colour and each named colour ramp, as
// #AARRGGBB. The 2D renderer, the composer and ScientificScene3D embed it.
QJsonObject styleJson(const QVariantMap &metrics, const QList<QPair<QString, QColor>> &colors,
                      const QMap<QString, QList<QColor>> &ramps = {});

} // namespace QindaTK::ScientificSampling
