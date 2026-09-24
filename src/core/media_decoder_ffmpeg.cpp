// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_ffmpeg_p.h"

#if defined(QINDATK_HAVE_FFMPEG)

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <algorithm>

namespace QindaTK {
namespace MediaFfmpeg {

QString errorText(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(code, buffer, sizeof buffer) < 0)
        return QStringLiteral("FFmpeg error %1").arg(code);
    return QString::fromUtf8(buffer);
}

OperationError failure(ErrorCode code, const QString &message)
{
    return {code, message, QStringLiteral("uri"), QStringLiteral("ffmpeg")};
}

OperationError cancelled()
{
    return {ErrorCode::Cancelled, QStringLiteral("Media request cancelled"), {},
            QStringLiteral("ffmpeg")};
}

int interrupt(void *opaque)
{
    const auto *token = static_cast<const CancellationToken *>(opaque);
    return token && token->isCancelled() ? 1 : 0;
}

std::optional<TimelineRate> rate(AVRational value)
{
    if (value.num <= 0 || value.den <= 0) return std::nullopt;
    return TimelineRate::normalized(value.num, value.den);
}

std::optional<TimelineRate> unitsPerSecond(AVRational timeBase)
{
    return rate(AVRational{timeBase.den, timeBase.num});
}

QString name(const char *text)
{
    return text ? QString::fromLatin1(text) : QString();
}

AVFormatContext *openFormat(const QString &path, const CancellationToken *token,
                            OperationError *error)
{
    AVFormatContext *format = avformat_alloc_context();
    if (!format) {
        *error = failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate a demuxer"));
        return nullptr;
    }
    format->interrupt_callback.callback = &interrupt;
    format->interrupt_callback.opaque = const_cast<CancellationToken *>(token);
    const QByteArray encoded = QFile::encodeName(path);
    int code = avformat_open_input(&format, encoded.constData(), nullptr, nullptr);
    if (code < 0) {
        // avformat_open_input frees a caller-allocated context on failure.
        *error = token && token->isCancelled() ? cancelled()
            : failure(ErrorCode::Unsupported, QStringLiteral("Cannot open media %1: %2")
                                                  .arg(path, errorText(code)));
        return nullptr;
    }
    code = avformat_find_stream_info(format, nullptr);
    if (code < 0) {
        avformat_close_input(&format);
        *error = token && token->isCancelled() ? cancelled()
            : failure(ErrorCode::Unsupported, QStringLiteral("Cannot read stream information: %1")
                                                  .arg(errorText(code)));
        return nullptr;
    }
    return format;
}

OperationError openDecoder(AVFormatContext *format, int stream, AVCodecContext **context)
{
    const AVStream *source = format->streams[stream];
    const AVCodec *codec = avcodec_find_decoder(source->codecpar->codec_id);
    if (!codec)
        return failure(ErrorCode::Unsupported, QStringLiteral("No decoder for %1")
                           .arg(name(avcodec_get_name(source->codecpar->codec_id))));
    AVCodecContext *decoder = avcodec_alloc_context3(codec);
    if (!decoder) return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate a decoder"));
    int code = avcodec_parameters_to_context(decoder, source->codecpar);
    if (code >= 0) {
        decoder->pkt_timebase = source->time_base;
        // AGENT-NOTE: bounded threads per decoder; pools hold several.
        decoder->thread_count = std::clamp(QThread::idealThreadCount(), 1, 4);
        code = avcodec_open2(decoder, codec, nullptr);
    }
    if (code < 0) {
        avcodec_free_context(&decoder);
        return failure(ErrorCode::Unsupported, QStringLiteral("Cannot open the %1 decoder: %2")
                           .arg(name(codec->name), errorText(code)));
    }
    *context = decoder;
    return {};
}
} // namespace MediaFfmpeg

namespace {
using MediaFfmpeg::failure;

QString appendDamage(const QString &existing, const QString &message)
{
    return existing.isEmpty() ? message : existing + QStringLiteral("; ") + message;
}

QString fieldOrderName(AVFieldOrder order)
{
    switch (order) {
    case AV_FIELD_PROGRESSIVE: return QStringLiteral("progressive");
    case AV_FIELD_TT: return QStringLiteral("tt");
    case AV_FIELD_BB: return QStringLiteral("bb");
    case AV_FIELD_TB: return QStringLiteral("tb");
    case AV_FIELD_BT: return QStringLiteral("bt");
    default: return QStringLiteral("unknown");
    }
}

int chooseVideoStream(AVFormatContext *format)
{
    const int best = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (best >= 0 && !(format->streams[best]->disposition & AV_DISPOSITION_ATTACHED_PIC))
        return best;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const AVStream *stream = format->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC))
            return int(i);
    }
    return -1;
}

OperationError describeVideo(AVFormatContext *format, int index, MediaSourceInfo *info)
{
    AVStream *stream = format->streams[index];
    const AVCodecParameters *parameters = stream->codecpar;
    if (parameters->width < 1 || parameters->height < 1
        || parameters->width > MediaDecoder::kMaxDimension
        || parameters->height > MediaDecoder::kMaxDimension)
        return failure(ErrorCode::Unsupported, QStringLiteral("Video frame size %1×%2 is outside 1–%3")
                           .arg(parameters->width).arg(parameters->height)
                           .arg(MediaDecoder::kMaxDimension));
    const auto units = MediaFfmpeg::unitsPerSecond(stream->time_base);
    if (!units) return failure(ErrorCode::Unsupported, QStringLiteral("The video stream has no usable time base"));
    info->videoStream = index;
    info->videoCodec = MediaFfmpeg::name(avcodec_get_name(parameters->codec_id));
    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(parameters->format));
    info->pixelFormat = descriptor ? MediaFfmpeg::name(descriptor->name) : QString();
    info->hasAlpha = descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA);
    info->width = parameters->width;
    info->height = parameters->height;
    if (const auto aspect = MediaFfmpeg::rate(av_guess_sample_aspect_ratio(format, stream, nullptr)))
        info->sampleAspect = *aspect;
    info->ptsPerSecond = *units;
    info->fieldOrder = fieldOrderName(parameters->field_order);
    info->colorPrimaries = MediaFfmpeg::name(av_color_primaries_name(parameters->color_primaries));
    info->colorTransfer = MediaFfmpeg::name(av_color_transfer_name(parameters->color_trc));
    info->colorMatrix = MediaFfmpeg::name(av_color_space_name(parameters->color_space));
    info->colorRange = MediaFfmpeg::name(av_color_range_name(parameters->color_range));
    return {};
}

void describeAudio(AVFormatContext *format, int index, MediaSourceInfo *info)
{
    const AVStream *stream = format->streams[index];
    const AVCodecParameters *parameters = stream->codecpar;
    const auto units = MediaFfmpeg::unitsPerSecond(stream->time_base);
    if (parameters->sample_rate < 1 || parameters->ch_layout.nb_channels < 1 || !units) return;
    info->audioStream = index;
    info->audioCodec = MediaFfmpeg::name(avcodec_get_name(parameters->codec_id));
    info->audioSampleRate = parameters->sample_rate;
    info->audioChannels = parameters->ch_layout.nb_channels;
    info->audioPtsPerSecond = *units;
    info->audioPtsSampleExact = stream->time_base.num == 1 && stream->time_base.den == parameters->sample_rate;
}

OperationError describeStreams(AVFormatContext *format, MediaSourceInfo *info)
{
    info->container = MediaFfmpeg::name(format->iformat ? format->iformat->name : nullptr);
    info->backendVersion = MediaDecoder::backendVersion();
    info->containerSeconds = format->duration > 0 ? double(format->duration) / double(AV_TIME_BASE) : 0.0;
    const int video = chooseVideoStream(format);
    const int audio = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, video, nullptr, 0);
    if (video < 0 && audio < 0)
        return failure(ErrorCode::Unsupported, QStringLiteral("The file has no audio or video stream"));
    if (video >= 0) {
        if (const OperationError error = describeVideo(format, video, info)) return error;
    }
    if (audio >= 0) describeAudio(format, audio, info);
    const AVDictionaryEntry *timecode = video >= 0
        ? av_dict_get(format->streams[video]->metadata, "timecode", nullptr, 0) : nullptr;
    if (!timecode) timecode = av_dict_get(format->metadata, "timecode", nullptr, 0);
    if (timecode && timecode->value) info->sourceTimecode = QString::fromUtf8(timecode->value);
    return {};
}

void recordAudioPacket(const AVPacket *packet, AVCodecParameters *parameters, qint64 stamp,
                       MediaSourceInfo *info, qint64 *samples)
{
    qint64 count = av_get_audio_frame_duration2(parameters, packet->size);
    if (count <= 0 && packet->duration > 0) {
        const auto converted = TimelineTime::rescale(packet->duration, info->audioPtsPerSecond,
            TimelineRate{info->audioSampleRate, 1}, TimeMap::NearestAway);
        count = converted.value_or(0);
    }
    if (count <= 0 || info->audioPackets.size() >= MediaSourceInfo::kMaxAudioPackets) return;
    if (info->audioPackets.isEmpty()) info->audioStartPts = stamp;
    info->audioPackets.append({stamp, *samples});
    *samples += count;
}

void placeAudio(MediaSourceInfo *info)
{
    if (!info->hasAudio()) return;
    const TimelineRate samples{info->audioSampleRate, 1};
    const auto audioStart = TimelineTime::rescale(info->audioStartPts, info->audioPtsPerSecond,
                                                  samples, TimeMap::NearestAway);
    const auto videoStart = info->hasVideo()
        ? TimelineTime::rescale(info->frames.constFirst().pts, info->ptsPerSecond, samples,
                                TimeMap::NearestAway)
        : audioStart;
    const auto offset = audioStart && videoStart ? TimelineTime::subtract(*audioStart, *videoStart)
                                                 : std::nullopt;
    info->audioOffsetSamples = offset.value_or(0);
}

void detectTruncation(const AVFormatContext *format, MediaSourceInfo *info)
{
    if (format->duration <= 0) return;
    const TimelineRate micros{AV_TIME_BASE, 1};
    qint64 indexedEnd = info->hasVideo() ? info->durationTicks(micros).value_or(0) : 0;
    if (info->hasAudio()) {
        const auto audioEnd = TimelineTime::rescale(info->audioOffsetSamples + info->audioSampleCount,
            TimelineRate{info->audioSampleRate, 1}, micros, TimeMap::Ceil);
        indexedEnd = std::max(indexedEnd, audioEnd.value_or(0));
    }
    const qint64 twoFrames = info->hasVideo()
        ? TimelineTime::rescale(2, info->nominalRate, micros, TimeMap::Ceil).value_or(0) : 0;
    if (format->duration - indexedEnd > std::max<qint64>(250000, twoFrames)) {
        info->truncated = true;
        info->damaged = true;
        info->damage = appendDamage(info->damage, QStringLiteral("The file ends before its declared duration"));
    }
}

// Reads every packet once: video packets become the frame index, audio
// packets the sample index. No frame is decoded here.
OperationError scanIndex(AVFormatContext *format, MediaSourceInfo *info, const CancellationToken &cancel)
{
    AVPacket *packet = av_packet_alloc();
    if (!packet) return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate a packet"));
    AVCodecParameters *audioParameters = info->audioStream >= 0
        ? format->streams[info->audioStream]->codecpar : nullptr;
    qint64 audioSamples = 0;
    int corrupt = 0;
    int untimed = 0;
    OperationError error;
    while (!error) {
        if (cancel.isCancelled()) { error = MediaFfmpeg::cancelled(); break; }
        const int code = av_read_frame(format, packet);
        if (code == AVERROR_EOF) break;
        if (code == AVERROR(EAGAIN)) continue;
        if (code < 0) {
            if (cancel.isCancelled()) { error = MediaFfmpeg::cancelled(); break; }
            info->damaged = true;
            info->damage = appendDamage(info->damage, QStringLiteral("Reading stopped early: %1")
                                                          .arg(MediaFfmpeg::errorText(code)));
            break;
        }
        if (packet->flags & AV_PKT_FLAG_CORRUPT) ++corrupt;
        const qint64 stamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        if (packet->stream_index == info->videoStream) {
            if (stamp == AV_NOPTS_VALUE) {
                ++untimed;
            } else if (info->frames.size() >= MediaSourceInfo::kMaxFrames) {
                error = failure(ErrorCode::Validation, QStringLiteral("The video stream exceeds the indexed frame limit"));
            } else {
                info->frames.append({stamp, packet->dts != AV_NOPTS_VALUE ? packet->dts : stamp,
                                     std::max<qint64>(0, packet->duration),
                                     (packet->flags & AV_PKT_FLAG_KEY) != 0});
            }
        } else if (audioParameters && packet->stream_index == info->audioStream
                   && stamp != AV_NOPTS_VALUE) {
            recordAudioPacket(packet, audioParameters, stamp, info, &audioSamples);
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if (error) return error;
    info->audioSampleCount = audioSamples;
    if (info->audioStream >= 0 && info->audioPackets.isEmpty()) {
        info->audioStream = -1;
        info->audioSampleRate = 0;
        info->audioChannels = 0;
    }
    if (corrupt > 0) {
        info->damaged = true;
        info->damage = appendDamage(info->damage, QStringLiteral("%1 corrupt packets").arg(corrupt));
    }
    if (untimed > 0) {
        info->damaged = true;
        info->damage = appendDamage(info->damage,
            QStringLiteral("%1 video packets without timestamps were not indexed").arg(untimed));
    }
    if (info->videoStream >= 0) {
        if (info->frames.isEmpty())
            return failure(ErrorCode::Validation, QStringLiteral("The video stream has no indexable frames"));
        const AVStream *stream = format->streams[info->videoStream];
        info->finalizeIndex(MediaFfmpeg::rate(stream->r_frame_rate), MediaFfmpeg::rate(stream->avg_frame_rate));
    } else {
        info->finalizeIndex(std::nullopt, std::nullopt);
    }
    placeAudio(info);
    detectTruncation(format, info);
    return {};
}
} // namespace

void MediaDecoder::Private::closeAll()
{
    if (scaler) {
        sws_freeContext(scaler);
        scaler = nullptr;
    }
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    avformat_close_input(&videoFormat);
    avformat_close_input(&audioFormat);
    av_packet_free(&packet);
    av_frame_free(&frame);
    info.reset();
    path.clear();
    videoPositioned = false;
    videoDemuxEnded = false;
    videoDecoderEnded = false;
    hasLastVideoPts = false;
    lastVideoPts = 0;
    videoNeedsReopen = false;
}

OperationError MediaDecoder::Private::ensureBuffers()
{
    if (!packet) packet = av_packet_alloc();
    if (!frame) frame = av_frame_alloc();
    if (!packet || !frame)
        return failure(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate decode buffers"));
    return {};
}

void MediaDecoder::Private::closeAudio()
{
    avcodec_free_context(&audio);
    avformat_close_input(&audioFormat);
}

OperationError MediaDecoder::Private::reopenVideo()
{
    avcodec_free_context(&video);
    avformat_close_input(&videoFormat);
    videoPositioned = false;
    videoDemuxEnded = false;
    videoDecoderEnded = false;
    hasLastVideoPts = false;
    videoNeedsReopen = true;
    if (!info || info->videoStream < 0)
        return failure(ErrorCode::Unsupported, QStringLiteral("No video stream is open"));
    OperationError error;
    videoFormat = MediaFfmpeg::openFormat(path, &cancel, &error);
    if (!videoFormat) return error;
    if (info->videoStream >= int(videoFormat->nb_streams)) {
        avformat_close_input(&videoFormat);
        return failure(ErrorCode::Validation, QStringLiteral("The video stream changed since indexing"));
    }
    for (unsigned i = 0; i < videoFormat->nb_streams; ++i)
        videoFormat->streams[i]->discard = int(i) == info->videoStream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    error = MediaFfmpeg::openDecoder(videoFormat, info->videoStream, &video);
    if (error) {
        avformat_close_input(&videoFormat);
        return error;
    }
    // Freshly opened: the demuxer reads from the first packet.
    videoPositioned = true;
    videoNeedsReopen = false;
    return {};
}

OperationError MediaDecoder::Private::openAudio()
{
    if (audio) return {};
    if (!info || info->audioStream < 0)
        return failure(ErrorCode::Unsupported, QStringLiteral("The media has no audio stream"));
    OperationError error;
    audioFormat = MediaFfmpeg::openFormat(path, &cancel, &error);
    if (!audioFormat) return error;
    if (info->audioStream >= int(audioFormat->nb_streams)) {
        avformat_close_input(&audioFormat);
        return failure(ErrorCode::Validation, QStringLiteral("The audio stream changed since indexing"));
    }
    error = MediaFfmpeg::openDecoder(audioFormat, info->audioStream, &audio);
    if (error) {
        avformat_close_input(&audioFormat);
        return error;
    }
    for (unsigned i = 0; i < audioFormat->nb_streams; ++i)
        audioFormat->streams[i]->discard = int(i) == info->audioStream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    return {};
}

MediaDecoder::MediaDecoder() : d(std::make_unique<Private>()) {}
MediaDecoder::~MediaDecoder() = default;

bool MediaDecoder::available()
{
    return true;
}

QString MediaDecoder::backendVersion()
{
    const unsigned format = avformat_version();
    const unsigned codec = avcodec_version();
    return QStringLiteral("FFmpeg %1 (libavformat %2.%3.%4, libavcodec %5.%6.%7)")
        .arg(QString::fromLatin1(av_version_info()))
        .arg(AV_VERSION_MAJOR(format)).arg(AV_VERSION_MINOR(format)).arg(AV_VERSION_MICRO(format))
        .arg(AV_VERSION_MAJOR(codec)).arg(AV_VERSION_MINOR(codec)).arg(AV_VERSION_MICRO(codec));
}

OperationError MediaDecoder::open(const QString &uri, const CancellationToken &cancel)
{
    close();
    const QString path = mediaLocalPath(uri);
    if (path.isEmpty())
        return failure(ErrorCode::Unsupported, QStringLiteral("Media decoding accepts local paths and file URIs"));
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile() || !file.isReadable())
        return failure(ErrorCode::Unavailable, QStringLiteral("Media is missing or unreadable: %1").arg(path));
    if (cancel.isCancelled()) return MediaFfmpeg::cancelled();
    d->cancel = cancel;
    d->path = path;
    OperationError error;
    d->videoFormat = MediaFfmpeg::openFormat(path, &d->cancel, &error);
    if (!d->videoFormat) {
        close();
        return error;
    }
    const qint64 size = file.size();
    const qint64 modified = file.lastModified().toMSecsSinceEpoch();
    d->info = MediaIndexCache::find(path, size, modified);
    if (!d->info) {
        auto indexed = std::make_shared<MediaSourceInfo>();
        indexed->path = path;
        indexed->fileSize = size;
        indexed->modifiedMs = modified;
        error = describeStreams(d->videoFormat, indexed.get());
        if (!error) error = scanIndex(d->videoFormat, indexed.get(), d->cancel);
        if (error) {
            close();
            return error;
        }
        d->info = indexed;
        MediaIndexCache::insert(d->info);
    }
    if (d->info->videoStream >= 0) {
        if (d->info->videoStream >= int(d->videoFormat->nb_streams)) {
            close();
            return failure(ErrorCode::Validation, QStringLiteral("The video stream changed since indexing"));
        }
        for (unsigned i = 0; i < d->videoFormat->nb_streams; ++i)
            d->videoFormat->streams[i]->discard = int(i) == d->info->videoStream ? AVDISCARD_DEFAULT
                                                                                 : AVDISCARD_ALL;
        error = MediaFfmpeg::openDecoder(d->videoFormat, d->info->videoStream, &d->video);
        if (error) {
            close();
            return error;
        }
    }
    error = d->ensureBuffers();
    if (error) close();
    return error;
}

bool MediaDecoder::isOpen() const
{
    return d->info != nullptr;
}

MediaSourceInfoPtr MediaDecoder::info() const
{
    return d->info;
}

void MediaDecoder::close()
{
    d->closeAll();
}

} // namespace QindaTK

#endif // QINDATK_HAVE_FFMPEG
