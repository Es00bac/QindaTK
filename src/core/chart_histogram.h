// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace QindaTK {

struct ChartHistogramBin {
    double lower = 0;
    double upper = 0;
    quint64 count = 0;
    double displayed = 0;
};

struct ChartHistogramResult {
    QVector<ChartHistogramBin> bins;
    QString channel;
    QString unit;
    QString error;
    quint64 underflow = 0;
    quint64 overflow = 0;
    quint64 missing = 0;
    quint64 inRange = 0;
    bool valid() const { return error.isEmpty(); }
};

// AGENT-CONTRACT: analysis backend supplies edges and channel meaning.
// Bins are [lower, upper), except the final bin includes its upper edge.
// Probability and density divide only by in-range finite sample count.
class ChartHistogram {
public:
    enum Normalization { Count, Probability, Density };
    static ChartHistogramResult fromSamples(const QVector<double> &edges,
                                             const QVector<double> &samples,
                                             Normalization normalization,
                                             const QString &channel = {},
                                             const QString &unit = {});
    static ChartHistogramResult fromCounts(const QVector<double> &edges,
                                            const QVector<quint64> &counts,
                                            Normalization normalization,
                                            const QString &channel = {},
                                            const QString &unit = {});
};

} // namespace QindaTK
