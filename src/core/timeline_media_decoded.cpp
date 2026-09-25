// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_decoder.h"
#include "time_map.h"
#include "timeline_media_provider.h"
#include "timeline_time.h"

#include <QThreadPool>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qint64 kChunkFrames = 1 << 18; // sample frames per decoder read

// AGENT-NOTE: EditTimeline audio clips draw these peaks from the same files
// the program monitor plays (docs/edit-timeline.md "Media"). Sample spans come
// from the exact source ticks at the stream's own rate, so a pop in the file
// lands in the column that holds its tick; nothing is resampled or invented.
class DecodedBackend final : public TimelineMediaBackend {
public:
    QString backendId() const override { return QStringLiteral("qindatk.decoded-audio"); }
    void start(const TimelineMediaRequest &request, CancellationToken cancel,
               Completion completion) override
    {
        if (request.kind != QLatin1String("peaks")) {
            TimelineMediaResult result;
            result.error = {ErrorCode::Unsupported,
                            QStringLiteral("The decoded backend draws audio peaks, not video frames"),
                            {}, backendId()};
            completion(result);
            return;
        }
        QThreadPool::globalInstance()->start([request, cancel, completion, id = backendId()] {
            completion(peaks(request, cancel, id));
        });
    }

private:
    static TimelineMediaResult peaks(const TimelineMediaRequest &request, const CancellationToken &cancel,
                                     const QString &id)
    {
        TimelineMediaResult result;
        const auto fail = [&](ErrorCode code, const QString &message) {
            result.peaks.clear();
            result.error = {code, message, {}, id};
            return result;
        };
        if (!MediaDecoder::available())
            return fail(ErrorCode::Unsupported, QStringLiteral("Built without the FFmpeg media backend"));
        MediaDecoder decoder;
        if (OperationError error = decoder.open(request.location, cancel)) {
            result.error = error;
            return result;
        }
        const MediaSourceInfoPtr info = decoder.info();
        if (!info || !info->hasAudio()) return fail(ErrorCode::Unsupported, QStringLiteral("The media has no sound"));
        const TimelineRate sampleRate{info->audioSampleRate, 1};
        const auto first = TimelineTime::rescale(request.sourceStart, request.ticksPerSecond, sampleRate,
                                                 TimeMap::Floor);
        const auto end = TimelineTime::rescale(request.sourceEnd, request.ticksPerSecond, sampleRate,
                                               TimeMap::Ceil);
        if (!first || !end || *end <= *first) return fail(ErrorCode::Validation, QStringLiteral("Invalid media source range"));
        const int channels = std::clamp(info->audioChannels, 1, 8);
        const qint64 total = *end - *first;
        const int columns = request.columns;
        result.peaks.fill(0.0f, columns);
        // Column c holds samples [first + total·c/columns, first + total·(c+1)/columns).
        for (qint64 at = 0; at < total;) {
            const qint64 count = std::min(kChunkFrames, total - at);
            const MediaAudioBlock block = decoder.readAudio(*first + at, count, info->audioSampleRate, channels,
                                                            cancel);
            if (block.error) {
                result.peaks.clear();
                result.error = block.error;
                return result;
            }
            const qint64 frames = std::min<qint64>(count, block.samples.size() / channels);
            for (qint64 i = 0; i < frames; ++i) {
                const qint64 position = at + i;
                const int column = int(std::min<qint64>(columns - 1, position * columns / total));
                float peak = result.peaks[column];
                for (int channel = 0; channel < channels; ++channel)
                    peak = std::max(peak, std::fabs(block.samples[i * channels + channel]));
                result.peaks[column] = std::min(peak, 1.0f);
            }
            at += count;
            if (cancel.isCancelled()) return fail(ErrorCode::Cancelled, QStringLiteral("Cancelled"));
        }
        return result;
    }
};
} // namespace

TimelineMediaBackendPtr createDecodedTimelineMediaBackend()
{
    return std::make_shared<DecodedBackend>();
}

} // namespace QindaTK
