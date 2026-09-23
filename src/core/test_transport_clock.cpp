// SPDX-License-Identifier: LGPL-3.0-or-later
#include "test_transport_clock.h"

#include <limits>
#include <cmath>

namespace QindaTK {
TestTransportClock::TestTransportClock(QObject *parent) : TransportAdapter(parent) {
    m_state.revision = 1;
    m_state.canPlay = true;
    m_state.canSeek = true;
    m_state.canRecord = true;
    m_state.canLoop = true;
    m_state.canPunch = true;
}
void TestTransportClock::submit(const TransportCommand &command, ReplyCallback completion) {
    m_pending.insert(command.id, {command, std::move(completion), {}});
}
bool TestTransportClock::process(const QString &requestId) {
    auto it = m_pending.find(requestId);
    if (it == m_pending.end() || it->reply) return false;
    const TransportCommand &command = it->command;
    TransportReply reply;
    reply.requestId = requestId;
    reply.accepted = true;
    switch (command.kind) {
    case TransportCommand::Kind::Play:
        if (!m_state.deviceError.isEmpty() || !m_state.canPlay) {
            reply.accepted = false;
            reply.error = m_state.deviceError.isEmpty() ? QStringLiteral("Playback unavailable") : m_state.deviceError;
        } else m_state.playing = true;
        break;
    case TransportCommand::Kind::Stop:
        m_state.playing = false;
        m_state.recording = false;
        break;
    case TransportCommand::Kind::Seek:
        if (!m_state.canSeek) { reply.accepted = false; reply.error = QStringLiteral("Seeking unavailable"); }
        else m_state.positionSample = command.positionSample;
        break;
    case TransportCommand::Kind::Record:
        if (command.enabled && (m_denyRecording || !m_state.canRecord || !m_state.deviceError.isEmpty())) {
            reply.accepted = false;
            reply.error = m_state.deviceError.isEmpty() ? QStringLiteral("Recording denied") : m_state.deviceError;
        } else {
            m_state.recording = command.enabled;
            if (command.enabled) m_state.playing = true;
        }
        break;
    case TransportCommand::Kind::Loop:
        m_state.loopEnabled = command.enabled;
        m_state.loopStart = command.rangeStart;
        m_state.loopEnd = command.rangeEnd;
        break;
    case TransportCommand::Kind::Punch:
        m_state.punchEnabled = command.enabled;
        m_state.punchStart = command.rangeStart;
        m_state.punchEnd = command.rangeEnd;
        break;
    }
    ++m_state.revision;
    reply.snapshot = m_state;
    it->reply = std::move(reply);
    return true;
}
bool TestTransportClock::deliver(const QString &requestId) {
    auto it = m_pending.find(requestId);
    if (it == m_pending.end() || !it->reply) return false;
    auto callback = std::move(it->callback);
    const TransportReply reply = *it->reply;
    m_pending.erase(it);
    if (callback) callback(reply);
    return true;
}
bool TestTransportClock::advance(qint64 samples) {
    if (samples < 0) return false;
    if (!m_state.playing || samples == 0) return true;
    if (m_state.positionSample > std::numeric_limits<qint64>::max() - samples) return false;
    m_state.positionSample += samples;
    ++m_state.revision;
    push();
    return true;
}
bool TestTransportClock::setPlaybackRate(double rate) {
    if (!std::isfinite(rate) || rate <= 0 || rate > 16) return false;
    m_state.playbackRate = rate;
    ++m_state.revision;
    push();
    return true;
}
void TestTransportClock::externalState(qint64 positionSample, bool playing, bool recording) {
    m_state.positionSample = positionSample;
    m_state.playing = playing;
    m_state.recording = recording;
    ++m_state.revision;
    push();
}
void TestTransportClock::setCanRecord(bool available, const QString &reason) {
    m_state.canRecord = available;
    m_state.recordUnavailableReason = available ? QString() : reason;
    ++m_state.revision;
    push();
}
void TestTransportClock::setCanSeek(bool available, const QString &reason) {
    m_state.canSeek = available;
    m_state.seekUnavailableReason = available ? QString() : reason;
    ++m_state.revision;
    push();
}
void TestTransportClock::setDeviceError(const QString &error) {
    m_state.deviceError = error;
    if (!error.isEmpty()) {
        m_state.playing = false;
        m_state.recording = false;
    }
    ++m_state.revision;
    push();
}
void TestTransportClock::push() { if (m_sink) m_sink(m_state); }
} // namespace QindaTK
