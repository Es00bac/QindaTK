// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioRenderGraph: MIDI tracks (WP-064, docs/audio-midi.md). compileMidi()
// runs on the GUI thread; addMidi()/playPlacement() run inside render() on
// the device thread and never allocate, lock or perform I/O.
#include "audio_graph.h"

#include "timeline_model.h"

#include <QHash>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace QindaTK {

void AudioRenderGraph::compileMidi(const Sources &sources, qsizetype trackCount)
{
    m_clock = MidiClock::build(sources.schedule.get(), sources.sampleRate);
    m_attack = MidiTestInstrument::attackFrames(sources.sampleRate);
    m_release = MidiTestInstrument::releaseFrames(sources.sampleRate);
    for (int pitch = 0; pitch < 128; ++pitch) m_midiPhase[pitch] = MidiTestInstrument::phaseStep(pitch, sources.sampleRate);
    const TimelineModel *timeline = sources.timeline;
    const MidiContent empty;
    const MidiContent &content = sources.midi ? *sources.midi : empty;
    QHash<QUuid, int> programs;
    const auto programFor = [&](const QUuid &id) {
        if (const auto found = programs.constFind(id); found != programs.cend()) return *found;
        const MidiPattern *pattern = Midi::find(content, id);
        const int index = pattern ? int(m_programs.size()) : -1;
        if (pattern) m_programs.push_back(MidiProgram::compile(*pattern));
        programs.insert(id, index);
        return index;
    };
    const TimelineRate rate{sources.sampleRate, 1};
    const auto toSamples = [&](qint64 ticks) { return TimelineTime::rescale(ticks, m_timebase, rate, TimeMap::NearestAway); };
    for (qsizetype i = 0; timeline && i < trackCount; ++i) {
        Node &node = m_nodes[std::size_t(i)];
        const QUuid trackId = AudioMixer::trackOf(node.channelId);
        const TimelineTrack *track = timeline->trackRecord(trackId);
        if (!track || track->kind != QLatin1String("midi")) continue;
        node.midi = true;
        node.midiLane = Midi::key(trackId);
        node.placementFirst = m_placements.size();
        for (const auto &entry : timeline->trackIndex(trackId)) {
            const TimelineClip *clip = node.audible ? timeline->clipRecord(entry.id) : nullptr;
            const QUuid patternId = clip ? Midi::patternFromResource(clip->resourceId) : QUuid();
            if (patternId.isNull()) continue;
            const int program = programFor(patternId);
            if (program < 0) {
                if (!m_missing.contains(clip->resourceId)) m_missing.push_back(clip->resourceId);
                continue;
            }
            const auto from = toSamples(clip->start);
            const auto to = toSamples(clip->end());
            if (!from || !to || *to <= *from) continue;
            // AGENT-NOTE: the pattern origin is the musical tick where the
            // clip's source would start (start - sourceOffset), so a left trim
            // keeps notes in place; a beat-locked clip uses its anchor exactly.
            qint64 origin = clip->musicalTick;
            if (clip->anchor != TimelineClip::Anchor::Musical || clip->sourceOffset != 0 || clip->musicalOffset != 0) {
                const auto originSample = toSamples(clip->start - clip->sourceOffset);
                if (!originSample) continue;
                origin = m_clock.nearestTick(*originSample);
            }
            m_placements.push_back({program, origin, *from, *to});
            m_length = std::max(m_length, *to + m_release);
        }
        node.placementCount = m_placements.size() - node.placementFirst;
    }
    for (const MidiSlot &slot : content.launchSlots)
        if (const int program = programFor(slot.patternId); program >= 0)
            m_slots.push_back({Midi::key(slot.id), Midi::key(slot.trackId), program});
    std::sort(m_slots.begin(), m_slots.end(), [](const SlotProgram &a, const SlotProgram &b) { return a.slot < b.slot; });
}

int AudioRenderGraph::launchProgram(quint64 slot, quint64 lane) const noexcept
{
    const auto found = std::lower_bound(m_slots.begin(), m_slots.end(), slot,
                                        [](const SlotProgram &entry, quint64 value) { return entry.slot < value; });
    return found != m_slots.end() && found->slot == slot && found->lane == lane ? found->program : -1;
}

template <typename F>
void AudioRenderGraph::forEachPlacement(const Node &node, const MidiLaunchState *launch, F &&fn) const noexcept
{
    for (std::size_t p = 0; p < node.placementCount; ++p) fn(m_placements[node.placementFirst + p]);
    if (!launch) return;
    for (int l = 0; l < launch->count; ++l) {
        const MidiLaunchLane &lane = launch->lanes[l];
        if (lane.lane != node.midiLane) continue;
        if (lane.slot)
            if (const int program = launchProgram(lane.slot, lane.lane); program >= 0)
                fn(MidiPlacement{program, lane.origin, lane.start, std::numeric_limits<qint64>::max()});
        if (lane.previousSlot)
            if (const int program = launchProgram(lane.previousSlot, lane.lane); program >= 0)
                fn(MidiPlacement{program, lane.previousOrigin, lane.previousStart, lane.previousStop});
    }
}

void AudioRenderGraph::addMidi(const Node &node, qint64 start, int frames, double *const in[2],
                               const MidiLaunchState *launch) const noexcept
{
    forEachPlacement(node, launch, [&](const MidiPlacement &placement) {
        playPlacement(placement, start, frames, in, node.lanes);
    });
}

// The test instrument (midi_program.h): each note is a cosine at its pitch
// from its exact start sample, released at its end (or at the placement's
// cut). Overlapping notes, including the same pitch, sum.
void AudioRenderGraph::playPlacement(const MidiPlacement &placement, qint64 start, int frames, double *const in[2],
                                     int lanes) const noexcept
{
    const qint64 stop = start + frames;
    if (placement.program < 0 || placement.from >= stop || placement.to <= start - m_release) return;
    const MidiProgram &program = m_programs[std::size_t(placement.program)];
    const qint64 from = m_clock.tickAt(std::max(placement.from, start - m_release), false) - program.reach;
    const qint64 to = m_clock.tickAt(std::min(placement.to, stop), true) + 1;
    program.forEachNote(placement.origin, from, to, [&](qint64 at, qint64 duration, int pitch, int, quint16 velocity) {
        const qint64 begin = m_clock.sampleAt(at);
        if (begin < placement.from || begin >= placement.to || begin >= stop) return;
        const qint64 end = std::min(m_clock.sampleAt(at + duration), placement.to);
        if (end + m_release <= start) return;
        const double step = m_midiPhase[pitch];
        const double amplitude = MidiTestInstrument::kLevel * double(velocity) / 65535.0;
        const qint64 last = std::min(stop, end + m_release);
        for (qint64 sample = std::max(begin, start); sample < last; ++sample) {
            const qint64 n = sample - begin;
            const double value = amplitude * MidiTestInstrument::envelope(n, sample < end ? -1 : sample - end, m_attack, m_release)
                                 * std::cos(step * double(n));
            const int i = int(sample - start);
            in[0][i] += value;
            if (lanes > 1) in[1][i] += value;
        }
    });
}

int AudioRenderGraph::midiEvents(const QString &channelId, qint64 start, int frames, MidiScheduledEvent *out,
                                 int capacity, const MidiLaunchState *launch) const
{
    const Node *node = nullptr;
    for (const Node &candidate : m_nodes)
        if (candidate.midi && candidate.channelId == channelId) node = &candidate;
    if (!node || frames <= 0) return 0;
    const qint64 stop = start + frames;
    int count = 0;
    const auto push = [&](const MidiScheduledEvent &event) {
        if (out && count < capacity) out[count] = event;
        ++count;
    };
    forEachPlacement(*node, launch, [&](const MidiPlacement &placement) {
        if (placement.program < 0 || placement.from >= stop || placement.to < start) return;
        const MidiProgram &program = m_programs[std::size_t(placement.program)];
        const qint64 first = m_clock.tickAt(std::max(placement.from, start), false) - 1;
        const qint64 to = m_clock.tickAt(std::min(placement.to, stop), true) + 1;
        program.forEachNote(placement.origin, first - program.reach, to,
                            [&](qint64 at, qint64 duration, int pitch, int channel, quint16 velocity) {
            const qint64 begin = m_clock.sampleAt(at);
            if (begin < placement.from || begin >= placement.to) return;
            const qint64 end = std::min(m_clock.sampleAt(at + duration), placement.to);
            if (begin >= start && begin < stop)
                push({begin, MidiScheduledEvent::NoteOn, channel, pitch, 0, velocity});
            if (end >= start && end < stop) push({end, MidiScheduledEvent::NoteOff, channel, pitch, 0, 0});
        });
        program.forEachEvent(placement.origin, first, to, [&](qint64 at, const MidiProgram::Event &event) {
            const qint64 sample = m_clock.sampleAt(at);
            if (sample < placement.from || sample >= placement.to || sample < start || sample >= stop) return;
            const bool note = Midi::perNote(MidiController(event.kind));
            push({sample, MidiScheduledEvent::Controller, int(event.channel), note ? int(event.pitch) : int(event.number),
                  int(event.kind), event.value});
        });
    });
    // At one sample: note-offs, controllers, note-ons, then per-note
    // controllers (they need their note sounding).
    const auto rank = [](const MidiScheduledEvent &event) {
        return event.type == MidiScheduledEvent::Controller && Midi::perNote(MidiController(event.controller)) ? 3
                                                                                                                : event.type;
    };
    if (out)
        std::sort(out, out + std::min(count, capacity), [&](const MidiScheduledEvent &a, const MidiScheduledEvent &b) {
            return std::make_tuple(a.sample, rank(a), a.channel, a.number)
                   < std::make_tuple(b.sample, rank(b), b.channel, b.number);
        });
    return count;
}

} // namespace QindaTK
