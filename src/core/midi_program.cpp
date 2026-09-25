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
    // Notes in tick order (the pattern's own order is tick, pitch, channel, id).
    std::vector<int> order(std::size_t(pattern.notes.size()));
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return pattern.notes.at(a).tick < pattern.notes.at(b).tick; });
    QHash<QUuid, int> index;
    QHash<QUuid, int> pitches;
    program.notes.reserve(order.size());
    for (const int i : order) {
        const MidiNote &note = pattern.notes.at(i);
        const qint64 duration = std::min(note.duration, program.length); // never over its own repeat
        index.insert(note.id, int(program.notes.size()));
        pitches.insert(note.id, note.pitch);
        program.notes.push_back({note.tick, duration, note.velocity, quint8(note.pitch), quint8(note.channel), 0, 0});
        program.noteReach = std::max(program.noteReach, duration);
    }
    program.events.reserve(std::size_t(pattern.events.size()));
    std::vector<std::vector<Point>> bends(program.notes.size());
    for (const MidiControlEvent &event : pattern.events) {
        program.events.push_back({event.tick, event.value, quint8(event.kind), quint8(event.number), quint8(event.channel),
                                  quint8(pitches.value(event.noteId, 0))});
        // What the built-in test instrument hears (docs/audio-midi.md).
        const double unit = double(event.value) / 4294967295.0;
        const double signedUnit = (double(event.value) - 2147483648.0) / 2147483648.0;
        const int channel = std::clamp(event.channel, 0, 15);
        if (event.kind == MidiController::ControlChange && (event.number == 7 || event.number == 11))
            program.shapes[channel][event.number == 7 ? Volume : Expression].push_back({event.tick, unit});
        else if (event.kind == MidiController::PitchBend)
            program.shapes[channel][Bend].push_back({event.tick, signedUnit * kBendSemitones});
        else if (event.kind == MidiController::NoteBend)
            if (const auto found = index.constFind(event.noteId); found != index.cend())
                bends[std::size_t(*found)].push_back({event.tick, signedUnit * kNoteBendSemitones});
    }
    std::stable_sort(program.events.begin(), program.events.end(), [](const Event &a, const Event &b) { return a.tick < b.tick; });
    const auto byTick = [](const Point &a, const Point &b) { return a.tick < b.tick; };
    for (auto &channel : program.shapes)
        for (std::vector<Point> &points : channel) std::stable_sort(points.begin(), points.end(), byTick);
    for (std::size_t i = 0; i < bends.size(); ++i) {
        std::stable_sort(bends[i].begin(), bends[i].end(), byTick);
        program.notes[i].bendFirst = int(program.noteBends.size());
        program.notes[i].bendCount = int(bends[i].size());
        program.noteBends.insert(program.noteBends.end(), bends[i].begin(), bends[i].end());
    }
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
            program.rowReach = std::max(program.rowReach, gate);
        }
        program.rows.push_back(compiled);
    }
    return program;
}

} // namespace QindaTK
