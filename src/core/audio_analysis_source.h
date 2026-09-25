// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_peaks.h"
#include "audio_session.h"
#include "audio_spectrogram.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (D-135/D-136, docs/audio-editing.md): the analysis half of
// WaveformEditor and SpectrogramView. A source reads one resource of an
// AudioSession through session.edits (so it always analyses the samples that
// play), over the view range [startSample, endSample) in source samples
// (an empty endSample is the end of the source). Heavy work runs on the
// thread pool with a cancellation token; a newer request, a range change or a
// new source revision cancels the older one, and a superseded result is
// never published. The views stay pure: they bind `columns` / `tiles` and
// forward their zoom/scroll requests to zoom() and scroll().
class AudioAnalysisSource : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QindaTK::AudioSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString resourceId READ resourceId WRITE setResourceId NOTIFY resourceIdChanged)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(QString startSample READ startSample WRITE setStartSample NOTIFY rangeChanged)
    Q_PROPERTY(QString endSample READ endSample WRITE setEndSample NOTIFY rangeChanged)
    Q_PROPERTY(int columnCount READ columnCount WRITE setColumnCount NOTIFY columnCountChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString revision READ revision NOTIFY resultChanged)
    Q_PROPERTY(QString frames READ frames NOTIFY resultChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY resultChanged)
    Q_PROPERTY(int channels READ channels NOTIFY resultChanged)
public:
    static constexpr qint64 kMinimumZoomSamples = 8;

    explicit AudioAnalysisSource(QObject *parent = nullptr);
    ~AudioAnalysisSource() override;

    AudioSession *session() const { return m_session; }
    void setSession(AudioSession *session);
    QString resourceId() const { return m_resourceId; }
    void setResourceId(const QString &id);
    int channel() const { return m_channel; }
    void setChannel(int channel);
    QString startSample() const { return m_startSample; }
    void setStartSample(const QString &sample);
    QString endSample() const { return m_endSample; }
    void setEndSample(const QString &sample);
    int columnCount() const { return m_columnCount; }
    void setColumnCount(int count);
    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QString revision() const { return QString::number(m_revision); }
    QString frames() const { return QString::number(m_frames); }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }

    Q_INVOKABLE void refresh() { schedule(); }
    // Cancels the running request; state becomes "canceled" until the next change.
    Q_INVOKABLE void cancel();
    // Keeps `anchorSample` at the same place and divides the range length by
    // `factor` (down to kMinimumZoomSamples, up to the whole source).
    Q_INVOKABLE void zoom(const QString &anchorSample, double factor);
    // Moves the range by `fraction` of its length, clamped to the source.
    Q_INVOKABLE void scroll(double fraction);
    Q_INVOKABLE void showAll();

signals:
    void sessionChanged();
    void resourceIdChanged();
    void channelChanged();
    void rangeChanged();
    void columnCountChanged();
    void stateChanged();
    void resultChanged();

protected:
    // Called once per event-loop turn after any change; starts or completes
    // the request for the current settings.
    virtual void run() = 0;
    // Drops everything computed from an older revision.
    virtual void invalidate() = 0;
    // A setting (range, columns, channel, analysis parameters) changed. The
    // default recomputes on the next turn; a source whose requests depend on
    // every setting abandons the running one first.
    virtual void settingsTouched() { schedule(); }
    // Marks the next run "loading" and queues it (once per turn).
    void schedule();
    // Cancels the running request and makes any result it still delivers stale.
    void abandon();
    void setState(const QString &state, const QString &error = {});
    // The samples to analyse, refreshing revision/frames/rate/channels.
    AudioBufferPtr currentBuffer();
    std::pair<qint64, qint64> viewRange() const; // clamped to m_frames
    CancellationToken restartCancellation();
    bool stillCurrent(quint64 serial, quint64 revision) const { return serial == m_serial && revision == m_revision; }

    quint64 m_serial = 0;
    quint64 m_revision = 0;

private:
    void sourceChanged(const QString &resourceId);
    void setRange(qint64 start, qint64 end);

    QPointer<AudioSession> m_session;
    QMetaObject::Connection m_sourceConnection;
    QString m_resourceId;
    int m_channel = -1;
    QString m_startSample = QStringLiteral("0");
    QString m_endSample;
    int m_columnCount = 512;
    QString m_state = QStringLiteral("empty");
    QString m_error;
    qint64 m_frames = 0;
    int m_sampleRate = 0;
    int m_channels = 0;
    bool m_scheduled = false;
    bool m_skipRun = false;
    CancellationSource m_cancel;
};

// Waveform columns with exact extrema from a peak pyramid built once per
// source revision on a worker thread.
class AudioWaveformSource : public AudioAnalysisSource {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList columns READ columns NOTIFY resultChanged)
public:
    explicit AudioWaveformSource(QObject *parent = nullptr);
    QVariantList columns() const { return m_columns; }
    // Exact extrema over [startSample, endSample) once the pyramid is ready:
    // {ok, minimum, maximum}.
    Q_INVOKABLE QVariantMap extrema(const QString &startSample, const QString &endSample) const;

protected:
    void run() override;
    void invalidate() override;

private:
    AudioPeakPyramidPtr m_pyramid;
    quint64 m_pyramidRevision = 0;
    quint64 m_buildingRevision = 0; // revision of the build in flight
    quint64 m_buildSerial = 0;      // request serial of the build in flight; 0 when none
    QVariantList m_columns;
};

// Spectrogram cells from cached spectrum tiles (D-136).
class AudioSpectrogramSource : public AudioAnalysisSource {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int fftSize READ fftSize WRITE setFftSize NOTIFY settingsChanged)
    Q_PROPERTY(int hopSize READ hopSize WRITE setHopSize NOTIFY settingsChanged)
    Q_PROPERTY(int rows READ rows WRITE setRows NOTIFY settingsChanged)
    Q_PROPERTY(double minimumHz READ minimumHz WRITE setMinimumHz NOTIFY settingsChanged)
    Q_PROPERTY(double maximumHz READ maximumHz WRITE setMaximumHz NOTIFY settingsChanged)
    Q_PROPERTY(QString frequencyScale READ frequencyScale WRITE setFrequencyScale NOTIFY settingsChanged)
    Q_PROPERTY(double floorDb READ floorDb WRITE setFloorDb NOTIFY settingsChanged)
    Q_PROPERTY(int maxFrames READ maxFrames WRITE setMaxFrames NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList tiles READ tiles NOTIFY resultChanged)
    Q_PROPERTY(QStringList sampleBoundaries READ sampleBoundaries NOTIFY resultChanged)
    Q_PROPERTY(double resolvedMaximumHz READ resolvedMaximumHz NOTIFY resultChanged)
    Q_PROPERTY(int effectiveHop READ effectiveHop NOTIFY resultChanged)
    Q_PROPERTY(int stride READ stride NOTIFY resultChanged)
    Q_PROPERTY(QString window READ window CONSTANT)
    Q_PROPERTY(QString normalization READ normalization CONSTANT)
    Q_PROPERTY(int cachedTiles READ cachedTiles NOTIFY resultChanged)
    Q_PROPERTY(QString cacheBytes READ cacheBytes NOTIFY resultChanged)
public:
    static constexpr qint64 kCacheBudgetBytes = qint64(32) * 1024 * 1024;

    explicit AudioSpectrogramSource(QObject *parent = nullptr);

    int fftSize() const { return m_request.fftSize; }
    void setFftSize(int size);
    int hopSize() const { return m_request.hop; }
    void setHopSize(int hop);
    int rows() const { return m_request.rows; }
    void setRows(int rows);
    double minimumHz() const { return m_request.minimumHz; }
    void setMinimumHz(double hz);
    double maximumHz() const { return m_request.maximumHz; }
    void setMaximumHz(double hz);
    QString frequencyScale() const { return m_request.logarithmic ? QStringLiteral("log") : QStringLiteral("linear"); }
    void setFrequencyScale(const QString &scale);
    double floorDb() const { return m_request.floorDb; }
    void setFloorDb(double db);
    int maxFrames() const { return m_request.maxFrames; }
    void setMaxFrames(int frames);
    QVariantList tiles() const { return m_tiles; }
    QStringList sampleBoundaries() const { return m_boundaries; }
    double resolvedMaximumHz() const { return m_image ? m_image->bandEdges.back() : m_request.maximumHz; }
    int effectiveHop() const { return m_image ? int(m_image->effectiveHop) : 0; }
    int stride() const { return m_image ? m_image->stride : 0; }
    QString window() const { return QStringLiteral("Hann"); }
    QString normalization() const { return QStringLiteral("dBFS amplitude (full-scale sine = 0 dB)"); }
    int cachedTiles() const { return int(m_cache.size()); }
    QString cacheBytes() const { return QString::number(m_cacheBytes); }

    // {ok, decibels, intensity, lowHz, highHz, startSample, endSample} of one cell.
    Q_INVOKABLE QVariantMap cell(int column, int row) const;
    // The row whose band holds `hz`, or -1.
    Q_INVOKABLE int rowForFrequency(double hz) const;

signals:
    void settingsChanged();

protected:
    void run() override;
    void invalidate() override;
    void settingsTouched() override;

private:
    struct CacheEntry {
        AudioSpectrumTilePtr tile;
        quint64 used = 0;
    };
    QString cacheKey(int channel, int fftSize, qint64 hop, qint64 index) const;
    void store(const QString &key, const AudioSpectrumTilePtr &tile);

    AudioSpectrogramRequest m_request;
    double m_floorUsed = -120.0;
    std::optional<AudioSpectrogramImage> m_image;
    QVariantList m_tiles;
    QStringList m_boundaries;
    QHash<QString, CacheEntry> m_cache;
    qint64 m_cacheBytes = 0;
    quint64 m_useSerial = 0;
};

} // namespace QindaTK
