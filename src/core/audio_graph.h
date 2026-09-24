// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_media.h"
#include "engine_schedule.h"

#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

namespace QindaTK {

class TimelineModel;

// AGENT-CONTRACT: an immutable render program compiled on the GUI thread from
// the committed timeline, decoded media and an EngineScheduleSnapshot. The
// realtime callback and the offline renderer execute the same render(), so a
// device and a file receive identical samples (docs/audio-engine.md).
class AudioRenderGraph {
public:
    static constexpr int kMaxBlock = 8192;
    static constexpr int kCubicSteps = 32; // linear pieces per cubic automation segment

    struct Sources {
        const TimelineModel *timeline = nullptr;
        // Decoded media at `sampleRate`, or null when the resource is unavailable.
        std::function<AudioBufferPtr(const QString &resourceId, int sampleRate)> media;
        EngineScheduleSnapshotPtr schedule;
        int sampleRate = 48000;
    };
    // Audio (or untyped) tracks with audio clips. Muted tracks, and unsoloed
    // tracks while any track is soloed, are left out.
    static std::shared_ptr<const AudioRenderGraph> compile(const Sources &sources);
    // Track gain automation binds by ID: "track:<track id>/gain"; a unit of
    // "dB" is converted to a linear factor, any other unit is a linear factor.
    static QString gainParameterId(const QString &trackId);

    quint64 generation() const { return m_generation; }
    int sampleRate() const { return m_sampleRate; }
    qint64 length() const { return m_length; }
    int clipCount() const;
    QStringList missingResources() const { return m_missing; }

    // AGENT-CONTRACT: realtime-safe. Adds the signal for [start, start + frames)
    // into `out` (interleaved, `channels` wide; frames <= kMaxBlock). `scratch`
    // holds kMaxBlock doubles owned by the caller. Never allocates, locks or
    // performs I/O; cubic automation was flattened at compile time.
    void render(qint64 start, int frames, float *out, int channels, double *scratch) const noexcept;

private:
    struct Clip {
        qint64 start = 0;       // graph samples, inclusive
        qint64 end = 0;         // exclusive
        qint64 sourceStart = 0; // media frame played at `start`
        AudioBufferPtr media;   // at the graph rate
    };
    struct Track {
        std::vector<Clip> clips; // sorted by start
        AutomationParameter gainParameter;
        QVector<AutomationPoint> gainPoints; // step/linear only
        bool automated = false;
        bool decibels = false;
        double constantGain = 1.0;
    };
    quint64 m_generation = 0;
    int m_sampleRate = 48000;
    TimelineRate m_timebase{TimelineTime::kDefaultTicksPerSecond, 1};
    qint64 m_length = 0;
    std::vector<Track> m_tracks;
    EngineScheduleSnapshotPtr m_schedule;
    QStringList m_missing;
};
using AudioRenderGraphPtr = std::shared_ptr<const AudioRenderGraph>;

} // namespace QindaTK
