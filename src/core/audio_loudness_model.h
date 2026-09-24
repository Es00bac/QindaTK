// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_meter_tap.h"

#include <QObject>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <algorithm>
#include <array>
#include <climits>
#include <memory>
#include <utility>
#include <vector>

namespace QindaTK {

// AGENT-CONTRACT: ITU-R BS.1770-4 loudness of one measured stream (D-103,
// docs/audio-measurement.md). It consumes K-weighted AudioMeterTap blocks on
// the GUI thread: ten level blocks form one 100 ms sub-block; momentary is the
// last 4 sub-blocks (400 ms), short-term the last 30 (3 s); every 400 ms block
// (75 % overlap) louder than the -70 LUFS absolute gate enters the integrated
// measurement, which is gated again 10 LU below the mean of those blocks.
// Memory is bounded for any duration (a 0.01 LU histogram keeps exact energy
// sums); `history` keeps the last historySeconds of readings. Values are NaN
// until measurable and -inf for digital silence.
class AudioLoudnessModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString algorithm READ algorithm CONSTANT)
    Q_PROPERTY(QString unit READ unit CONSTANT)
    Q_PROPERTY(QString channelLayout READ channelLayout NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList channelWeights READ channelWeights NOTIFY configurationChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY configurationChanged)
    Q_PROPERTY(double momentaryLufs READ momentaryLufs NOTIFY measured)
    Q_PROPERTY(double shortTermLufs READ shortTermLufs NOTIFY measured)
    Q_PROPERTY(double integratedLufs READ integratedLufs NOTIFY measured)
    Q_PROPERTY(double maxMomentaryLufs READ maxMomentaryLufs NOTIFY measured)
    Q_PROPERTY(double maxShortTermLufs READ maxShortTermLufs NOTIFY measured)
    Q_PROPERTY(double measuredSeconds READ measuredSeconds NOTIFY measured)
    Q_PROPERTY(int gatedBlocks READ gatedBlocks NOTIFY measured)
    Q_PROPERTY(qint64 droppedBlocks READ droppedBlocks NOTIFY measured)
    Q_PROPERTY(QString historyMetric READ historyMetric WRITE setHistoryMetric NOTIFY settingsChanged)
    Q_PROPERTY(QString historyMetricId READ historyMetricId NOTIFY settingsChanged)
    Q_PROPERTY(QString historyWindow READ historyWindow NOTIFY settingsChanged)
    Q_PROPERTY(double historySeconds READ historySeconds WRITE setHistorySeconds NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY measured)
    Q_PROPERTY(QString summary READ summary NOTIFY measured)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    static constexpr int kSubBlockBlocks = 10;      // level blocks per 100 ms sub-block
    static constexpr int kMomentarySubBlocks = 4;   // 400 ms
    static constexpr int kShortTermSubBlocks = 30;  // 3 s
    static constexpr double kAbsoluteGateLufs = -70.0;
    static constexpr double kRelativeGateLu = -10.0;
    static constexpr double kGateResolutionLu = 0.01;
    static constexpr int kGateBins = 10000;         // -70 .. +30 LUFS

    explicit AudioLoudnessModel(QObject *parent = nullptr);
    ~AudioLoudnessModel() override;

    QString algorithm() const { return QStringLiteral("ITU-R BS.1770-4"); }
    QString unit() const { return QStringLiteral("LUFS"); }
    QString channelLayout() const { return m_layout; }
    QVariantList channelWeights() const;
    int sampleRate() const { return m_rate; }
    double momentaryLufs() const { return m_momentary; }
    double shortTermLufs() const { return m_shortTerm; }
    double integratedLufs() const;
    double maxMomentaryLufs() const { return m_maxMomentary; }
    double maxShortTermLufs() const { return m_maxShortTerm; }
    double measuredSeconds() const;
    int gatedBlocks() const { return int(std::min<quint64>(m_gatedCount, quint64(INT_MAX))); }
    qint64 droppedBlocks() const { return m_droppedBlocks; }
    QString historyMetric() const;
    void setHistoryMetric(const QString &metric);
    QString historyMetricId() const;
    QString historyWindow() const;
    double historySeconds() const { return m_historySeconds; }
    void setHistorySeconds(double seconds);
    QVariantList history() const;
    QString summary() const;
    QString lastError() const { return m_lastError; }

    // ---- engine path (C++, GUI thread) ----
    // A different rate, channel count, layout or block length resets the
    // measurement; 0 channels means no stream.
    void configure(int sampleRate, int channels, const QString &layout, int blockFrames = 0);
    void consume(const AudioMeterBlock &block);
    // Emits `measured` when a 100 ms sub-block closed since the last publish.
    void publish(quint64 newlyDropped = 0);
    // BS.1770-4 Table 3: L/R/C 1.0, Ls/Rs 1.41, LFE excluded; Mono is one
    // channel at 1.0; channels of an undeclared layout count as front channels.
    static QVector<double> channelWeightsFor(const QString &layout, int channels);
    // -0.691 + 10 log10(z); -inf for z <= 0.
    static double loudness(double weightedMeanSquare);

    // Clears the integrated measurement, maxima and history ("reset history").
    Q_INVOKABLE void reset();
    // Measurement report with provenance (schema "qindatk.loudness-report/1").
    Q_INVOKABLE QVariantMap report() const;
    // Fixture/offline path: measures interleaved samples through an
    // AudioMeterTap (continuous across calls with the same format). A trailing
    // partial level block waits for the next call.
    Q_INVOKABLE bool submitInterleaved(const QVariantList &samples, int channels, int sampleRate,
                                       const QString &layout = {});

signals:
    void measured();
    void configurationChanged();
    void settingsChanged();
    void errorChanged();

private:
    void clear();
    void closeSubBlock();
    double recentMean(int subBlocks) const;
    void gate(double energy);
    void appendHistory(double seconds, double value);
    int historyCapacity() const;
    bool fail(const QString &message);
    void clearError();

    int m_rate = 0;
    int m_channels = 0;
    int m_blockFrames = 0;
    QString m_layout;
    QVector<double> m_weights;
    // The open 100 ms sub-block.
    double m_subEnergy = 0.0;
    qint64 m_subFrames = 0;
    int m_subBlocks = 0;
    // Weighted mean squares of the last 30 closed sub-blocks, circular.
    std::array<double, kShortTermSubBlocks> m_recent{};
    int m_recentNext = 0;
    quint64 m_subBlockCount = 0;
    // Integrated gate: 400 ms blocks above -70 LUFS by 0.01 LU bin.
    std::vector<quint32> m_binCount;
    std::vector<double> m_binEnergy;
    quint64 m_gatedCount = 0;
    double m_gatedEnergy = 0.0;
    mutable double m_integrated = 0.0;
    mutable bool m_integratedDirty = true;
    double m_momentary = 0.0;
    double m_shortTerm = 0.0;
    double m_maxMomentary = 0.0;
    double m_maxShortTerm = 0.0;
    qint64 m_measuredFrames = 0;
    qint64 m_droppedBlocks = 0;
    bool m_fresh = false;
    // History: (seconds since reset, reading), circular.
    bool m_historyShortTerm = true;
    double m_historySeconds = 60.0;
    std::vector<std::pair<double, double>> m_history;
    std::size_t m_historyHead = 0;
    std::size_t m_historySize = 0;
    mutable QVariantList m_historyList;
    mutable bool m_historyDirty = true;
    std::unique_ptr<AudioMeterTap> m_fixtureTap;
    QString m_lastError;
};

} // namespace QindaTK
