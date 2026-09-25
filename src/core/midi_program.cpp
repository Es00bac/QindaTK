// SPDX-License-Identifier: LGPL-3.0-or-later
#include "midi_program.h"

#include "engine_schedule.h"
#include "timeline_time.h"

#include <QHash>

namespace QindaTK {
namespace {

MidiWide magnitude(MidiWide value)
{
    return value < 0 ? -value : value;
}

} // namespace

MidiClock MidiClock::build(const EngineScheduleSnapshot *schedule, int sampleRate)
{
    MidiClock clock;
    clock.m_rate = sampleRate > 0 ? sampleRate : 48000;
    if (!schedule) return clock;
    clock.m_ppq = std::max(1, schedule->ticksPerQuarter());
    if (const auto origin = schedule->sampleAtMusicalTick(0)) {
        clock.m_origin = schedule->sampleRate() == clock.m_rate
            ? *origin
            : TimelineTime::rescale(*origin, {schedule->sampleRate(), 1}, {clock.m_rate, 1}, TimeMap::NearestAway)
                  .value_or(0);
    }
    QVector<TempoEvent> events = schedule->tempoEvents();
    std::stable_sort(events.begin(), events.end(), [](const TempoEvent &a, const TempoEvent &b) { return a.tick < b.tick; });
    std::vector<Segment> segments;
    std::vector<Meter> meters;
    for (const TempoEvent &event : events) {
        if (event.kind == TempoEvent::Kind::Tempo && event.microsecondsPerQuarter > 0) {
            Segment segment{event.tick, event.microsecondsPerQuarter, 0};
            if (!segments.empty()) {
                const Segment &previous = segments.back();
                segment.units = previous.units + MidiWide(event.tick - previous.tick) * previous.microseconds;
            }
            segments.push_back(segment);
        } else if (event.kind == TempoEvent::Kind::Meter && event.denominator > 0 && event.numerator > 0) {
            const qint64 beat = std::max<qint64>(1, qint64(4) * clock.m_ppq / event.denominator);
            meters.push_back({event.tick, beat * event.numerator, beat});
        }
    }
    if (!segments.empty()) clock.m_segments = std::move(segments);
    if (!meters.empty()) clock.m_meters = std::move(meters);
    else clock.m_meters = {Meter{0, qint64(4) * clock.m_ppq, clock.m_ppq}};
    return clock;
}

qint64 MidiClock::sampleAt(qint64 tick) const noexcept
{
    const auto next = std::upper_bound(m_segments.begin(), m_segments.end(), tick,
                                       [](qint64 value, const Segment &segment) { return value < segment.tick; });
    const Segment &segment = next == m_segments.begin() ? *next : *(next - 1);
    const MidiWide units = segment.units + MidiWide(tick - segment.tick) * segment.microseconds;
    const MidiWide n = units * m_rate;
    const MidiWide d = MidiWide(m_ppq) * 1000000;
    MidiWide q = n / d;
    if (2 * magnitude(n % d) >= d) q += n < 0 ? -1 : 1; // NearestAway, as TimeMap::fromTick
    return m_origin + qint64(q);
}

qint64 MidiClock::tickAt(qint64 sample, bool ceil) const noexcept
{
    const MidiWide x = MidiWide(sample - m_origin) * m_ppq * 1000000;
    const MidiWide rate = m_rate;
    const auto next = std::upper_bound(m_segments.begin(), m_segments.end(), x,
                                       [rate](MidiWide value, const Segment &segment) { return value < segment.units * rate; });
    const Segment &segment = next == m_segments.begin() ? *next : *(next - 1);
    const MidiWide n = x - segment.units * rate;
    const MidiWide d = MidiWide(segment.microseconds) * rate;
    MidiWide q = n / d;
    if (n % d != 0) {
        if (!ceil && n < 0) q -= 1;
        if (ceil && n > 0) q += 1;
    }
    return segment.tick + qint64(q);
}

qint64 MidiClock::nearestTick(qint64 sample) const noexcept
{
    const qint64 below = tickAt(sample, false);
    return sampleAt(below + 1) - sample < sample - sampleAt(below) ? below + 1 : below;
}

qint64 MidiClock::nextGrid(qint64 tick, MidiQuantize grid) const noexcept
{
    tick = std::max<qint64>(0, tick);
    if (grid == MidiQuantize::None) return tick;
    const auto next = std::upper_bound(m_meters.begin(), m_meters.end(), tick,
                                       [](qint64 value, const Meter &meter) { return value < meter.tick; });
    const Meter &meter = next == m_meters.begin() ? *next : *(next - 1);
    const qint64 unit = grid == MidiQuantize::Bar ? meter.bar : meter.beat;
    const qint64 offset = tick - meter.tick;
    qint64 line = meter.tick + (offset + unit - 1) / unit * unit;
    if (next != m_meters.end() && next != m_meters.begin() && line > next->tick) line = next->tick; // a meter change is a bar line
    return line;
}

MidiProgram MidiProgram::compile(const MidiPattern &pattern)
{
    MidiProgram program;
    program.key = Midi::key(pattern.id);
    program.length = std::max<qint64>(1, pattern.length);
    program.seed = pattern.seed;
    QHash<QUuid, int> pitches;
    program.notes.reserve(std::size_t(pattern.notes.size()));
    for (const MidiNote &note : pattern.notes) {
        program.notes.push_back({note.tick, note.duration, note.velocity, quint8(note.pitch), quint8(note.channel)});
        program.reach = std::max(program.reach, note.duration);
        pitches.insert(note.id, note.pitch);
    }
    std::stable_sort(program.notes.begin(), program.notes.end(), [](const Note &a, const Note &b) { return a.tick < b.tick; });
    program.events.reserve(std::size_t(pattern.events.size()));
    for (const MidiControlEvent &event : pattern.events)
        program.events.push_back({event.tick, event.value, quint8(event.kind), quint8(event.number), quint8(event.channel),
                                  quint8(pitches.value(event.noteId, 0))});
    std::stable_sort(program.events.begin(), program.events.end(), [](const Event &a, const Event &b) { return a.tick < b.tick; });
    for (const MidiStepRow &row : pattern.rows) {
        if (row.steps.isEmpty() || row.stepTicks < 1) continue;
        Row compiled;
        compiled.key = Midi::key(row.id);
        compiled.stepTicks = row.stepTicks;
        compiled.first = int(program.steps.size());
        compiled.count = int(row.steps.size());
        compiled.pitch = quint8(row.pitch);
        compiled.channel = quint8(row.channel);
        for (const MidiStep &step : row.steps) {
            const qint64 gate = std::max<qint64>(1, std::llround(step.gate * double(row.stepTicks)));
            program.steps.push_back({step.probability, gate, step.velocity, step.enabled});
            program.reach = std::max(program.reach, gate);
        }
        program.rows.push_back(compiled);
    }
    return program;
}

} // namespace QindaTK
