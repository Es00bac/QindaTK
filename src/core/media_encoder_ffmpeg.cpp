// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_encoder.h"
#include "media_ffmpeg_p.h"

#if defined(QINDATK_HAVE_FFMPEG)

#include <QFile>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace QindaTK {
using MediaFfmpeg::failure;

struct MediaEncoder::Private {
    QString path;
    MediaEncodeSettings settings;
    AVFormatContext *format = nullptr;
    AVCodecContext *video = nullptr;
    AVCodecContext *audio = nullptr;
    AVStream *videoStream = nullptr;
    AVStream *audioStream = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    qint64 videoFrames = 0;
    qint64 audioSamples = 0;
    bool headerWritten = false;
    bool finished = false;

    ~Private() { release(true); }
    void release(bool removePartial);
    OperationError drain(AVCodecContext *codec, AVStream *stream, const AVFrame *input);
    OperationError openVideo();
    OperationError openAudio();
};

void MediaEncoder::Private::release(bool removePartial)
{
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    if (format) {
        if (format->pb) avio_closep(&format->pb);
        avformat_free_context(format);
        format = nullptr;
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    videoStream = nullptr;
    audioStream = nullptr;
    headerWritten = false;
    if (removePartial && !finished && !path.isEmpty()) QFile::remove(path);
}

OperationError MediaEncoder::Private::drain(AVCodecContext *codec, AVStream *stream, const AVFrame *input)
{
    int code = avcodec_send_frame(codec, input);
    if (code < 0 && code != AVERROR_EOF)
        return failure(ErrorCode::BackendFailure, QStringLiteral("Encoding failed: %1")
                                                      .arg(MediaFfmpeg::errorText(code)));
    while (true) {
        code = avcodec_receive_packet(codec, packet);
        if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) return {};
        if (code < 0)
            return failure(ErrorCode::BackendFailure, QStringLiteral("Encoding failed: %1")
                                                          .arg(MediaFfmpeg::errorText(code)));
        av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
        packet->stream_index = stream->index;
        // The muxer takes the packet reference and leaves `packet` blank.
        code = av_interleaved_write_frame(format, packet);
        if (code < 0)
            return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot write media: %1")
                                                          .arg(MediaFfmpeg::errorText(code)));
    }
}

OperationError MediaEncoder::Private::openVideo()
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    if (!codec) return failure(ErrorCode::Unsupported, QStringLiteral("The FFV1 encoder is unavailable"));
    videoStream = avformat_new_stream(format, nullptr);
    video = avcodec_alloc_context3(codec);
    if (!videoStream || !video)
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate the video encoder"));
    const TimelineRate rate = settings.frameRate;
    video->width = settings.frameSize.width();
    video->height = settings.frameSize.height();
    video->pix_fmt = AV_PIX_FMT_0RGB32; // QImage::Format_RGB32 memory order
    video->time_base = AVRational{int(rate.denominator), int(rate.numerator)};
    video->framerate = AVRational{int(rate.numerator), int(rate.denominator)};
    video->sample_aspect_ratio = AVRational{1, 1};
    video->gop_size = settings.gopSize;
    video->thread_count = 1;
    if (format->oformat->flags & AVFMT_GLOBALHEADER) video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int code = avcodec_open2(video, codec, nullptr);
    if (code >= 0) code = avcodec_parameters_from_context(videoStream->codecpar, video);
    if (code < 0)
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot open the FFV1 encoder: %1")
                                                      .arg(MediaFfmpeg::errorText(code)));
    videoStream->time_base = video->time_base;
    videoStream->avg_frame_rate = video->framerate;
    return {};
}

OperationError MediaEncoder::Private::openAudio()
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!codec) return failure(ErrorCode::Unsupported, QStringLiteral("The PCM encoder is unavailable"));
    audioStream = avformat_new_stream(format, nullptr);
    audio = avcodec_alloc_context3(codec);
    if (!audioStream || !audio)
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate the audio encoder"));
    audio->sample_fmt = AV_SAMPLE_FMT_S16;
    audio->sample_rate = settings.sampleRate;
    av_channel_layout_default(&audio->ch_layout, settings.channels);
    audio->time_base = AVRational{1, settings.sampleRate};
    if (format->oformat->flags & AVFMT_GLOBALHEADER) audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int code = avcodec_open2(audio, codec, nullptr);
    if (code >= 0) code = avcodec_parameters_from_context(audioStream->codecpar, audio);
    if (code < 0)
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot open the PCM encoder: %1")
                                                      .arg(MediaFfmpeg::errorText(code)));
    audioStream->time_base = audio->time_base;
    return {};
}

MediaEncoder::MediaEncoder() : d(std::make_unique<Private>()) {}
MediaEncoder::~MediaEncoder() = default;

bool MediaEncoder::available()
{
    return avcodec_find_encoder(AV_CODEC_ID_FFV1) && avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE)
        && av_guess_format("matroska", nullptr, nullptr);
}

OperationError MediaEncoder::open(const QString &path, const MediaEncodeSettings &settings)
{
    d->release(true);
    d->finished = false;
    d->videoFrames = 0;
    d->audioSamples = 0;
    d->path.clear();
    const QSize size = settings.frameSize;
    if (!settings.frameRate.isValid() || settings.gopSize < 1 || settings.gopSize > 600
        || settings.channels < 0 || settings.channels > 8 || (!settings.video && settings.channels == 0)
        || (settings.video && (size.width() < 1 || size.height() < 1
                               || size.width() > 8192 || size.height() > 8192))
        || (settings.channels > 0 && (settings.sampleRate < 8000 || settings.sampleRate > 192000))
        || path.isEmpty())
        return failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid media encode settings"));
    d->path = path;
    d->settings = settings;
    const auto fail = [this](const OperationError &error) {
        d->release(true);
        return error;
    };
    const QByteArray encoded = QFile::encodeName(path);
    int code = avformat_alloc_output_context2(&d->format, nullptr, "matroska", encoded.constData());
    if (code < 0 || !d->format)
        return fail(failure(ErrorCode::Unsupported, QStringLiteral("The Matroska muxer is unavailable")));
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->packet || !d->frame)
        return fail(failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate encode buffers")));
    if (settings.video) {
        if (OperationError error = d->openVideo()) return fail(error);
    }
    if (settings.channels > 0) {
        if (OperationError error = d->openAudio()) return fail(error);
    }
    code = avio_open(&d->format->pb, encoded.constData(), AVIO_FLAG_WRITE);
    if (code >= 0) code = avformat_write_header(d->format, nullptr);
    if (code < 0)
        return fail(failure(ErrorCode::Unavailable, QStringLiteral("Cannot write %1: %2")
                                                        .arg(path, MediaFfmpeg::errorText(code))));
    d->headerWritten = true;
    return {};
}

OperationError MediaEncoder::writeVideo(const QImage &input)
{
    if (!d->video || !d->headerWritten || d->finished)
        return failure(ErrorCode::InvalidArgument, QStringLiteral("The encoder is not open for video"));
    if (input.size() != d->settings.frameSize)
        return failure(ErrorCode::InvalidArgument, QStringLiteral("Frame size %1×%2 does not match the output %3×%4")
                           .arg(input.width()).arg(input.height())
                           .arg(d->settings.frameSize.width()).arg(d->settings.frameSize.height()));
    const QImage image = input.convertToFormat(QImage::Format_RGB32);
    av_frame_unref(d->frame);
    d->frame->format = AV_PIX_FMT_0RGB32;
    d->frame->width = image.width();
    d->frame->height = image.height();
    const int code = av_frame_get_buffer(d->frame, 0);
    if (code < 0)
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate a video frame"));
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(d->frame->data[0] + qsizetype(y) * d->frame->linesize[0], image.constScanLine(y),
                    size_t(image.width()) * 4);
    d->frame->pts = d->videoFrames;
    d->frame->duration = 1;
    const OperationError error = d->drain(d->video, d->videoStream, d->frame);
    av_frame_unref(d->frame);
    if (error) return error;
    ++d->videoFrames;
    return {};
}

OperationError MediaEncoder::writeAudio(const float *interleaved, qint64 frames)
{
    if (!d->audio || !d->headerWritten || d->finished)
        return failure(ErrorCode::InvalidArgument, QStringLiteral("The encoder is not open for audio"));
    if (frames < 0 || (frames > 0 && !interleaved))
        return failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid audio block"));
    const int channels = d->settings.channels;
    constexpr qint64 kChunk = 4096;
    for (qint64 done = 0; done < frames;) {
        const int count = int(std::min(kChunk, frames - done));
        av_frame_unref(d->frame);
        d->frame->format = AV_SAMPLE_FMT_S16;
        d->frame->nb_samples = count;
        d->frame->sample_rate = d->settings.sampleRate;
        int code = av_channel_layout_copy(&d->frame->ch_layout, &d->audio->ch_layout);
        if (code >= 0) code = av_frame_get_buffer(d->frame, 0);
        if (code < 0)
            return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate an audio frame"));
        auto *out = reinterpret_cast<int16_t *>(d->frame->data[0]);
        const float *in = interleaved + done * channels;
        for (qint64 i = 0; i < qint64(count) * channels; ++i) {
            const double value = std::isfinite(in[i]) ? double(in[i]) : 0.0;
            out[i] = int16_t(std::clamp<long>(std::lrint(value * 32768.0), -32768L, 32767L));
        }
        d->frame->pts = d->audioSamples;
        const OperationError error = d->drain(d->audio, d->audioStream, d->frame);
        av_frame_unref(d->frame);
        if (error) return error;
        d->audioSamples += count;
        done += count;
    }
    return {};
}

OperationError MediaEncoder::finish()
{
    if (!d->format || !d->headerWritten || d->finished)
        return failure(ErrorCode::InvalidArgument, QStringLiteral("The encoder is not open"));
    OperationError error;
    if (d->video) error = d->drain(d->video, d->videoStream, nullptr);
    if (!error && d->audio) error = d->drain(d->audio, d->audioStream, nullptr);
    if (!error) {
        const int code = av_write_trailer(d->format);
        if (code < 0)
            error = failure(ErrorCode::BackendFailure, QStringLiteral("Cannot finish %1: %2")
                                                           .arg(d->path, MediaFfmpeg::errorText(code)));
    }
    if (error) {
        d->release(true);
        return error;
    }
    d->finished = true;
    d->release(false);
    return {};
}

void MediaEncoder::abort()
{
    d->release(true);
}

qint64 MediaEncoder::videoFramesWritten() const
{
    return d->videoFrames;
}

qint64 MediaEncoder::audioSamplesWritten() const
{
    return d->audioSamples;
}

} // namespace QindaTK

#endif // QINDATK_HAVE_FFMPEG
