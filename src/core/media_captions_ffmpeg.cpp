// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_captions.h"
#include "media_ffmpeg_p.h"

#if defined(QINDATK_HAVE_FFMPEG)

namespace QindaTK {
namespace {
constexpr int kMaximumCues = 100000;

bool textCodec(AVCodecID id)
{
    return id == AV_CODEC_ID_SUBRIP || id == AV_CODEC_ID_TEXT || id == AV_CODEC_ID_WEBVTT;
}
} // namespace

MediaCaptionTrack MediaCaptions::readEmbedded(const QString &path, int ordinal)
{
    MediaCaptionTrack track;
    const QString local = mediaLocalPath(path);
    if (local.isEmpty() || ordinal < 0) {
        track.error = MediaFfmpeg::failure(ErrorCode::InvalidArgument, QStringLiteral("Embedded captions need a local media file"));
        return track;
    }
    OperationError error;
    AVFormatContext *format = MediaFfmpeg::openFormat(local, nullptr, &error);
    if (!format) {
        track.error = error;
        return track;
    }
    int stream = -1;
    int seen = 0;
    bool unsupported = false;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const AVCodecParameters *parameters = format->streams[i]->codecpar;
        if (parameters->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
        if (!textCodec(parameters->codec_id)) {
            unsupported = true;
            continue;
        }
        if (seen++ == ordinal) {
            stream = int(i);
            break;
        }
    }
    if (stream < 0) {
        avformat_close_input(&format);
        track.error = MediaFfmpeg::failure(unsupported ? ErrorCode::Unsupported : ErrorCode::Unavailable,
            unsupported ? QStringLiteral("Only text subtitle streams (SubRip, plain text, WebVTT) are read")
                        : QStringLiteral("The media has no text subtitle stream %1").arg(ordinal));
        return track;
    }
    AVStream *subtitles = format->streams[stream];
    track.codec = MediaFfmpeg::name(avcodec_get_name(subtitles->codecpar->codec_id));
    if (const AVDictionaryEntry *language = av_dict_get(subtitles->metadata, "language", nullptr, 0))
        track.language = QString::fromUtf8(language->value);
    AVPacket *packet = av_packet_alloc();
    const AVRational milliseconds{1, 1000};
    int code = 0;
    while (packet && (code = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == stream && packet->pts != AV_NOPTS_VALUE && packet->duration > 0
            && packet->size > 0) {
            CaptionCueRecord cue;
            cue.inMs = av_rescale_q(packet->pts, subtitles->time_base, milliseconds);
            cue.outMs = av_rescale_q(packet->pts + packet->duration, subtitles->time_base, milliseconds);
            QString text = QString::fromUtf8(reinterpret_cast<const char *>(packet->data), packet->size);
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            while (text.endsWith(QLatin1Char('\n'))) text.chop(1);
            cue.text = text;
            cue.language = track.language;
            cue.id = QStringLiteral("cue-%1").arg(track.cues.size() + 1);
            if (cue.inMs >= 0 && cue.outMs > cue.inMs && !text.isEmpty()) track.cues.append(cue);
        }
        av_packet_unref(packet);
        if (track.cues.size() >= kMaximumCues) break;
    }
    av_packet_free(&packet);
    avformat_close_input(&format);
    if (code < 0 && code != AVERROR_EOF)
        track.error = MediaFfmpeg::failure(ErrorCode::BackendFailure, QStringLiteral("Cannot read subtitles: %1")
                                                                         .arg(MediaFfmpeg::errorText(code)));
    else if (track.cues.isEmpty())
        track.error = MediaFfmpeg::failure(ErrorCode::Unavailable, QStringLiteral("The subtitle stream has no timed cues"));
    return track;
}

} // namespace QindaTK

#endif // QINDATK_HAVE_FFMPEG
