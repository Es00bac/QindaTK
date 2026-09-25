// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioEngine: TransportAdapter side and device-thread event handling.
#include "audio_engine.h"

#include "audio_engine_p.h"

#include <tuple>
#include <utility>

namespace QindaTK {

QString AudioEngine::eventText(int code)
{
    switch (code) {
    case EngineEvent::InvalidPosition: return tr("The position is before the session start");
    case EngineEvent::InvalidRange: return tr("The loop range is empty or negative");
    case EngineEvent::NoInput: return tr("The device configuration has no input");
    case EngineEvent::TakeBusy: return tr("The previous take is still being written");
    default: return {};
    }
}

namespace {

auto visible(const TransportSnapshot &s)
{
    return std::tie(s.positionSample, s.playing, s.recording, s.loopEnabled, s.loopStart, s.loopEnd,
                    s.canPlay, s.canSeek, s.canRecord, s.canLoop, s.playUnavailableReason,
                    s.recordUnavailableReason, s.deviceError, s.punchEnabled, s.punchStart, s.punchEnd, s.canPunch);
}

} // namespace

void AudioEngine::drain(Run &run)
{
    drainJumps(run);
    EngineEvent event;
    while (run.processor->takeEvent(event)) handleEvent(run, event);
    drainMidi(run);
}

void AudioEngine::handleEvent(Run &run, const EngineEvent &event)
{
    const QString key = QStringLiteral("%1:%2").arg(run.serial).arg(event.serial);
    switch (event.kind) {
    case EngineEvent::Ack: {
        const auto it = m_pending.find(event.serial);
        if (it == m_pending.end()) return;
        const Pending pending = it.value();
        m_pending.erase(it);
        if (event.accepted && pending.command.kind == TransportCommand::Kind::Loop) {
            m_idle.loopEnabled = pending.command.enabled;
            if (pending.command.enabled) {
                m_idle.loopStart = pending.command.rangeStart;
                m_idle.loopEnd = pending.command.rangeEnd;
            }
        }
        publishSnapshot(true);
        if (pending.callback)
            pending.callback({pending.id, event.accepted, event.accepted ? QString() : eventText(event.code), m_last});
        return;
    }
    case EngineEvent::RecordBegin: {
        auto &take = m_takes[key];
        if (!take) take = std::make_shared<Take>();
        take->position = event.position;
        take->latency = run.format.inputLatency + run.format.outputLatency;
        take->sampleRate = run.format.sampleRate;
        take->channels = run.format.inputChannels;
        take->loopEnabled = m_idle.loopEnabled;
        take->loopStart = m_idle.loopStart;
        take->loopEnd = m_idle.loopEnd;
        take->punchEnabled = m_idle.punchEnabled;
        take->punchStart = m_idle.punchStart;
        take->punchEnd = m_idle.punchEnd;
        return;
    }
    case EngineEvent::RecordEnd: {
        auto &take = m_takes[key];
        if (!take) take = std::make_shared<Take>();
        // AGENT-GUARD: every jump of this take was pushed before its end
        // event, so draining here (not only at the next poll) sees them all.
        drainJumps(run);
        take->dropped = event.dropped;
        take->jumpsDropped = event.jumpsDropped;
        take->deviceLost = event.code == EngineEvent::Lost;
        take->ended = true;
        return completeTake(key);
    }
    case EngineEvent::DeviceLost: {
        m_stateMessage = tr("%1 was lost: %2").arg(m_activeName.isEmpty() ? run.config.deviceId : m_activeName,
                                                     QString::fromUtf8(event.reason));
        m_lost = true;
        return;
    }
    default:
        return;
    }
}

void AudioEngine::drainJumps(Run &run)
{
    RecordJump jump;
    while (run.processor->takeJump(jump)) {
        auto &take = m_takes[QStringLiteral("%1:%2").arg(run.serial).arg(jump.take)];
        if (!take) take = std::make_shared<Take>();
        take->jumps.push_back({jump.frame, jump.position});
    }
}

void AudioEngine::takeWritten(const QString &key, const QString &path, qint64 frames, const QString &error)
{
    auto &take = m_takes[key];
    if (!take) take = std::make_shared<Take>();
    take->path = path;
    take->written = true;
    take->error = error;
    take->frames = frames;
    completeTake(key);
}

void AudioEngine::completeTake(const QString &key)
{
    const auto take = m_takes.value(key);
    if (!take || !take->ended || !take->written) return;
    m_takes.remove(key);
    QVariantList jumps;
    for (const auto &[frame, position] : std::as_const(take->jumps))
        jumps.push_back(QVariantMap{{QStringLiteral("frame"), QString::number(frame)},
                                    {QStringLiteral("position"), QString::number(position)}});
    emit recordingFinished({{QStringLiteral("path"), take->path},
                            {QStringLiteral("startSample"), QString::number(take->position - take->latency)},
                            {QStringLiteral("recordedSample"), QString::number(take->position)},
                            {QStringLiteral("frames"), QString::number(take->frames)},
                            {QStringLiteral("sampleRate"), take->sampleRate},
                            {QStringLiteral("channels"), take->channels},
                            {QStringLiteral("droppedFrames"), QString::number(take->dropped)},
                            {QStringLiteral("latencyFrames"), take->latency},
                            {QStringLiteral("deviceLost"), take->deviceLost},
                            {QStringLiteral("error"), take->error},
                            {QStringLiteral("jumps"), jumps},
                            {QStringLiteral("jumpsDropped"), QString::number(take->jumpsDropped)},
                            {QStringLiteral("loopEnabled"), take->loopEnabled},
                            {QStringLiteral("loopStart"), QString::number(take->loopStart)},
                            {QStringLiteral("loopEnd"), QString::number(take->loopEnd)},
                            {QStringLiteral("punchEnabled"), take->punchEnabled},
                            {QStringLiteral("punchStart"), QString::number(take->punchStart)},
                            {QStringLiteral("punchEnd"), QString::number(take->punchEnd)}});
}

void AudioEngine::rejectPending(const QString &reason)
{
    if (m_pending.isEmpty()) return;
    const auto pending = std::exchange(m_pending, {});
    publishSnapshot(true);
    for (const Pending &entry : pending)
        if (entry.callback) entry.callback({entry.id, false, reason, m_last});
}

TransportSnapshot AudioEngine::compose() const
{
    TransportSnapshot s;
    s.revision = m_revision;
    const bool running = m_run && m_state == State::Running;
    s.positionSample = m_run ? m_run->processor->position() : m_idle.position;
    s.playing = m_run && m_run->processor->playing();
    s.recording = m_run && m_run->processor->recording();
    s.loopEnabled = m_idle.loopEnabled;
    s.loopStart = m_idle.loopStart;
    s.loopEnd = m_idle.loopEnd;
    s.canPlay = running;
    s.playUnavailableReason = running ? QString()
                              : m_job ? tr("The audio device is being reconfigured")
                                      : tr("No audio device is running");
    s.canSeek = s.canLoop = !m_job;
    s.seekUnavailableReason = s.loopUnavailableReason = m_job ? s.playUnavailableReason : QString();
    const bool input = m_run && m_run->format.inputChannels > 0;
    s.canRecord = running && input && m_recordAvailable;
    s.recordUnavailableReason = !running ? s.playUnavailableReason
                                : !input ? eventText(EngineEvent::NoInput)
                                         : m_recordReason;
    s.punchEnabled = m_idle.punchEnabled;
    s.punchStart = m_idle.punchStart;
    s.punchEnd = m_idle.punchEnd;
    s.canPunch = true;
    s.deviceError = m_state == State::Lost || m_state == State::Error ? m_stateMessage : QString();
    return s;
}

void AudioEngine::publishSnapshot(bool force)
{
    TransportSnapshot next = compose();
    if (!force && m_revision > 0 && visible(next) == visible(m_last)) return;
    next.revision = ++m_revision;
    m_last = next;
    if (m_sink) m_sink(m_last);
}

TransportSnapshot AudioEngine::snapshot() const
{
    return compose(); // live: the device thread publishes its position atomically
}

void AudioEngine::setSnapshotSink(SnapshotCallback sink)
{
    m_sink = std::move(sink);
}

void AudioEngine::submit(const TransportCommand &command, ReplyCallback completion)
{
    const auto reply = [&](bool accepted, const QString &error) {
        publishSnapshot(accepted);
        if (completion) completion({command.id, accepted, error, m_last});
    };
    using Kind = TransportCommand::Kind;
    if (command.kind == Kind::Punch) {
        // AGENT-NOTE (D-177): punch is non-destructive. The device records
        // the whole pass; the session selects [punchStart, punchEnd) of the
        // new takes in a comp, so pre- and post-roll stay available.
        if (command.enabled && (command.rangeStart < 0 || command.rangeEnd <= command.rangeStart))
            return reply(false, eventText(EngineEvent::InvalidRange));
        m_idle.punchEnabled = command.enabled;
        if (command.enabled) {
            m_idle.punchStart = command.rangeStart;
            m_idle.punchEnd = command.rangeEnd;
        }
        return reply(true, {});
    }
    if (m_job) return reply(false, tr("The audio device is being reconfigured"));
    if (!m_run) {
        switch (command.kind) {
        case Kind::Seek:
            if (command.positionSample < 0) return reply(false, eventText(EngineEvent::InvalidPosition));
            m_idle.position = command.positionSample;
            return reply(true, {});
        case Kind::Loop:
            if (command.enabled && (command.rangeStart < 0 || command.rangeEnd <= command.rangeStart))
                return reply(false, eventText(EngineEvent::InvalidRange));
            m_idle.loopEnabled = command.enabled;
            if (command.enabled) {
                m_idle.loopStart = command.rangeStart;
                m_idle.loopEnd = command.rangeEnd;
            }
            return reply(true, {});
        case Kind::Stop:
            return reply(true, {});
        default:
            if (command.kind == Kind::Record && !command.enabled) return reply(true, {});
            return reply(false, compose().playUnavailableReason);
        }
    }
    if (command.kind == Kind::Record && command.enabled && !m_recordAvailable) return reply(false, m_recordReason);
    if (m_pending.size() >= qsizetype(AudioEngineProcessor::kCommandCapacity))
        return reply(false, tr("Too many transport commands are waiting for the audio device"));
    EngineCommand engine;
    engine.serial = ++m_commandSerial;
    engine.kind = command.kind == Kind::Play ? EngineCommand::Play
                  : command.kind == Kind::Stop ? EngineCommand::Stop
                  : command.kind == Kind::Seek ? EngineCommand::Seek
                  : command.kind == Kind::Record ? EngineCommand::Record
                                                 : EngineCommand::Loop;
    engine.position = command.positionSample;
    engine.rangeStart = command.rangeStart;
    engine.rangeEnd = command.rangeEnd;
    engine.enabled = command.enabled;
    if (!m_run->processor->post(engine)) return reply(false, tr("The audio command queue is full"));
    m_pending.insert(engine.serial, {command.id, command, std::move(completion)});
}

} // namespace QindaTK
