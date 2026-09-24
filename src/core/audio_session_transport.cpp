// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioSession: the engine clock for automation (WP-061).
#include "audio_session.h"

namespace QindaTK {

qint64 AudioSession::transportTicks() const
{
    if (m_engine) {
        const qint64 sample = m_engine->snapshot().positionSample;
        return TimelineTime::rescale(sample, {sampleRate(), 1}, m_timeline->timebase(), TimeMap::NearestAway)
            .value_or(m_forwardedTicks);
    }
    return TimelineTime::parseDecimal(m_automation->transportTicksText()).value_or(0);
}

void AudioSession::forwardTransport()
{
    // AGENT-GUARD (WP-061): automation passes follow the acknowledged engine
    // transport state (start, stop, seeks, loop wraps), timed by the device
    // thread's live sample: the clock AudioParameterBinding stamps gestures
    // with. The coalesced TransportModel position can lag a gesture sample by
    // up to 33 ms; AutomationModel would take that as a loop wrap and split
    // the pass (docs/tempo-automation.md).
    const TransportModel *transport = m_engine ? m_engine->transport() : nullptr;
    if (!transport) return;
    const qint64 ticks = transportTicks();
    const bool playing = transport->playing();
    if (playing && !m_automation->transportRunning()) m_automation->transportStarted(ticks);
    else if (!playing && m_automation->transportRunning()) m_automation->transportStopped(ticks);
    else if (ticks != m_forwardedTicks) m_automation->transportMoved(ticks);
    m_forwardedTicks = ticks;
}

} // namespace QindaTK
