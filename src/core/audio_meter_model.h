// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_meter_tap.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <span>

namespace QindaTK {

struct AudioBlockLevels {
    QVector<double> peaks; // linear full scale; values over 1 are overloads
    QVector<double> rms;   // linear full scale over the supplied block
    qsizetype invalidSamples = 0;
};

// Fixture/offline calculation. No QObject, clocks, GUI frame timing, or
// per-sample allocation in the sample loop. The engine measures in
// AudioMeterTap instead (the same quantities, on the audio thread).
AudioBlockLevels measureInterleaved(std::span<const float> samples, int channels);

// AGENT-CONTRACT: GUI-thread peak/RMS state of one measured stream
// (docs/audio-measurement.md). The engine path is configure() for the stream
// layout, consume() for every AudioMeterTap block drained from the audio
// thread and publish() once per drain: ballistics advance by each block's own
// audio duration, so snapshots do not depend on the GUI refresh rate (D-102).
// submitLevels/submitInterleaved remain the fixture path, timed by the
// caller's elapsed wall time. Views only project `levels`.
class AudioMeterModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(int channelCount READ channelCount NOTIFY levelsChanged)
    Q_PROPERTY(QString metric READ metric CONSTANT)
    Q_PROPERTY(QString channelLayout READ channelLayout NOTIFY configurationChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY configurationChanged)
    Q_PROPERTY(double blockMilliseconds READ blockMilliseconds NOTIFY configurationChanged)
    Q_PROPERTY(double rmsWindowMilliseconds READ rmsWindowMilliseconds WRITE setRmsWindowMilliseconds NOTIFY settingsChanged)
    Q_PROPERTY(double holdMilliseconds READ holdMilliseconds WRITE setHoldMilliseconds NOTIFY settingsChanged)
    Q_PROPERTY(double fallDbPerSecond READ fallDbPerSecond WRITE setFallDbPerSecond NOTIFY settingsChanged)
    Q_PROPERTY(QString integrationWindow READ integrationWindow NOTIFY settingsChanged)
    Q_PROPERTY(double measuredSeconds READ measuredSeconds NOTIFY levelsChanged)
    Q_PROPERTY(qint64 droppedBlocks READ droppedBlocks NOTIFY levelsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    static constexpr double kFloorDbfs = -120.0;

    explicit AudioMeterModel(QObject *parent = nullptr) : QObject(parent) {}
    QVariantList levels() const;
    int channelCount() const { return int(m_channels.size()); }
    QString metric() const { return QStringLiteral("sample-peak+rms"); }
    QString channelLayout() const { return m_layout; }
    int sampleRate() const { return m_sampleRate; }
    double blockMilliseconds() const;
    double rmsWindowMilliseconds() const;
    void setRmsWindowMilliseconds(double milliseconds);
    double holdMilliseconds() const { return m_holdMilliseconds; }
    void setHoldMilliseconds(double milliseconds);
    double fallDbPerSecond() const { return m_fallDbPerSecond; }
    void setFallDbPerSecond(double dbPerSecond);
    QString integrationWindow() const;
    double measuredSeconds() const;
    qint64 droppedBlocks() const { return m_droppedBlocks; }
    QString lastError() const { return m_lastError; }

    // ---- engine path (C++, GUI thread) ----
    // A different rate, channel count, layout or block length resets the
    // meter; 0 channels clears it (no stream). blockFrames <= 0: the tap default.
    void configure(int sampleRate, int channels, const QString &layout, int blockFrames = 0);
    void consume(const AudioMeterBlock &block);
    // One levelsChanged per drain; `newlyDropped` blocks never reached the GUI.
    void publish(quint64 newlyDropped = 0);
    // "Mono" -> {"M"}, "Stereo" -> {"L", "R"}, otherwise "1".."n".
    static QStringList channelNames(const QString &layout, int channels);

    // ---- fixture path ----
    Q_INVOKABLE bool submitLevels(const QVariantList &peaks, const QVariantList &rms,
                                  double elapsedMilliseconds);
    // Fixture/offline path. Large audio blocks should use the C++ measure
    // function and submit only channel levels to the GUI thread.
    Q_INVOKABLE bool submitInterleaved(const QVariantList &samples, int channels,
                                       double elapsedMilliseconds);
    // Clears clip latches, the maximum peak and invalid-sample counts.
    Q_INVOKABLE void resetClip();
    static double toDbfs(double linear);
signals:
    void levelsChanged();
    void configurationChanged();
    void settingsChanged();
    void errorChanged();
private:
    struct Channel {
        double peak = 0;
        double rms = 0;
        double maxPeak = 0;
        double displayPeakDbfs = kFloorDbfs;
        double heldPeakDbfs = kFloorDbfs;
        double holdRemainingMs = 0;
        qint64 invalid = 0;
        bool clipped = false;
    };
    bool fail(const QString &message);
    void clearError();
    void advance(Channel &channel, double peak, double elapsedMilliseconds) const;
    void resize(int channels);
    void resetWindow();
    int windowBlocks() const;
    bool update(const QVector<double> &peaks, const QVector<double> &rms,
                double elapsedMilliseconds, qsizetype invalidSamples);
    QVector<Channel> m_channels;
    QStringList m_names;
    QString m_layout;
    int m_sampleRate = 0;
    int m_blockFrames = 0;
    double m_rmsWindowMilliseconds = 300;
    double m_holdMilliseconds = 1500;
    double m_fallDbPerSecond = 24;
    // Sliding RMS window of the engine path: per-block sums, circular.
    QVector<double> m_windowSquares; // windowBlocks() * channels
    QVector<int> m_windowFrames;     // windowBlocks()
    int m_windowNext = 0;
    qint64 m_measuredFrames = 0;
    qint64 m_droppedBlocks = 0;
    qsizetype m_invalidSamples = 0; // fixture path: the last submitted block
    QString m_lastError;
};

} // namespace QindaTK
