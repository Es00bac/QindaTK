// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_playback_engine.h"

#include <QMetaObject>
#include <algorithm>

namespace QindaTK {

MediaPlaybackEngine::MediaPlaybackEngine(QObject *parent)
    : TransportAdapter(parent), m_clock(std::make_shared<MediaSystemClock>())
{
    m_state.revision = 1;
    refreshCapabilities();
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(kTickIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &MediaPlaybackEngine::tick);
    // Results cross to this object's thread; ~QObject drops undelivered ones.
    m_worker = std::make_unique<MediaDecodeWorker>([this](MediaWorkerResult result) {
        QMetaObject::invokeMethod(this, [this, result]() mutable { onResult(std::move(result)); },
                                  Qt::QueuedConnection);
    });
}

MediaPlaybackEngine::~MediaPlaybackEngine()
{
    m_timer.stop();
    for (Flight &flight : m_flights) flight.cancel.cancel();
    for (const PendingProbe &probe : std::as_const(m_probes)) probe.cancel.cancel();
    m_worker.reset();
}

void MediaPlaybackEngine::refreshCapabilities()
{
    const bool ready = m_program && !m_program->isEmpty();
    const QString none = ready ? QString() : tr("No media is loaded");
    m_state.canPlay = ready;
    m_state.canSeek = ready;
    m_state.canLoop = ready;
    m_state.canRecord = false;
    m_state.canPunch = false;
    m_state.playUnavailableReason = none;
    m_state.seekUnavailableReason = none;
    m_state.loopUnavailableReason = none;
    m_state.recordUnavailableReason = tr("Media playback cannot record");
    m_state.punchUnavailableReason = tr("Media playback cannot punch in");
    m_state.playbackRate = 1.0;
}

QSize MediaPlaybackEngine::requestSize() const
{
    // The output size bounds previews; it never upscales or changes the aspect.
    const QSize native = m_program ? m_program->frameSize : QSize();
    if (m_outputSize.width() <= 0 || m_outputSize.height() <= 0 || native.isEmpty()) return native;
    if (native.width() <= m_outputSize.width() && native.height() <= m_outputSize.height()) return native;
    return native.scaled(m_outputSize, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

QString MediaPlaybackEngine::currentKey() const
{
    const QSize size = requestSize();
    return m_programKey + QLatin1Char('@') + QString::number(size.width()) + QLatin1Char('x')
        + QString::number(size.height());
}

qint64 MediaPlaybackEngine::endSample() const
{
    return m_program ? m_program->endSample() : 0;
}

bool MediaPlaybackEngine::pending() const
{
    return m_targetFrame >= 0 && (m_presentedFrame != m_targetFrame || m_presentedKey != currentKey());
}

qint64 MediaPlaybackEngine::frameForPosition(qint64 position) const
{
    if (!m_program || m_program->isEmpty()) return -1;
    if (const auto frame = m_program->frameAtSample(position)) return *frame;
    // The end position (and anything past it) holds the last frame.
    return position <= 0 ? 0 : m_program->frameCount - 1;
}

qint64 MediaPlaybackEngine::clockPosition(bool *reachedEnd) const
{
    *reachedEnd = false;
    if (!m_state.playing || !m_program) return m_state.positionSample;
    const qint64 elapsed = std::max<qint64>(0, m_clock->nowNanoseconds() - m_anchorNanos);
    const auto advanced = TimelineTime::mulDiv(elapsed, quint64(m_program->sampleRate), 1000000000ULL,
                                               TimeMap::Floor);
    const qint64 end = endSample();
    const auto raw = advanced ? TimelineTime::add(m_anchorSample, *advanced) : std::nullopt;
    const qint64 position = raw ? *raw : end;
    if (m_state.loopEnabled && m_state.loopEnd > m_state.loopStart && m_anchorSample < m_state.loopEnd
        && position >= m_state.loopEnd)
        return m_state.loopStart + (position - m_state.loopEnd) % (m_state.loopEnd - m_state.loopStart);
    if (position >= end) {
        *reachedEnd = true;
        return end;
    }
    return position;
}

void MediaPlaybackEngine::publish()
{
    if (m_sink) m_sink(m_state);
}

void MediaPlaybackEngine::startFrameTimer()
{
    if (!m_timer.isActive()) m_timer.start();
}

void MediaPlaybackEngine::submit(const TransportCommand &command, ReplyCallback completion)
{
    TransportReply reply;
    reply.requestId = command.id;
    reply.accepted = true;
    const auto deny = [&reply](const QString &reason) {
        reply.accepted = false;
        reply.error = reason;
    };
    const bool ready = m_program && !m_program->isEmpty();
    const qint64 end = endSample();
    bool changed = false;
    switch (command.kind) {
    case TransportCommand::Kind::Play:
        if (!ready) {
            deny(m_state.playUnavailableReason);
        } else if (!m_state.playing) {
            if (m_state.positionSample >= end) m_state.positionSample = 0; // play at the end restarts
            m_state.playing = true;
            m_anchorSample = m_state.positionSample;
            m_anchorNanos = m_clock->nowNanoseconds();
            changed = true;
        }
        break;
    case TransportCommand::Kind::Stop:
        if (m_state.playing) {
            bool reachedEnd = false;
            m_state.positionSample = clockPosition(&reachedEnd);
            m_state.playing = false;
            changed = true;
        }
        break;
    case TransportCommand::Kind::Seek:
        if (!ready) {
            deny(m_state.seekUnavailableReason);
        } else if (command.positionSample < 0 || command.positionSample > end) {
            deny(tr("Position %1 is outside the program (0 to %2 samples)").arg(command.positionSample).arg(end));
        } else {
            m_state.positionSample = command.positionSample;
            m_anchorSample = command.positionSample;
            m_anchorNanos = m_clock->nowNanoseconds();
            changed = true;
        }
        break;
    case TransportCommand::Kind::Loop:
        if (command.enabled && !ready) {
            deny(m_state.loopUnavailableReason);
        } else if (command.enabled && (command.rangeStart < 0 || command.rangeEnd > end
                                       || command.rangeStart >= command.rangeEnd)) {
            deny(tr("The loop range must lie inside the program"));
        } else {
            if (m_state.playing) {
                bool reachedEnd = false;
                m_anchorSample = clockPosition(&reachedEnd);
                m_anchorNanos = m_clock->nowNanoseconds();
                m_state.positionSample = m_anchorSample;
            }
            m_state.loopEnabled = command.enabled;
            m_state.loopStart = command.rangeStart;
            m_state.loopEnd = command.rangeEnd;
            changed = true;
        }
        break;
    case TransportCommand::Kind::Record:
        if (command.enabled) deny(m_state.recordUnavailableReason);
        break;
    case TransportCommand::Kind::Punch:
        if (command.enabled) deny(m_state.punchUnavailableReason);
        break;
    }
    if (changed) ++m_state.revision;
    reply.snapshot = m_state;
    // AGENT-NOTE: acknowledgments are delivered on the next event-loop turn
    // so a caller always holds the request ID before its reply arrives.
    QMetaObject::invokeMethod(this, [completion, reply]() {
        if (completion) completion(reply);
    }, Qt::QueuedConnection);
    if (!changed) return;
    if (m_state.playing) startFrameTimer();
    else m_timer.stop();
    updateTarget();
    emit stateChanged();
}

void MediaPlaybackEngine::setClock(std::shared_ptr<MediaPlaybackClock> clock)
{
    if (!clock || clock == m_clock) return;
    bool reachedEnd = false;
    const qint64 position = clockPosition(&reachedEnd);
    m_clock = std::move(clock);
    m_anchorSample = position;
    m_anchorNanos = m_clock->nowNanoseconds();
    m_state.positionSample = position;
}

void MediaPlaybackEngine::setProgram(MediaProgramPtr program)
{
    m_program = std::move(program);
    m_programKey = m_program ? m_program->cacheKey() : QString();
    const QString key = currentKey();
    m_cache.erase(std::remove_if(m_cache.begin(), m_cache.end(),
                                 [&key](const CacheEntry &entry) { return entry.key != key; }),
                  m_cache.end());
    const qint64 end = endSample();
    m_state.positionSample = std::clamp<qint64>(m_state.positionSample, 0, end);
    if (m_state.loopEnabled && m_state.loopEnd > end) m_state.loopEnabled = false;
    if (m_state.playing) {
        if (!m_program || m_program->isEmpty()) {
            m_state.playing = false;
            m_timer.stop();
        } else {
            m_anchorSample = m_state.positionSample;
            m_anchorNanos = m_clock->nowNanoseconds();
        }
    }
    refreshCapabilities();
    ++m_state.revision;
    publish();
    // A new program is not a playback skip: no dropped frames are counted.
    m_targetFrame = -1;
    if ((!m_program || m_program->isEmpty()) && m_presentedFrame >= 0) {
        // AGENT-GUARD: with no program (e.g. offline media) nothing may stay
        // on screen under the old identity.
        m_presented = MediaRenderedFrame();
        m_presentedFrame = -1;
        m_presentedKey.clear();
        m_presentedProgram.reset();
        ++m_presentedRevision;
        emit framePresented();
    }
    updateTarget();
    emit stateChanged();
}

void MediaPlaybackEngine::setOutputSize(const QSize &size)
{
    const QSize bounded = size.width() > 0 && size.height() > 0
        ? QSize(std::min(size.width(), 8192), std::min(size.height(), 8192)) : QSize();
    if (bounded == m_outputSize) return;
    m_outputSize = bounded;
    updateTarget();
}

void MediaPlaybackEngine::setCacheLimit(int frames)
{
    m_cacheLimit = std::clamp(frames, 0, kMaxCacheFrames);
    trimCache();
}

void MediaPlaybackEngine::tick()
{
    if (!m_state.playing) return;
    bool reachedEnd = false;
    const qint64 position = clockPosition(&reachedEnd);
    if (position == m_state.positionSample && !reachedEnd) return;
    m_state.positionSample = position;
    // Frames the clock skipped count as dropped even on the tick that ends playback.
    updateTarget(true);
    if (reachedEnd) {
        m_state.playing = false;
        m_timer.stop();
        cancelOutside(m_targetFrame, m_targetFrame);
    }
    ++m_state.revision;
    publish();
    if (reachedEnd) emit stateChanged();
}

void MediaPlaybackEngine::updateTarget(bool countDrops)
{
    const qint64 target = frameForPosition(m_state.positionSample);
    const QString key = currentKey();
    if (target != m_targetFrame) {
        if (countDrops && m_targetFrame >= 0 && target > m_targetFrame) {
            quint64 skipped = quint64(target - m_targetFrame - 1);
            if (m_presentedFrame != m_targetFrame || m_presentedKey != key) ++skipped;
            if (skipped > 0) {
                m_stats.dropped += skipped;
                emit statisticsChanged();
            }
        }
        m_targetFrame = target;
        emit targetChanged();
    }
    if (target < 0) {
        cancelOutside(0, -1);
        return;
    }
    const qint64 last = m_state.playing
        ? std::min<qint64>(target + kPrefetchFrames, m_program->frameCount - 1) : target;
    cancelOutside(target, last);
    if (m_presentedFrame != target || m_presentedKey != key) request(target, 0);
    for (qint64 frame = target + 1; frame <= last; ++frame) request(frame, 1);
}

const MediaPlaybackEngine::CacheEntry *MediaPlaybackEngine::cached(qint64 frame)
{
    const QString key = currentKey();
    for (CacheEntry &entry : m_cache) {
        if (entry.key == key && entry.frame == frame) {
            entry.used = ++m_cacheClock;
            return &entry;
        }
    }
    return nullptr;
}

void MediaPlaybackEngine::request(qint64 frame, int priority)
{
    const QString key = currentKey();
    if (const CacheEntry *entry = cached(frame)) {
        if (frame == m_targetFrame && (m_presentedFrame != frame || m_presentedKey != key)) {
            ++m_stats.cacheHits;
            const MediaRenderedFrame hit = entry->rendered;
            present(hit);
        }
        return;
    }
    for (const Flight &flight : std::as_const(m_flights))
        if (!flight.cancelled && flight.key == key && flight.frame == frame) return;
    MediaWorkerJob job;
    job.kind = MediaWorkerJob::Kind::Render;
    job.serial = ++m_serial;
    job.priority = priority;
    job.program = m_program;
    job.frame = frame;
    job.outputSize = requestSize();
    Flight flight;
    flight.key = key;
    flight.frame = frame;
    flight.size = job.outputSize;
    flight.cancel = job.cancel; // copies share one cancellation state
    m_flights.insert(job.serial, flight);
    ++m_stats.requested;
    m_worker->submit(std::move(job));
    emit statisticsChanged();
}

void MediaPlaybackEngine::cancelOutside(qint64 first, qint64 last)
{
    const QString key = currentKey();
    for (Flight &flight : m_flights) {
        if (flight.cancelled) continue;
        if (flight.key != key || flight.frame < first || flight.frame > last) {
            flight.cancel.cancel();
            flight.cancelled = true;
        }
    }
}

void MediaPlaybackEngine::present(const MediaRenderedFrame &rendered)
{
    m_presented = rendered;
    m_presentedFrame = rendered.frame;
    m_presentedKey = currentKey();
    m_presentedProgram = m_program; // only current-program results are presented
    ++m_presentedRevision;
    ++m_stats.presented;
    emit framePresented();
    emit statisticsChanged();
}

void MediaPlaybackEngine::remember(const QString &key, qint64 frame, const MediaRenderedFrame &rendered)
{
    if (m_cacheLimit <= 0 || rendered.image.isNull()) return;
    for (CacheEntry &entry : m_cache) {
        if (entry.key == key && entry.frame == frame) {
            entry.rendered = rendered;
            entry.bytes = rendered.image.sizeInBytes();
            entry.used = ++m_cacheClock;
            return;
        }
    }
    CacheEntry entry;
    entry.key = key;
    entry.frame = frame;
    entry.rendered = rendered;
    entry.bytes = rendered.image.sizeInBytes();
    entry.used = ++m_cacheClock;
    m_cache.append(entry);
    trimCache();
}

void MediaPlaybackEngine::trimCache()
{
    qint64 bytes = 0;
    for (const CacheEntry &entry : std::as_const(m_cache)) bytes += entry.bytes;
    while (!m_cache.isEmpty() && (m_cache.size() > m_cacheLimit || bytes > kMaxCacheBytes)) {
        const auto oldest = std::min_element(m_cache.begin(), m_cache.end(),
            [](const CacheEntry &a, const CacheEntry &b) { return a.used < b.used; });
        bytes -= oldest->bytes;
        m_cache.erase(oldest);
    }
}

void MediaPlaybackEngine::onResult(MediaWorkerResult result)
{
    if (result.kind == MediaWorkerJob::Kind::Probe) {
        const PendingProbe probe = m_probes.take(result.serial);
        if (probe.done) probe.done(result.info, result.error);
        return;
    }
    const auto it = m_flights.find(result.serial);
    if (it == m_flights.end()) return;
    const Flight flight = it.value();
    m_flights.erase(it);
    const QString key = currentKey();
    const bool wanted = flight.key == key && flight.frame == m_targetFrame
        && (m_presentedFrame != flight.frame || m_presentedKey != key);
    if (result.cancelled) {
        ++m_stats.cancelled;
        emit statisticsChanged();
        return;
    }
    if (result.rendered.image.isNull()) {
        // A failed target is presented as a failure: the view never keeps
        // showing the previous frame under the new identity.
        ++m_stats.failed;
        if (wanted) {
            MediaRenderedFrame failure = result.rendered;
            failure.frame = flight.frame;
            if (!failure.error) failure.error = result.error;
            present(failure);
        } else {
            emit statisticsChanged();
        }
        return;
    }
    remember(flight.key, flight.frame, result.rendered);
    if (wanted) {
        present(result.rendered);
        return;
    }
    const qint64 last = m_state.playing ? m_targetFrame + kPrefetchFrames : m_targetFrame;
    if (flight.key != key || flight.frame < m_targetFrame || flight.frame > last) {
        ++m_stats.stale;
        emit statisticsChanged();
    }
}

void MediaPlaybackEngine::probe(const QString &uri, std::function<void(MediaSourceInfoPtr, OperationError)> done)
{
    for (const PendingProbe &older : std::as_const(m_probes)) older.cancel.cancel();
    MediaWorkerJob job;
    job.kind = MediaWorkerJob::Kind::Probe;
    job.serial = ++m_serial;
    job.uri = uri;
    m_probes.insert(job.serial, PendingProbe{std::move(done), job.cancel});
    m_worker->submit(std::move(job));
}

} // namespace QindaTK
