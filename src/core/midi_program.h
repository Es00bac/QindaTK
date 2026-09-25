// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "midi_types.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace QindaTK {

class EngineScheduleSnapshot;

__extension__ typedef __int128 MidiWide; // exact tick × microsecond × rate products

// AGENT-CONTRACT (D-152): the musical clock of one compiled graph. Built on
// the GUI thread from the committed tempo map (EngineScheduleSnapshot) at the
// graph's sample rate; afterwards read-only, allocation-free and safe on the
// device thread. sampleAt() is TimeMap::fromTick's exact arithmetic
// (NearestAway), so the engine, the schedule snapshot and the timeline agree
// on every note position at the session rate.
class MidiClock {
public:
    static MidiClock build(const EngineScheduleSnapshot *schedule, int sampleRate);
    int sampleRate() const noexcept { return m_rate; }
    int ticksPerQuarter() const noexcept { return m_ppq; }
    qint64 sampleAt(qint64 tick) const noexcept;
    // The last tick at or before the sample (ceil false) or the first at or after it.
    qint64 tickAt(qint64 sample, bool ceil) const noexcept;
    qint64 nearestTick(qint64 sample) const noexcept;
    // The first grid line at or after `tick` (clamped to 0): the tick itself,
    // the next beat or the next bar line of the meter in force.
    qint64 nextGrid(qint64 tick, MidiQuantize grid) const noexcept;

private:
    struct Segment { qint64 tick = 0; qint64 microseconds = 500000; MidiWide units = 0; };
    struct Meter { qint64 tick = 0; qint64 bar = 3840; qint64 beat = 960; };
    std::vector<Segment> m_segments{Segment{}};
    std::vector<Meter> m_meters{Meter{}};
    qint64 m_origin = 0;
    int m_rate = 48000;
    int m_ppq = 960;
};

// One pattern compiled for the device thread: plain arrays, no Qt containers.
struct MidiProgram {
    struct Note {
        qint64 tick = 0;
        qint64 duration = 1; // as played: at most the pattern length (D-152)
        quint16 velocity = 1;
        quint8 pitch = 0;
        quint8 channel = 0;
        int bendFirst = 0;   // this note's per-note pitch bends in `noteBends`
        int bendCount = 0;
    };
    struct Event { qint64 tick = 0; quint32 value = 0; quint8 kind = 0; quint8 number = 0; quint8 channel = 0; quint8 pitch = 0; };
    struct Row { quint64 key = 0; qint64 stepTicks = 1; int first = 0; int count = 1; quint8 pitch = 0; quint8 channel = 0; };
    struct Step { double probability = 1.0; qint64 gate = 1; quint16 velocity = 1; bool enabled = false; };
    // A controller value the test instrument hears from `tick` on (gain 0..1, or semitones).
    struct Point { qint64 tick = 0; double value = 0.0; };
    enum Shape : int { Volume, Expression, Bend, kShapes }; // CC7, CC11, channel pitch bend
    static constexpr double kBendSemitones = 2.0;      // channel pitch bend range (General MIDI)
    static constexpr double kNoteBendSemitones = 48.0; // per-note pitch bend range (MIDI 2.0 default)

    // The occurrences of `points` from musical tick `origin`, looping on
    // `length` (0: once, from `origin`), in tick order. Realtime-safe.
    struct Cursor {
        const Point *points = nullptr;
        int count = 0;
        qint64 origin = 0;
        qint64 length = 0;
        qint64 pass = 0;
        int index = 0;
        static constexpr qint64 kNever = std::numeric_limits<qint64>::max();
        qint64 next() const noexcept
        {
            return index < count ? origin + pass * length + points[index].tick : kNever;
        }
        void advance() noexcept
        {
            if (++index == count && length > 0) {
                index = 0;
                ++pass;
            }
        }
        // Places the cursor on the first occurrence after `tick` and returns
        // the value in force at `tick` (`fallback` before the first one).
        double seek(qint64 tick, double fallback) noexcept
        {
            if (count == 0 || tick < origin) {
                pass = 0;
                index = 0;
                return fallback;
            }
            const qint64 relative = tick - origin;
            pass = length > 0 ? relative / length : 0;
            const qint64 within = relative - pass * length;
            const Point *after = std::upper_bound(points, points + count, within,
                                                  [](qint64 value, const Point &point) { return value < point.tick; });
            index = int(after - points);
            double value = fallback;
            if (index > 0) value = points[index - 1].value;
            else if (pass > 0) value = points[count - 1].value; // the previous pass's last value
            if (index == count && length > 0) {
                index = 0;
                ++pass;
            }
            return value;
        }
    };

    quint64 key = 0;
    qint64 length = 1;
    quint32 seed = 0;
    // AGENT-GUARD (D-152): how far back a sounding note or step can have
    // started. A played note lasts at most `length` (it would otherwise
    // overlap its own repeat), so a render window looks back at most one
    // pass and device-thread work per block never grows with playback time.
    qint64 noteReach = 1;
    qint64 rowReach = 1;
    std::vector<Note> notes;   // sorted by tick
    std::vector<Event> events; // sorted by tick
    std::vector<Row> rows;
    std::vector<Step> steps;
    std::vector<Point> shapes[16][kShapes]; // per channel, sorted by tick
    std::vector<Point> noteBends;           // per note (Note::bendFirst), sorted by tick

    static MidiProgram compile(const MidiPattern &pattern);

    bool shaped(int channel) const noexcept
    {
        return !shapes[channel][Volume].empty() || !shapes[channel][Expression].empty() || !shapes[channel][Bend].empty();
    }
    Cursor shape(int channel, Shape kind, qint64 origin) const noexcept
    {
        const std::vector<Point> &points = shapes[channel][kind];
        return {points.data(), int(points.size()), origin, length};
    }
    // The per-note bends of note `note` played in the pass that starts at `base`.
    Cursor bends(int note, qint64 base) const noexcept
    {
        if (note < 0) return {};
        const Note &n = notes[std::size_t(note)];
        return {noteBends.data() + n.bendFirst, n.bendCount, base, 0};
    }

    // AGENT-CONTRACT (D-151): whether step `step` of a row plays on pass
    // `iteration` (passes counted from the placement origin). A pure function
    // of the pattern seed, the row, the step and the pass: every render of a
    // placement — device, file, after a seek or a loop — plays the same steps.
    static bool plays(quint32 seed, quint64 row, qint64 step, qint64 iteration, double probability) noexcept
    {
        if (probability >= 1.0) return true;
        if (probability <= 0.0) return false;
        quint64 z = (quint64(seed) << 32) ^ row ^ (quint64(step) * 0x9E3779B97F4A7C15ull)
                    ^ (quint64(iteration) * 0xC2B2AE3D27D4EB4Full);
        z += 0x9E3779B97F4A7C15ull; // SplitMix64 finalizer
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return double(z >> 11) * 0x1.0p-53 < probability;
    }

    static qint64 floorDiv(qint64 a, qint64 b) noexcept { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

    // Every note occurrence (pattern notes looping on `length`, step rows on
    // their own lengths) that can still sound at or after tick `from` and
    // starts before `to`, for a placement that starts at musical tick
    // `origin`: fn(tick, duration, pitch, channel, velocity, note), where
    // `note` indexes `notes` (-1 for a step). Callers drop the ones that end
    // before their window.
    template <typename F>
    void forEachNote(qint64 origin, qint64 from, qint64 to, F &&fn) const noexcept
    {
        const qint64 noteFrom = std::max(from - noteReach, origin);
        if (!notes.empty() && to > noteFrom)
            for (qint64 k = (noteFrom - origin) / length; origin + k * length < to; ++k) {
                const qint64 base = origin + k * length;
                const auto first = std::lower_bound(notes.begin(), notes.end(), noteFrom - base,
                                                    [](const Note &note, qint64 value) { return note.tick < value; });
                for (auto note = first; note != notes.end(); ++note) {
                    const qint64 at = base + note->tick;
                    if (at >= to) break;
                    fn(at, note->duration, int(note->pitch), int(note->channel), note->velocity, int(note - notes.begin()));
                }
            }
        const qint64 rowFrom = std::max(from - rowReach, origin);
        if (to <= rowFrom) return;
        for (const Row &row : rows) {
            qint64 j = -floorDiv(origin - rowFrom, row.stepTicks); // ceil((rowFrom - origin) / stepTicks)
            for (qint64 at = origin + j * row.stepTicks; at < to; ++j, at += row.stepTicks) {
                const qint64 index = j % row.count;
                const Step &step = steps[std::size_t(row.first + index)];
                if (step.enabled && plays(seed, row.key, index, j / row.count, step.probability))
                    fn(at, step.gate, int(row.pitch), int(row.channel), step.velocity, -1);
            }
        }
    }
    // Controller events in [from, to), looping on `length`: fn(tick, event).
    template <typename F>
    void forEachEvent(qint64 origin, qint64 from, qint64 to, F &&fn) const noexcept
    {
        from = std::max(from, origin);
        if (to <= from || events.empty()) return;
        for (qint64 k = (from - origin) / length; origin + k * length < to; ++k) {
            const qint64 base = origin + k * length;
            const auto first = std::lower_bound(events.begin(), events.end(), from - base,
                                                [](const Event &event, qint64 value) { return event.tick < value; });
            for (auto event = first; event != events.end(); ++event) {
                const qint64 at = base + event->tick;
                if (at >= to) break;
                fn(at, *event);
            }
        }
    }
};
// Where a program plays: from musical tick `origin`, heard for notes that
// start in the sample window [from, to) and cut at `to`.
struct MidiPlacement {
    int program = -1;
    qint64 origin = 0;
    qint64 from = 0;
    qint64 to = std::numeric_limits<qint64>::max();
};

// A MIDI event at an exact sample, as the graph schedules it for an
// instrument (the built-in test instrument, WP-066 plugin instruments).
struct MidiScheduledEvent {
    enum Type : int { NoteOff = 0, Controller = 1, NoteOn = 2 }; // order at one sample (per-note controllers last)
    qint64 sample = 0;
    int type = NoteOn;
    int channel = 0;
    int number = 0;      // note pitch (also for per-note controllers), or CC number
    int controller = 0;  // MidiController when type is Controller
    quint32 value = 0;   // 16-bit velocity (NoteOn) or 32-bit controller value
};

// AGENT-CONTRACT (D-153): the clip launcher's state on the device thread,
// owned by AudioEngineProcessor and read by AudioRenderGraph::render. Keys are
// Midi::key() of the track (lane) and slot IDs, so a recompiled graph keeps
// playing what was launched. `previous*` keeps the release tail of a clip
// that a launch or stop just replaced.
struct MidiLaunchLane {
    quint64 lane = 0;
    quint64 slot = 0;             // playing; 0 = none
    quint64 playSerial = 0;
    qint64 origin = 0;
    qint64 start = 0;
    quint64 queuedSlot = 0;       // waiting for its boundary; 0 = none
    quint64 queuedSerial = 0;
    qint64 queuedOrigin = 0;
    qint64 queuedStart = 0;
    int queuedQuantize = 0;       // MidiQuantize: re-applied after a transport jump
    qint64 stopAt = -1;           // a quantized stop; -1 = none
    quint64 stopSerial = 0;
    int stopQuantize = 0;
    quint64 previousSlot = 0;
    qint64 previousOrigin = 0;
    qint64 previousStart = 0;
    qint64 previousStop = 0;
};
struct MidiLaunchState {
    static constexpr int kLanes = 32;
    MidiLaunchLane lanes[kLanes];
    int count = 0;
};

// The built-in test instrument (WP-064): one cosine voice per note with a
// 1 ms linear attack and a 5 ms linear release after the note end. It
// follows its placement's controllers: CC7 volume × CC11 expression scale the
// level, channel pitch bend (±2 semitones) and per-note pitch bend (±48)
// bend the pitch with continuous phase. It is stateless — a sample depends
// only on the note, its placement's controller events and the sample
// position — so seeks, loops, recompiles and offline renders sound
// identical, and a note can never outlive its end by more than the release.
namespace MidiTestInstrument {
constexpr double kLevel = 0.25;
inline int attackFrames(int rate) { return std::max(1, rate / 1000); }
inline int releaseFrames(int rate) { return std::max(1, rate / 200); }
inline double phaseStep(int pitch, int rate)
{
    return 2.0 * std::numbers::pi * 440.0 * std::pow(2.0, double(pitch - 69) / 12.0) / double(rate);
}
// `n` samples after the note start; `released` samples after its end (-1 while held).
inline double envelope(qint64 n, qint64 released, int attack, int release) noexcept
{
    const double a = std::min(1.0, double(n + 1) / double(attack));
    const double r = released < 0 ? 1.0 : 1.0 - double(released + 1) / double(release);
    return std::max(0.0, std::min(a, r));
}
} // namespace MidiTestInstrument

} // namespace QindaTK
