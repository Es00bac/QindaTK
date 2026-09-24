// SPDX-License-Identifier: LGPL-3.0-or-later
// MediaDecoder's audio path: seek by the packet index, decode, convert to
// interleaved float, map channels and resample (docs/media-playback.md).
#include "media_decoder.h"
#include "media_ffmpeg_p.h"

#include <algorithm>

namespace QindaTK {

#if defined(QINDATK_HAVE_FFMPEG)
namespace {
using MediaFfmpeg::failure;

constexpr int kMaxPacketErrors = 64;

// Mono duplicates at unity gain; N->1 averages; other layouts keep the
// channel positions both sides have and leave extra outputs silent.
void mapChannels(const float *in, int inChannels, float *out, int outChannels)
{
    if (inChannels == outChannels) {
        std::copy(in, in + inChannels, out);
    } else if (inChannels == 1) {
        std::fill(out, out + outChannels, in[0]);
    } else if (outChannels == 1) {
        float sum = 0.0f;
        for (int c = 0; c < inChannels; ++c) sum += in[c];
        out[0] = sum / float(inChannels);
    } else {
        for (int c = 0; c < outChannels; ++c) out[c] = c < inChannels ? in[c] : 0.0f;
    }
}

struct AudioConverter {
    SwrContext *context = nullptr;
    int format = -1;
    int channels = 0;
    int rate = 0;
    ~AudioConverter() { swr_free(&context); }
};

// Converts one decoded frame to interleaved float at its own channel count.
bool toFloat(AudioConverter *converter, const AVFrame *frame, QVector<float> *scratch)
{
    const int channels = frame->ch_layout.nb_channels;
    if (!converter->context || converter->format != frame->format
        || converter->channels != channels || converter->rate != frame->sample_rate) {
        swr_free(&converter->context);
        if (swr_alloc_set_opts2(&converter->context, &frame->ch_layout, AV_SAMPLE_FMT_FLT,
                                frame->sample_rate, &frame->ch_layout,
                                static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
                                0, nullptr) < 0
            || swr_init(converter->context) < 0) {
            swr_free(&converter->context);
            return false;
        }
        converter->format = frame->format;
        converter->channels = channels;
        converter->rate = frame->sample_rate;
    }
    scratch->resize(qsizetype(frame->nb_samples) * channels);
    uint8_t *out[1] = {reinterpret_cast<uint8_t *>(scratch->data())};
    const int converted = swr_convert(converter->context, out, frame->nb_samples,
                                      frame->extended_data, frame->nb_samples);
    if (converted < 0) return false;
    scratch->resize(qsizetype(converted) * channels);
    return true;
}

// Stream sample of a decoded frame's first sample.
qint64 frameStart(const MediaSourceInfo &info, const AVFrame *frame, qint64 running)
{
    qint64 pts = frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) return running;
    if (info.audioPtsSampleExact) return pts - info.audioStartPts;
    const qint64 indexed = info.audioFirstSampleOfPts(pts);
    if (indexed >= 0) return indexed;
    if (running >= 0) return running;
    const auto relative = TimelineTime::subtract(pts, info.audioStartPts);
    const auto converted = relative ? TimelineTime::rescale(*relative, info.audioPtsPerSecond,
        TimelineRate{info.audioSampleRate, 1}, TimeMap::NearestAway) : std::nullopt;
    return converted.value_or(0);
}

// Positions the audio demuxer at or before stream sample `first`.
bool seekAudio(MediaDecoder::Private *d, qint64 first)
{
    const MediaSourceInfo &info = *d->info;
    if (info.hasVideo()) {
        // AGENT-NOTE: Matroska indexes only video key frames, so a file with
        // video is sought on its video stream (named explicitly: the audio
        // demuxer discards video, which would make FFmpeg's default stream
        // the audio one) one second before `first`. Audio from that key frame
        // on is delivered, so the packet holding `first` is not skipped.
        const qint64 preroll = std::max<qint64>(0, first + info.audioOffsetSamples - info.audioSampleRate);
        const auto offset = TimelineTime::rescale(preroll, TimelineRate{info.audioSampleRate, 1},
                                                  info.ptsPerSecond, TimeMap::Floor);
        const auto target = offset ? TimelineTime::add(info.frames.constFirst().pts, *offset) : std::nullopt;
        if (target && av_seek_frame(d->audioFormat, info.videoStream, *target, AVSEEK_FLAG_BACKWARD) >= 0)
            return true;
    }
    const qint64 packet = info.audioPacketAtOrBefore(first);
    const qint64 seekPts = packet >= 0 ? info.audioPackets.at(packet).pts : info.audioStartPts;
    if (av_seek_frame(d->audioFormat, info.audioStream, seekPts, AVSEEK_FLAG_BACKWARD) >= 0) return true;
    return avformat_seek_file(d->audioFormat, info.audioStream, INT64_MIN, seekPts, seekPts, 0) >= 0;
}

// Decodes stream samples [first, end) into `window`, which starts at stream
// sample `windowFirst` and holds `channels` interleaved floats per sample.
OperationError decodeAudioWindow(MediaDecoder::Private *d, qint64 first, qint64 end, qint64 windowFirst,
                                 int channels, QVector<float> *window, qint64 *decoded,
                                 const CancellationToken &cancel)
{
    const MediaSourceInfo &info = *d->info;
    if (!seekAudio(d, first)) {
        // Last resort: a freshly opened demuxer reads from the first packet.
        d->closeAudio();
        if (OperationError error = d->openAudio()) return error;
    }
    avcodec_flush_buffers(d->audio);
    AudioConverter converter;
    QVector<float> scratch;
    QVector<float> mapped(channels);
    qint64 running = -1;
    bool demuxEnded = false;
    int errors = 0;
    while (true) {
        const int received = avcodec_receive_frame(d->audio, d->frame);
        if (received == 0) {
            const qint64 start = frameStart(info, d->frame, running);
            const int sourceChannels = d->frame->ch_layout.nb_channels;
            const int samples = d->frame->nb_samples;
            if (start >= 0 && sourceChannels > 0 && toFloat(&converter, d->frame, &scratch)) {
                const qint64 from = std::max(first, start);
                const qint64 to = std::min(end, start + qint64(scratch.size() / sourceChannels));
                for (qint64 s = from; s < to; ++s) {
                    mapChannels(scratch.constData() + (s - start) * sourceChannels, sourceChannels,
                                mapped.data(), channels);
                    std::copy(mapped.cbegin(), mapped.cend(), window->begin() + (s - windowFirst) * channels);
                }
                if (to > from) *decoded += to - from;
            }
            running = (start >= 0 ? start : 0) + samples;
            av_frame_unref(d->frame);
            if (running >= end) break;
            continue;
        }
        if (received == AVERROR_EOF) break;
        if (received != AVERROR(EAGAIN))
            return failure(ErrorCode::Validation, QStringLiteral("Audio could not be decoded"));
        if (cancel.isCancelled()) return MediaFfmpeg::cancelled();
        if (demuxEnded) break;
        const int read = av_read_frame(d->audioFormat, d->packet);
        if (read < 0) {
            if (cancel.isCancelled()) return MediaFfmpeg::cancelled();
            demuxEnded = true;
            avcodec_send_packet(d->audio, nullptr);
            continue;
        }
        if (d->packet->stream_index != info.audioStream) {
            av_packet_unref(d->packet);
            continue;
        }
        const int sent = avcodec_send_packet(d->audio, d->packet);
        av_packet_unref(d->packet);
        if (sent < 0 && sent != AVERROR(EAGAIN) && ++errors > kMaxPacketErrors)
            return failure(ErrorCode::Validation, QStringLiteral("Too many damaged audio packets"));
    }
    return {};
}

// Resamples `window` (starting at media sample `windowMediaFirst` at
// `sourceRate`) and copies media samples [first, first + count) at `rate`.
OperationError resampleWindow(const QVector<float> &window, qint64 windowMediaFirst, int sourceRate,
                              qint64 first, qint64 count, int rate, int channels,
                              QVector<float> *output)
{
    AVChannelLayout layout;
    av_channel_layout_default(&layout, channels);
    SwrContext *resampler = nullptr;
    if (swr_alloc_set_opts2(&resampler, &layout, AV_SAMPLE_FMT_FLT, rate, &layout,
                            AV_SAMPLE_FMT_FLT, sourceRate, 0, nullptr) < 0
        || swr_init(resampler) < 0) {
        swr_free(&resampler);
        av_channel_layout_uninit(&layout);
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot create the audio resampler"));
    }
    const qint64 inFrames = window.size() / channels;
    const int capacity = swr_get_out_samples(resampler, int(inFrames)) + 256;
    QVector<float> resampled(qsizetype(capacity) * channels, 0.0f);
    uint8_t *out[1] = {reinterpret_cast<uint8_t *>(resampled.data())};
    const uint8_t *in[1] = {reinterpret_cast<const uint8_t *>(window.constData())};
    int produced = swr_convert(resampler, out, capacity, in, int(inFrames));
    if (produced >= 0 && produced < capacity) {
        uint8_t *tail[1] = {reinterpret_cast<uint8_t *>(resampled.data() + qsizetype(produced) * channels)};
        const int flushed = swr_convert(resampler, tail, capacity - produced, nullptr, 0);
        if (flushed > 0) produced += flushed;
    }
    swr_free(&resampler);
    av_channel_layout_uninit(&layout);
    if (produced < 0) return failure(ErrorCode::BackendFailure, QStringLiteral("Audio resampling failed"));
    const auto windowStart = TimelineTime::rescale(windowMediaFirst, TimelineRate{sourceRate, 1},
                                                   TimelineRate{rate, 1}, TimeMap::NearestAway);
    if (!windowStart) return failure(ErrorCode::Validation, QStringLiteral("Audio position overflows"));
    const qint64 offset = first - *windowStart;
    for (qint64 k = 0; k < count; ++k) {
        const qint64 j = offset + k;
        if (j < 0 || j >= produced) continue;
        std::copy(resampled.cbegin() + j * channels, resampled.cbegin() + (j + 1) * channels,
                  output->begin() + k * channels);
    }
    return {};
}
} // namespace

MediaAudioBlock MediaDecoder::readAudio(qint64 firstSample, qint64 count, int sampleRate, int channels,
                                        const CancellationToken &cancel)
{
    MediaAudioBlock block;
    block.firstSample = firstSample;
    block.sampleRate = sampleRate;
    block.channels = channels;
    if (sampleRate < 1 || sampleRate > 768000 || channels < 1 || channels > 32 || count < 0
        || count > kMaxAudioRead) {
        block.error = failure(ErrorCode::InvalidArgument,
            QStringLiteral("Audio reads need 1–768000 Hz, 1–32 channels and at most %1 sample frames")
                .arg(kMaxAudioRead));
        return block;
    }
    block.samples = QVector<float>(count * channels, 0.0f);
    if (!d->info || !d->info->hasAudio() || count == 0) return block;
    if (cancel.isCancelled()) {
        block.error = MediaFfmpeg::cancelled();
        return block;
    }
    d->cancel = cancel;
    if (OperationError error = d->openAudio()) {
        block.error = error;
        return block;
    }
    if (OperationError error = d->ensureBuffers()) {
        block.error = error;
        return block;
    }
    const MediaSourceInfo &info = *d->info;
    const int sourceRate = info.audioSampleRate;
    const bool resample = sourceRate != sampleRate;
    qint64 streamFirst = 0;
    qint64 streamEnd = 0;
    if (!resample) {
        streamFirst = firstSample - info.audioOffsetSamples;
        streamEnd = streamFirst + count;
    } else {
        constexpr qint64 pad = 64; // resampler filter warm-up, outside the request
        const auto from = TimelineTime::rescale(firstSample, TimelineRate{sampleRate, 1},
                                                TimelineRate{sourceRate, 1}, TimeMap::Floor);
        const auto to = TimelineTime::rescale(firstSample + count, TimelineRate{sampleRate, 1},
                                              TimelineRate{sourceRate, 1}, TimeMap::Ceil);
        if (!from || !to) {
            block.error = failure(ErrorCode::InvalidArgument, QStringLiteral("Audio position overflows"));
            return block;
        }
        streamFirst = *from - info.audioOffsetSamples - pad;
        streamEnd = *to - info.audioOffsetSamples + pad;
    }
    const qint64 readFirst = std::max<qint64>(0, streamFirst);
    const qint64 readEnd = std::min(streamEnd, info.audioSampleCount);
    if (readFirst >= readEnd) return block;
    QVector<float> window((streamEnd - streamFirst) * channels, 0.0f);
    qint64 decoded = 0;
    if (OperationError error = decodeAudioWindow(d.get(), readFirst, readEnd, streamFirst, channels,
                                                 &window, &decoded, cancel)) {
        // The next read reopens the audio demuxer (see videoNeedsReopen).
        d->closeAudio();
        block.error = error;
        return block;
    }
    if (!resample) {
        block.samples = window;
        block.decodedFrames = decoded;
        return block;
    }
    block.decodedFrames = TimelineTime::rescale(decoded, TimelineRate{sourceRate, 1},
                                                TimelineRate{sampleRate, 1}, TimeMap::Floor).value_or(0);
    block.error = resampleWindow(window, streamFirst + info.audioOffsetSamples, sourceRate, firstSample,
                                 count, sampleRate, channels, &block.samples);
    return block;
}
#endif // QINDATK_HAVE_FFMPEG

} // namespace QindaTK
