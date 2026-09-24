// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_engine.h"

#include "audio_engine_p.h"

#include <QDir>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace QindaTK {
namespace {

QVariantMap statisticsMap(const AudioDeviceStream &stream, const AudioStreamFormat &format, const QString &deviceId)
{
    const AudioCycleStats &stats = stream.stats();
    const double callbacks = double(stats.callbacks.load());
    return {{QStringLiteral("deviceId"), deviceId},
            {QStringLiteral("callbacks"), callbacks},
            {QStringLiteral("xruns"), double(stats.xruns.load())},
            {QStringLiteral("lateCallbacks"), double(stats.lateCallbacks.load())},
            {QStringLiteral("maxCallbackUs"), double(stats.maxCallbackNs.load()) / 1000.0},
            {QStringLiteral("meanCallbackUs"), callbacks > 0 ? double(stats.totalCallbackNs.load()) / 1000.0 / callbacks : 0.0},
            {QStringLiteral("budgetUs"), format.sampleRate > 0 ? 1e6 * format.bufferSize / format.sampleRate : 0.0},
            {QStringLiteral("maxWakeLatenessUs"), double(stats.maxWakeLatenessNs.load()) / 1000.0},
            {QStringLiteral("realtimePriority"), stats.realtimePriority.load()},
            {QStringLiteral("hardwareClock"), format.hardwareClock},
            {QStringLiteral("sampleRate"), format.sampleRate},
            {QStringLiteral("bufferSize"), format.bufferSize},
            {QStringLiteral("inputLatencyFrames"), format.inputLatency},
            {QStringLiteral("outputLatencyFrames"), format.outputLatency},
            {QStringLiteral("roundTripLatencyFrames"), format.inputLatency + format.outputLatency}};
}

qint64 rescaled(qint64 value, int from, int to)
{
    return TimelineTime::rescale(value, {from, 1}, {to, 1}, TimeMap::NearestAway).value_or(0);
}

} // namespace

AudioEngine::AudioEngine(QObject *parent)
    : TransportAdapter(parent),
      m_builtin(std::make_shared<BuiltinAudioBackend>()),
      m_sharedDirectory(std::make_shared<SharedDirectory>())
{
    m_backends.push_back(m_builtin);
    if (auto alsa = createAlsaAudioBackend()) m_backends.push_back(alsa);
    m_recordDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("qindatk-takes"));
    m_sharedDirectory->set(m_recordDirectory);
    m_recordReason = tr("No track is armed for recording");
    m_poll.setInterval(15);
    connect(&m_poll, &QTimer::timeout, this, &AudioEngine::poll);
    m_deviceWatch.setInterval(1000);
    connect(&m_deviceWatch, &QTimer::timeout, this, &AudioEngine::refreshDevices);
    connect(&m_watcher, &QFutureWatcherBase::finished, this, &AudioEngine::finishJob);
    m_transport = new TransportModel(this);
    m_transport->setAdapter(this);
    refreshDevices();
}

AudioEngine::~AudioEngine()
{
    m_transport->setAdapter(nullptr);
    m_poll.stop();
    m_deviceWatch.stop();
    m_watcher.waitForFinished();
    m_job.reset();
    if (m_run) {
        m_run->stream->stop();
        m_run->processor->closeAfterStop();
        if (m_run->writer) m_run->writer->stop();
        m_run.reset();
    }
}

void AudioEngine::setBackends(const QList<AudioDeviceBackendPtr> &backends)
{
    if (m_run || m_job) return;
    m_backends = backends;
    m_builtin.reset();
    for (const auto &backend : backends)
        if (auto builtin = std::dynamic_pointer_cast<BuiltinAudioBackend>(backend)) m_builtin = builtin;
    refreshDevices();
}

BuiltinAudioBackend *AudioEngine::builtinBackend() const
{
    return m_builtin.get();
}

AudioDeviceBackendPtr AudioEngine::backendFor(const QString &deviceId) const
{
    const QString prefix = deviceId.section(QLatin1Char(':'), 0, 0);
    for (const auto &backend : m_backends)
        if (backend->name() == prefix) return backend;
    return {};
}

void AudioEngine::refreshDevices()
{
    QVariantList list;
    const QString activeId = m_active ? m_active->deviceId : QString();
    bool listed = false;
    for (const auto &backend : std::as_const(m_backends))
        for (const AudioDeviceInfo &device : backend->devices()) {
            if (device.id == activeId) {
                listed = true;
                m_activeName = device.name;
            }
            list.push_back(device.toMap());
        }
    if (!activeId.isEmpty() && !listed) {
        // A vanished device stays listed by its stable ID: the panel keeps
        // showing it instead of drifting to whatever device took its index.
        AudioDeviceInfo gone;
        gone.id = activeId;
        gone.name = m_activeName.isEmpty() ? activeId : m_activeName;
        gone.backend = activeId.section(QLatin1Char(':'), 0, 0);
        gone.available = false;
        gone.unavailableReason = tr("Disconnected");
        list.push_back(gone.toMap());
    }
    if (list != m_devices) {
        m_devices = list;
        emit devicesChanged();
    }
}

QVariantMap AudioEngine::activeConfiguration() const
{
    return m_active ? m_active->toMap() : QVariantMap{};
}

QString AudioEngine::stateName() const
{
    switch (m_state) {
    case State::Configuring: return QStringLiteral("configuring");
    case State::Running: return QStringLiteral("running");
    case State::Lost: return QStringLiteral("lost");
    case State::Error: return QStringLiteral("error");
    case State::Stopped: break;
    }
    return QStringLiteral("stopped");
}

void AudioEngine::setRecordDirectory(const QString &directory)
{
    if (directory == m_recordDirectory) return;
    m_recordDirectory = directory;
    m_sharedDirectory->set(directory);
    emit recordDirectoryChanged();
}

void AudioEngine::setRecordTarget(bool available, const QString &reason)
{
    m_recordAvailable = available;
    m_recordReason = available ? QString() : reason.isEmpty() ? tr("No track is armed for recording") : reason;
    publishSnapshot(false);
}

AudioStreamFormat AudioEngine::streamFormat() const
{
    return m_run ? m_run->format : AudioStreamFormat{};
}

quint64 AudioEngine::realtimeThreadToken() const
{
    return m_run && m_run->stream ? m_run->stream->stats().threadToken.load() : 0;
}

bool AudioEngine::setBuiltinDeviceConnected(const QString &deviceId, bool connected)
{
    const bool known = m_builtin && m_builtin->setConnected(deviceId, connected);
    if (known) refreshDevices();
    return known;
}

bool AudioEngine::applyConfiguration(const QString &requestId, const QVariantMap &configuration)
{
    QString error;
    const auto config = AudioDeviceConfig::fromMap(configuration, &error);
    const auto reject = [&](const QString &message) {
        emit configurationApplied(requestId, QStringLiteral("rejected"), activeConfiguration(), message);
        return false;
    };
    if (!config) return reject(error);
    if (!backendFor(config->deviceId)) return reject(tr("No audio backend provides %1").arg(config->deviceId));
    if (m_job && m_job->kind != JobKind::Apply && !m_queuedApply) {
        // A stop or loss teardown is closing the old device; apply right after it.
        m_queuedApply = std::make_pair(requestId, *config);
        return true;
    }
    if (m_job) return reject(tr("Another device change is in progress"));
    if (m_run && m_run->processor->recording()) return reject(tr("Stop recording before changing the audio device"));
    startJob(JobKind::Apply, requestId, config);
    return true;
}

void AudioEngine::stop()
{
    if (m_job) return;
    if (m_run) startJob(JobKind::Stop, {}, std::nullopt);
    else setState(State::Stopped, {});
}

void AudioEngine::startJob(JobKind kind, const QString &requestId, std::optional<AudioDeviceConfig> next)
{
    m_poll.stop();
    auto job = std::make_unique<Job>();
    job->kind = kind;
    job->requestId = requestId;
    job->next = next;
    job->previous = m_active;
    job->previousState = m_state;
    job->previousMessage = m_stateMessage;
    if (m_run) drain(*m_run);
    job->old = std::move(m_run);
    const AudioDeviceBackendPtr nextBackend = next ? backendFor(next->deviceId) : nullptr;
    const AudioDeviceBackendPtr oldBackend = job->old ? backendFor(job->old->config.deviceId) : nullptr;
    Job *raw = job.get();
    m_job = std::move(job);
    if (kind == JobKind::Lost) setState(State::Lost, m_stateMessage);
    else setState(State::Configuring, next ? tr("Opening %1").arg(next->deviceId) : tr("Stopping the audio device"));
    publishSnapshot(false);
    // AGENT-GUARD: devices open, stop and close here, never on the GUI or
    // device thread; the GUI touches `raw` again only in finishJob().
    m_watcher.setFuture(QtConcurrent::run([raw, nextBackend, oldBackend] {
        if (raw->old) {
            raw->old->stream->stop();
            raw->old->processor->closeAfterStop();
            if (raw->old->writer) raw->old->writer->stop();
            raw->finalStatistics = statisticsMap(*raw->old->stream, raw->old->format, raw->old->config.deviceId);
            raw->old->stream.reset();
        }
        if (raw->next && nextBackend) {
            raw->opened = nextBackend->open(*raw->next, &raw->error);
            if (!raw->opened && raw->old && oldBackend) {
                QString ignored;
                raw->rollback = oldBackend->open(raw->old->config, &ignored);
            }
        }
    }));
}

void AudioEngine::finishJob()
{
    std::unique_ptr<Job> job = std::move(m_job);
    if (!job) return;
    const int previousRate = m_rate;
    if (job->old) {
        const EngineTransportState last = job->old->processor->stateAfterStop();
        m_idle = {last.position, last.loopEnabled, last.loopStart, last.loopEnd};
        drain(*job->old);
        m_statistics = job->finalStatistics;
        emit statisticsChanged();
        job->old.reset();
    }
    m_lost = false;
    m_live.clear();
    rejectPending(tr("The audio device stopped before the command ran"));
    if (job->kind == JobKind::Apply) {
        QString state, message;
        if (job->opened) {
            const QString error = startRun(job->opened, *job->next, previousRate);
            state = error.isEmpty() ? QStringLiteral("accepted") : QStringLiteral("error");
            message = error.isEmpty() ? tr("%1 is running at %2 Hz with %3-frame periods")
                                            .arg(m_activeName.isEmpty() ? m_active->deviceId : m_activeName)
                                            .arg(m_rate).arg(m_bufferSize)
                                      : error;
            setState(error.isEmpty() ? State::Running : State::Error, message);
        } else if (job->rollback && job->previous) {
            const QString error = startRun(job->rollback, *job->previous, previousRate);
            state = QStringLiteral("rejected");
            message = tr("%1. The previous configuration is still active.").arg(job->error);
            setState(error.isEmpty() ? State::Running : State::Error, error.isEmpty() ? message : error);
        } else {
            // Nothing was running (first open, or recovery after a loss): the
            // request is refused and the earlier state, e.g. Lost, remains.
            const bool hadRun = !job->finalStatistics.isEmpty();
            state = hadRun ? QStringLiteral("error") : QStringLiteral("rejected");
            message = job->error;
            if (hadRun) setState(State::Error, job->error);
            else setState(job->previousState == State::Lost ? State::Lost : State::Stopped,
                          job->previousState == State::Lost ? job->previousMessage : job->error);
        }
        emit configurationApplied(job->requestId, state, activeConfiguration(), message);
    } else if (job->kind == JobKind::Stop) {
        m_active.reset(); // nothing is active after an explicit stop; a lost device stays active
        setState(State::Stopped, tr("The audio device is stopped"));
    }
    refreshDevices();
    publishSnapshot(false);
    if (m_queuedApply) {
        const auto [requestId, config] = *std::exchange(m_queuedApply, std::nullopt);
        startJob(JobKind::Apply, requestId, config);
    }
}

QString AudioEngine::startRun(AudioDeviceStreamPtr stream, const AudioDeviceConfig &config, int previousRate)
{
    auto run = std::make_shared<Run>();
    run->serial = ++m_runSerial;
    run->format = stream->format();
    run->config = config;
    run->config.bufferSize = run->format.bufferSize;
    run->stream = std::move(stream);
    if (previousRate > 0 && previousRate != run->format.sampleRate) {
        // Session clock: positions keep their time across a rate change.
        m_idle.position = rescaled(m_idle.position, previousRate, run->format.sampleRate);
        m_idle.loopStart = rescaled(m_idle.loopStart, previousRate, run->format.sampleRate);
        m_idle.loopEnd = rescaled(m_idle.loopEnd, previousRate, run->format.sampleRate);
    }
    run->processor = std::make_unique<AudioEngineProcessor>(
        run->format, EngineTransportState{m_idle.position, m_idle.loopEnabled, m_idle.loopStart, m_idle.loopEnd});
    if (run->format.inputChannels > 0) {
        const auto directory = m_sharedDirectory;
        const quint64 serial = run->serial;
        run->writer = std::make_unique<AudioTakeWriter>(
            *run->processor, [directory] { return directory->get(); },
            [this, serial](quint64 take, const QString &path, qint64 frames, const QString &error) {
                const QString key = QStringLiteral("%1:%2").arg(serial).arg(take);
                QMetaObject::invokeMethod(this, [this, key, path, frames, error] { takeWritten(key, path, frames, error); },
                                          Qt::QueuedConnection);
            });
    }
    m_active = run->config;
    m_rate = run->format.sampleRate;
    m_bufferSize = run->format.bufferSize;
    m_run = run;
    emit formatChanged(); // the session recompiles at this rate and calls setGraph()
    offerGraph();
    QString error;
    if (!run->stream->start(run->processor.get(), &error)) {
        run->processor->closeAfterStop();
        if (run->writer) run->writer->stop();
        m_run.reset();
        return error;
    }
    m_poll.start();
    return {};
}

void AudioEngine::poll()
{
    if (!m_run) return;
    drain(*m_run);
    if (m_lost && !m_job) {
        startJob(JobKind::Lost, {}, std::nullopt);
        return;
    }
    retireGraphs();
    publishSnapshot(false);
    if (++m_statisticsTicks % 16 == 0) updateStatistics();
}

void AudioEngine::setGraph(AudioRenderGraphPtr graph)
{
    m_graph = std::move(graph);
    offerGraph();
}

void AudioEngine::offerGraph()
{
    if (!m_run || !m_graph || m_graph->sampleRate() != m_run->format.sampleRate) return;
    if (!m_live.isEmpty() && m_live.last() == m_graph) return;
    m_live.push_back(m_graph);
    if (const AudioRenderGraph *stale = m_run->processor->offer(m_graph.get()))
        m_live.removeIf([stale](const AudioRenderGraphPtr &graph) { return graph.get() == stale; });
    retireGraphs();
}

void AudioEngine::retireGraphs()
{
    if (!m_run) return;
    // AGENT-GUARD: generations only grow, and the device thread adopts in
    // offer order, so every graph older than the adopted one is unreachable.
    const quint64 adopted = m_run->processor->adoptedGeneration();
    m_live.removeIf([adopted](const AudioRenderGraphPtr &graph) { return graph->generation() < adopted; });
}

void AudioEngine::updateStatistics()
{
    if (!m_run || !m_run->stream) return;
    m_statistics = statisticsMap(*m_run->stream, m_run->format, m_run->config.deviceId);
    emit statisticsChanged();
}

void AudioEngine::setState(State state, const QString &message)
{
    if (state == m_state && message == m_stateMessage) return;
    m_state = state;
    m_stateMessage = message;
    if (state == State::Lost) m_deviceWatch.start();
    else m_deviceWatch.stop();
    emit stateChanged();
}

} // namespace QindaTK
