// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_media.h"
#include "audio_mixer_types.h"
#include "audio_take_comp.h"
#include "engine_schedule.h"
#include "midi_program.h"

#include <QStringList>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace QindaTK {

class AudioChannelMeterSink;
class TimelineModel;
struct TimelineClip;

// AGENT-CONTRACT: an immutable render program compiled on the GUI thread from
// the committed timeline, decoded media, an EngineScheduleSnapshot and the
// mixer content. The realtime callback and the offline renderer execute the
// same render(), so a device and a file receive identical samples
// (docs/audio-engine.md, docs/audio-mixer.md).
class AudioRenderGraph {
public:
    static constexpr int kMaxBlock = 8192;
    static constexpr int kCubicSteps = 32; // linear pieces per cubic automation segment
    static constexpr int kChunk = 256;     // frames mixed per pass; work memory holds kChunk per lane

    struct Sources {
        const TimelineModel *timeline = nullptr;
        // Decoded media at `sampleRate`, or null when the resource is unavailable.
        std::function<AudioBufferPtr(const QString &resourceId, int sampleRate)> media;
        EngineScheduleSnapshotPtr schedule;
        int sampleRate = 48000;
        // Routing (WP-062). Null: every audio track feeds the master, the
        // master feeds "main". The master is always compiled.
        const AudioMixerContent *mixer = nullptr;
        // Device ports "device:<n>[-<m>]@<deviceId>" play only on this device
        // with at least that many output channels; any other port is silent.
        QString deviceId;
        int deviceChannels = 0;
        // A file render: the master feeds "main" (the file) whatever its
        // device routing, and device ports are silent.
        bool offline = false;
        // MIDI patterns and launcher slots (WP-064). MIDI tracks play the
        // patterns their clips name through the built-in test instrument.
        const MidiContent *midi = nullptr;
        // Take comps (WP-065): an audio clip whose resourceId starts with
        // "comp:" plays what this returns for it (invalid: missing).
        std::function<AudioCompPlayback(const QString &compId)> comp;
    };
    static bool isCompResource(const QString &resourceId) { return resourceId.startsWith(QLatin1String("comp:")); }
    // Audio, MIDI (or untyped) tracks become channels in timeline order, then the
    // buses in dependency order, then the master. Muted channels, and channels
    // a solo leaves out (D-112), feed nothing.
    static std::shared_ptr<const AudioRenderGraph> compile(const Sources &sources);
    // Track gain automation binds by ID: "track:<track id>/gain"; a unit of
    // "dB" is converted to a linear factor, any other unit is a linear factor.
    // The engine plays the live value while a control holds the parameter or
    // a pass writes it, the static value (the parameter's defaultValue) when
    // the lane has no points or its mode is off, and the curve otherwise.
    // Pan, width and send levels bind the same way (AudioMixer::parameterId).
    static QString gainParameterId(const QString &trackId);
    // AGENT-CONTRACT (D-101): a dB gain's range minimum is silence (-inf dB),
    // so a fader at the bottom of its travel is exact silence and no finite
    // attenuation stands in for it. AudioParameterBinding reports the same.
    static bool hasSilenceFloor(const AutomationParameter &parameter);

    quint64 generation() const { return m_generation; }
    int sampleRate() const { return m_sampleRate; }
    qint64 length() const { return m_length; }
    int clipCount() const;
    QStringList missingResources() const { return m_missing; }
    // Metered channels in AudioChannelMeterSink slot order, and their lanes.
    const QStringList &meterChannels() const { return m_meterChannels; }
    const QVector<int> &meterLanes() const { return m_meterLanes; }
    int meterSlots() const { return int(m_meterChannels.size()); }
    // Routes that play nothing because their port is not available.
    QStringList problems() const { return m_problems; }

    // AGENT-CONTRACT: realtime-safe. Adds the signal for [start, start + frames)
    // into `out` (interleaved, `channels` wide; frames <= kMaxBlock). `scratch`
    // holds kMaxBlock doubles owned by the caller; the channel lanes live in
    // this graph's own work memory, sized at compile time.
    // AGENT-GUARD: one thread renders a given graph at a time (the device
    // thread, or the caller of an offline render that compiled its own).
    // Never allocates, locks or performs I/O; cubic automation was flattened
    // at compile time. `meters` (may be null) receives post-fader channel levels.
    // `launch` (may be null) adds the clips the launcher plays (D-153).
    void render(qint64 start, int frames, float *out, int channels, double *scratch,
                AudioChannelMeterSink *meters = nullptr, const MidiLaunchState *launch = nullptr) const noexcept;

    // ---- MIDI (WP-064, docs/audio-midi.md) ----
    const MidiClock &midiClock() const { return m_clock; }
    // Realtime-safe: the program a launcher slot plays when it belongs to
    // that MIDI track lane in this graph, else -1.
    int launchProgram(quint64 slot, quint64 lane) const noexcept;
    // The events scheduled for a MIDI channel in [start, start + frames),
    // sorted by sample (note-offs, controllers, note-ons, then per-note
    // controllers at one sample). Writes at most `capacity`; returns the total found. GUI and
    // tests; it evaluates exactly what render() plays.
    int midiEvents(const QString &channelId, qint64 start, int frames, MidiScheduledEvent *out, int capacity,
                   const MidiLaunchState *launch = nullptr) const;

private:
    struct Clip {
        qint64 start = 0;       // graph samples, inclusive
        qint64 end = 0;         // exclusive
        qint64 sourceStart = 0; // media frame played at `start`
        AudioBufferPtr media;   // at the graph rate
        // Comp pieces (WP-065, D-175): media frames outside [mediaFirst,
        // mediaEnd) are silent, and samples in a fade region are weighted
        // (AudioComp::fadeWeight). Plain clips leave both regions empty.
        qint64 mediaFirst = 0;
        qint64 mediaEnd = std::numeric_limits<qint64>::max();
        qint64 fadeInStart = 0;
        qint64 fadeInEnd = 0;
        qint64 fadeOutStart = 0;
        qint64 fadeOutEnd = 0;
        AudioCompFade fadeShape = AudioCompFade::EqualPower;
        bool faded() const noexcept { return fadeInEnd > fadeInStart || fadeOutEnd > fadeOutStart; }
    };
    // One parameter, constant or evaluated per sample.
    struct Curve {
        AutomationParameter parameter;
        QVector<AutomationPoint> points; // step/linear only
        bool automated = false;
        bool decibels = false;
        double silenceFloor = -std::numeric_limits<double>::infinity(); // dB at or below: gain 0
        double constant = 1.0;           // the converted value when not automated
        double convert(double value) const noexcept;
    };
    struct Route {
        int node = -1;          // destination channel, or -1 for a port
        int port = -1;          // m_ports index when node < 0
        int level = -1;         // send level curve; -1 is unity
        bool preFader = false;
    };
    struct Node {
        QString channelId;
        int lanes = 2;
        std::vector<Clip> clips; // tracks only, sorted by start
        int gain = -1;           // curves; -1 is the default (unity, centre, width 1)
        int pan = -1;
        int width = -1;
        bool audible = true;
        std::vector<Route> routes;
        int meterSlot = -1;
        std::size_t buffer = 0;  // first lane in m_work (in kChunk units)
        bool midi = false;       // a MIDI track: its lanes come from the test instrument
        quint64 midiLane = 0;    // Midi::key(track id)
        std::size_t placementFirst = 0;
        std::size_t placementCount = 0;
    };
    struct PortTarget {
        QString id;
        bool main = false;       // "main": two lanes on the first output channels (a mono output averages)
        int channel = 0;         // first device channel (0-based)
        int lanes = 2;
        std::size_t buffer = 0;
    };

    int addCurve(const EngineScheduleSnapshot *schedule, const QString &parameterId);
    int portIndex(const QString &id, const Sources &sources, const QString &from);
    void evaluate(int curve, double fallback, qint64 start, int frames, double *out) const noexcept;
    bool constant(int curve) const noexcept { return curve < 0 || !m_curves[std::size_t(curve)].automated; }
    double *lane(std::size_t buffer, int index) const noexcept;
    void addClips(const Node &node, qint64 start, int frames, double *const in[2]) const noexcept;
    // Expands a comp clip into its pieces (audio_graph_comp.cpp).
    void addComp(Node &node, const TimelineClip &clip, const Sources &sources);
    void compileMidi(const Sources &sources, qsizetype trackCount);
    template <typename F> void forEachPlacement(const Node &node, const MidiLaunchState *launch, F &&fn) const noexcept;
    void addMidi(const Node &node, qint64 start, int frames, double *const in[2], const MidiLaunchState *launch) const noexcept;
    void playPlacement(const MidiPlacement &placement, qint64 start, int frames, double *const in[2], int lanes) const noexcept;
    void mix(const Node &node, const Route &route, double *const in[2], int frames, const double *gain,
             const double *pan, const double *width, const double *level) const noexcept;
    void mixChunk(qint64 start, int frames, float *out, int channels, double *scratch,
                  AudioChannelMeterSink *meters, const MidiLaunchState *launch) const noexcept;
    void writePorts(int frames, float *out, int channels) const noexcept;

    quint64 m_generation = 0;
    int m_sampleRate = 48000;
    TimelineRate m_timebase{TimelineTime::kDefaultTicksPerSecond, 1};
    qint64 m_length = 0;
    AudioPanLaw m_panLaw = AudioPanLaw::ConstantPower;
    std::vector<Curve> m_curves;
    std::vector<Node> m_nodes;   // processing order: tracks, buses (sources first), master
    std::vector<PortTarget> m_ports;
    std::size_t m_lanes = 0;     // lanes in m_work
    mutable std::vector<double> m_work;
    EngineScheduleSnapshotPtr m_schedule;
    QStringList m_missing;
    QStringList m_meterChannels;
    QVector<int> m_meterLanes;
    QStringList m_problems;
    // MIDI: the clock, one program per pattern, arrangement placements, and
    // launcher slots sorted by slot key.
    struct SlotProgram { quint64 slot = 0; quint64 lane = 0; int program = -1; };
    MidiClock m_clock;
    std::vector<MidiProgram> m_programs;
    std::vector<MidiPlacement> m_placements;
    std::vector<SlotProgram> m_slots;
    double m_midiPhase[128] = {};
    int m_attack = 1;
    int m_release = 1;
};
using AudioRenderGraphPtr = std::shared_ptr<const AudioRenderGraph>;

} // namespace QindaTK
