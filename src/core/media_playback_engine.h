// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_decode_worker.h"
#include "transport_model.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

namespace QindaTK {

// The playback time source. A system clock drives preview playback; an audio
// output adapter (WP-060) supplies its device's played position instead, so
// video follows audio (docs/media-playback.md "Audio sync").
class MediaPlaybackClock {
public:
    virtual ~MediaPlaybackClock() = default;
    virtual qint64 nowNanoseconds() const = 0;
};

class MediaSystemClock final : public MediaPlaybackClock {
public:
    MediaSystemClock() { m_timer.start(); }
    qint64 nowNanoseconds() const override { return m_timer.nsecsElapsed(); }
private:
    QElapsedTimer m_timer;
};

// Deterministic clock for tests and offline stepping: time moves only when set.
class MediaManualClock final : public MediaPlaybackClock {
public:
    qint64 nowNanoseconds() const override { return m_now.load(); }
    void setNanoseconds(qint64 value) { m_now.store(value); }
    void advanceNanoseconds(qint64 delta) { m_now.fetch_add(delta); }
private:
    std::atomic<qint64> m_now{0};
};

struct MediaPlaybackStatistics {
    quint64 requested = 0; // render jobs submitted to the decode thread
    quint64 presented = 0; // frames that became the displayed frame
    quint64 stale = 0;     // results no longer wanted when they arrived
    quint64 cancelled = 0; // jobs stopped or skipped by their cancellation token
    quint64 failed = 0;    // render errors (missing or damaged media)
    quint64 dropped = 0;   // playback frames whose time passed before display
    quint64 cacheHits = 0; // targets served from the frame cache
};

// AGENT-CONTRACT (docs/media-playback.md "Playback engine", D-098): the
// engine is the transport authority. Its snapshot position (samples at the
// program's sample rate) is the only playhead; commands are acknowledged
// with the resulting snapshot on the next event-loop turn. The displayed
// frame is always program.frameAtSample(position) once decoded: a result for
// any other frame or program revision is never presented, so rapid seeks can
// leave the view pending but never showing a superseded frame.
class MediaPlaybackEngine final : public TransportAdapter {
    Q_OBJECT
public:
    static constexpr int kPrefetchFrames = 6;
    static constexpr int kTickIntervalMs = 8;
    static constexpr int kDefaultCacheFrames = 48;
    static constexpr int kMaxCacheFrames = 256;
    static constexpr qint64 kMaxCacheBytes = 256LL * 1024 * 1024;

    explicit MediaPlaybackEngine(QObject *parent = nullptr);
    ~MediaPlaybackEngine() override;

    TransportSnapshot snapshot() const override { return m_state; }
    void submit(const TransportCommand &command, ReplyCallback completion) override;
    void setSnapshotSink(SnapshotCallback sink) override { m_sink = std::move(sink); }

    void setClock(std::shared_ptr<MediaPlaybackClock> clock);
    // Replaces the program. The position is kept (clamped to the new end);
    // work for the previous program is cancelled and its cached frames freed.
    void setProgram(MediaProgramPtr program);
    MediaProgramPtr program() const { return m_program; }
    // Largest render size for previews (aspect kept, never upscaled); an
    // empty size renders at the program's frame size.
    void setOutputSize(const QSize &size);
    QSize outputSize() const { return m_outputSize; }
    void setCacheLimit(int frames);
    int cacheLimit() const { return m_cacheLimit; }
    int cachedFrames() const { return int(m_cache.size()); }

    qint64 positionSample() const { return m_state.positionSample; }
    qint64 targetFrame() const { return m_targetFrame; }
    qint64 presentedFrame() const { return m_presentedFrame; }
    // The displayed frame; its image is null when that frame failed.
    const MediaRenderedFrame &presented() const { return m_presented; }
    // The program that produced the displayed frame (may trail program()).
    MediaProgramPtr presentedProgram() const { return m_presentedProgram; }
    quint64 presentedRevision() const { return m_presentedRevision; }
    bool pending() const;
    const MediaPlaybackStatistics &statistics() const { return m_stats; }
    int inFlight() const { return int(m_flights.size()); }

    // Advances playback from the clock now; the frame timer calls it too.
    void tick();
    // Opens and indexes `uri` on the decode thread; `done` runs on this
    // object's thread. A newer probe cancels older ones, whose `done` then
    // receives ErrorCode::Cancelled.
    void probe(const QString &uri, std::function<void(MediaSourceInfoPtr, OperationError)> done);

signals:
    void framePresented();
    void targetChanged();
    void statisticsChanged();
    void stateChanged();

private:
    struct Flight {
        QString key;
        qint64 frame = -1;
        QSize size;
        CancellationSource cancel;
        bool cancelled = false;
    };
    struct PendingProbe {
        std::function<void(MediaSourceInfoPtr, OperationError)> done;
        CancellationSource cancel;
    };
    struct CacheEntry {
        QString key;
        qint64 frame = -1;
        MediaRenderedFrame rendered;
        qint64 bytes = 0;
        quint64 used = 0;
    };
    // Program cache key plus the render size: identifies interchangeable frames.
    QString currentKey() const;
    QSize requestSize() const;
    qint64 endSample() const;
    qint64 frameForPosition(qint64 position) const;
    qint64 clockPosition(bool *reachedEnd) const;
    void publish();
    // countDrops: the clock moved the playhead, so skipped frames are dropped.
    void updateTarget(bool countDrops = false);
    void request(qint64 frame, int priority);
    void cancelOutside(qint64 first, qint64 last);
    void present(const MediaRenderedFrame &rendered);
    void remember(const QString &key, qint64 frame, const MediaRenderedFrame &rendered);
    const CacheEntry *cached(qint64 frame);
    void trimCache();
    void onResult(MediaWorkerResult result);
    void refreshCapabilities();
    void startFrameTimer();

    std::shared_ptr<MediaPlaybackClock> m_clock;
    SnapshotCallback m_sink;
    TransportSnapshot m_state;
    MediaProgramPtr m_program;
    QString m_programKey;
    QSize m_outputSize;
    qint64 m_anchorSample = 0;
    qint64 m_anchorNanos = 0;
    qint64 m_targetFrame = -1;
    qint64 m_presentedFrame = -1;
    QString m_presentedKey;
    MediaProgramPtr m_presentedProgram;
    MediaRenderedFrame m_presented;
    quint64 m_presentedRevision = 0;
    MediaPlaybackStatistics m_stats;
    QHash<quint64, Flight> m_flights;
    QHash<quint64, PendingProbe> m_probes;
    QList<CacheEntry> m_cache;
    int m_cacheLimit = kDefaultCacheFrames;
    quint64 m_serial = 0;
    quint64 m_cacheClock = 0;
    QTimer m_timer;
    // AGENT-GUARD: declared last so it is destroyed first; its thread calls
    // back into this object until it has joined.
    std::unique_ptr<MediaDecodeWorker> m_worker;
};

} // namespace QindaTK
