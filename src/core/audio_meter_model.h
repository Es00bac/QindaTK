// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
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

// Engine-side calculation. No QObject, clocks, GUI frame timing, or per-sample
// allocation in the sample loop. Send only the compact result to the GUI.
AudioBlockLevels measureInterleaved(std::span<const float> samples, int channels);

// GUI-thread presentation snapshot. An audio analysis worker measures blocks
// and queues their compact peak/RMS arrays to submitLevels(); it never touches
// QML. WP-060 must provide a preallocated realtime callback handoff.
class AudioMeterModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(int channelCount READ channelCount NOTIFY levelsChanged)
    Q_PROPERTY(double holdMilliseconds READ holdMilliseconds WRITE setHoldMilliseconds NOTIFY settingsChanged)
    Q_PROPERTY(double fallDbPerSecond READ fallDbPerSecond WRITE setFallDbPerSecond NOTIFY settingsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit AudioMeterModel(QObject *parent = nullptr) : QObject(parent) {}
    QVariantList levels() const;
    int channelCount() const { return m_channels.size(); }
    double holdMilliseconds() const { return m_holdMilliseconds; }
    void setHoldMilliseconds(double milliseconds);
    double fallDbPerSecond() const { return m_fallDbPerSecond; }
    void setFallDbPerSecond(double dbPerSecond);
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool submitLevels(const QVariantList &peaks, const QVariantList &rms,
                                  double elapsedMilliseconds);
    // Fixture/offline path. Large audio blocks should use the C++ measure
    // function and submit only channel levels to the GUI thread.
    Q_INVOKABLE bool submitInterleaved(const QVariantList &samples, int channels,
                                       double elapsedMilliseconds);
    Q_INVOKABLE void resetClip();
    static double toDbfs(double linear);
signals:
    void levelsChanged();
    void settingsChanged();
    void errorChanged();
private:
    struct Channel {
        double peak = 0;
        double rms = 0;
        double displayPeakDbfs = -120;
        double heldPeakDbfs = -120;
        double holdRemainingMs = 0;
        bool clipped = false;
    };
    bool fail(const QString &message);
    void clearError();
    bool update(const QVector<double> &peaks, const QVector<double> &rms,
                double elapsedMilliseconds, qsizetype invalidSamples);
    QVector<Channel> m_channels;
    double m_holdMilliseconds = 1500;
    double m_fallDbPerSecond = 24;
    qsizetype m_invalidSamples = 0;
    QString m_lastError;
};

} // namespace QindaTK
