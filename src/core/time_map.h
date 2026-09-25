// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>
#include <vector>

namespace QindaTK {

namespace detail {
// Internal fixed-width exact intermediate. Four 32-bit limbs cover every
// product permitted by TimeMap's validated input ranges on all C++17 compilers.
struct TimeMapWide {
    quint32 limb[4] = {};
    bool negative = false;
};
} // namespace detail

// Signed, exact storage domains. Conversions quantize only at their boundary.
struct SamplePosition { qint64 value = 0; };
struct FrameIndex { qint64 value = 0; };
struct MusicalTick { qint64 value = 0; };
struct RationalFrameRate { qint64 numerator = 30000; qint64 denominator = 1001; };

class TimeMap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY changed)
    Q_PROPERTY(qint64 frameRateNumerator READ frameRateNumerator NOTIFY changed)
    Q_PROPERTY(qint64 frameRateDenominator READ frameRateDenominator NOTIFY changed)
    Q_PROPERTY(int ticksPerQuarter READ ticksPerQuarter WRITE setTicksPerQuarter NOTIFY changed)
    Q_PROPERTY(QString originSample READ originSample WRITE setOriginSample NOTIFY changed)
    Q_PROPERTY(QVariantList tempoSegments READ tempoSegments WRITE setTempoSegments NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY rejected)
public:
    enum Rounding { TowardZero, Floor, Ceil, NearestAway };
    Q_ENUM(Rounding)
    explicit TimeMap(QObject *parent = nullptr);

    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int rate);
    qint64 frameRateNumerator() const { return m_frameRate.numerator; }
    qint64 frameRateDenominator() const { return m_frameRate.denominator; }
    Q_INVOKABLE bool setFrameRate(qint64 numerator, qint64 denominator);
    int ticksPerQuarter() const { return m_ppq; }
    void setTicksPerQuarter(int ppq);
    // An owner whose data is in ticks (the session's MIDI, WP-064) can refuse
    // a PPQ change it cannot follow: a non-empty reason rejects it.
    void setTicksPerQuarterGuard(std::function<QString(int ppq)> guard) { m_ppqGuard = std::move(guard); }
    QString originSample() const;
    void setOriginSample(const QString &value);
    QVariantList tempoSegments() const;
    void setTempoSegments(const QVariantList &segments);
    QString lastError() const { return m_lastError; }

    std::optional<SamplePosition> fromFrame(FrameIndex position, Rounding rounding) const;
    std::optional<FrameIndex> toFrame(SamplePosition position, Rounding rounding) const;
    std::optional<SamplePosition> fromTick(MusicalTick position, Rounding rounding) const;
    std::optional<MusicalTick> toTick(SamplePosition position, Rounding rounding) const;

    // String values avoid QML Number's 2^53 exact-integer ceiling. All
    // methods return {ok, value} or {ok:false, error}; no truncated result.
    Q_INVOKABLE QVariantMap frameToSample(const QString &frame, Rounding rounding = NearestAway) const;
    Q_INVOKABLE QVariantMap sampleToFrame(const QString &sample, Rounding rounding = NearestAway) const;
    Q_INVOKABLE QVariantMap tickToSample(const QString &tick, Rounding rounding = NearestAway) const;
    Q_INVOKABLE QVariantMap sampleToTick(const QString &sample, Rounding rounding = NearestAway) const;
    Q_INVOKABLE QVariantMap compareExact(const QString &left, const QString &right) const;
    Q_INVOKABLE QVariantMap formatTimecode(const QString &frame, bool dropFrame = false) const;
    Q_INVOKABLE QVariantMap parseTimecode(const QString &label, bool dropFrame = false) const;
    Q_INVOKABLE QVariantMap sampleToPixel(const QString &sample, const QString &viewportOriginSample,
                                           double pixelsPerSample) const;
    Q_INVOKABLE QVariantMap pixelToSample(double pixel, const QString &viewportOriginSample,
                                           double pixelsPerSample, Rounding rounding = NearestAway) const;
signals:
    void changed();
    void rejected(const QString &reason);
private:
    struct Segment {
        qint64 tick = 0;
        qint64 microsecondsPerQuarter = 500000;
        int numerator = 4;
        int denominator = 4;
        detail::TimeMapWide startUnits; // ticks * microseconds/quarter, accumulated exactly
    };
    static std::optional<qint64> parseInteger(const QString &text);
    static std::optional<qint64> quantize(detail::TimeMapWide numerator, quint64 denominator, Rounding rounding);
    static QVariantMap good(qint64 value);
    static QVariantMap bad(const QString &reason);
    static int segmentForTick(const std::vector<Segment> &segments, qint64 tick);
    int segmentForSample(detail::TimeMapWide sampleDelta) const;
    void reject(const QString &reason);
    int m_sampleRate = 48000;
    RationalFrameRate m_frameRate;
    int m_ppq = 960;
    std::function<QString(int)> m_ppqGuard;
    qint64 m_originSample = 0;
    std::vector<Segment> m_segments{{}};
    QString m_lastError;
};

} // namespace QindaTK
