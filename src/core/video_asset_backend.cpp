// SPDX-License-Identifier: LGPL-3.0-or-later
#include "video_asset_backend.h"
#include "media_decoder.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>

namespace QindaTK {
namespace {
constexpr qsizetype MaxOutputBytes = 32 * 1024 * 1024;
constexpr qint64 MaxProcessMilliseconds = 30000;

OperationError failure(ErrorCode code, const QString &message)
{ return {code, message, QStringLiteral("uri"), QStringLiteral("video")}; }

QByteArray runVideoTool(const QString &tool, const QStringList &arguments,
                        const CancellationToken &cancel, OperationError *error)
{
    const QString executable = QStandardPaths::findExecutable(tool);
    if (executable.isEmpty()) {
        *error = failure(ErrorCode::Unsupported, QStringLiteral("%1 is not installed").arg(tool));
        return {};
    }
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(2000)) {
        *error = failure(ErrorCode::BackendFailure, process.errorString());
        return {};
    }
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray output, diagnostics;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50);
        output += process.readAllStandardOutput();
        diagnostics += process.readAllStandardError();
        if (cancel.isCancelled() || output.size() > MaxOutputBytes
            || elapsed.elapsed() > MaxProcessMilliseconds) {
            process.kill();
            process.waitForFinished(1000);
            *error = failure(cancel.isCancelled() ? ErrorCode::Cancelled
                : output.size() > MaxOutputBytes ? ErrorCode::Validation
                : ErrorCode::BackendFailure,
                cancel.isCancelled() ? QStringLiteral("Video request cancelled")
                : output.size() > MaxOutputBytes ? QStringLiteral("Video frame exceeds output limit")
                : QStringLiteral("Video decoder timed out"));
            return {};
        }
    }
    output += process.readAllStandardOutput();
    diagnostics += process.readAllStandardError();
    if (output.size() > MaxOutputBytes || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        *error = failure(ErrorCode::BackendFailure,
            QStringLiteral("%1 failed: %2").arg(tool, QString::fromUtf8(diagnostics.left(2048)).trimmed()));
        return {};
    }
    return output;
}

AssetResult videoMetadata(const QString &path, const CancellationToken &cancel)
{
    AssetResult result;
    const QByteArray bytes = runVideoTool(QStringLiteral("ffprobe"),
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
         QStringLiteral("v:0"), QStringLiteral("-show_entries"),
         QStringLiteral("stream=codec_name,width,height,avg_frame_rate,duration:format=duration"),
         QStringLiteral("-of"), QStringLiteral("json"), path}, cancel, &result.error);
    if (result.error) return result;
    const QJsonDocument json = QJsonDocument::fromJson(bytes);
    const QJsonArray streams = json.object().value(QStringLiteral("streams")).toArray();
    if (streams.isEmpty()) {
        result.error = failure(ErrorCode::Unsupported, QStringLiteral("No video stream in asset"));
        return result;
    }
    const QJsonObject stream = streams.first().toObject();
    result.metadata = {{QStringLiteral("kind"), QStringLiteral("metadata")},
        {QStringLiteral("mediaType"), QStringLiteral("video")},
        {QStringLiteral("codec"), stream.value(QStringLiteral("codec_name")).toString()},
        {QStringLiteral("width"), stream.value(QStringLiteral("width")).toInt()},
        {QStringLiteral("height"), stream.value(QStringLiteral("height")).toInt()},
        {QStringLiteral("frameRate"), stream.value(QStringLiteral("avg_frame_rate")).toString()},
        {QStringLiteral("durationSeconds"),
         json.object().value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble()}};
    return result;
}
// AGENT-NOTE (D-096): with the FFmpeg libraries a time request maps to an
// exact presentation index through MediaSourceInfo's rate policy, so both
// request forms report exactFrameIndex: true and the resolved frameIndex.
AssetResult libraryVideo(const QString &path, const AssetRequest &request, const CancellationToken &cancel,
                         const JobProgress &progress)
{
    AssetResult result;
    MediaDecoder decoder;
    if (const OperationError error = decoder.open(path, cancel)) {
        result.error = error;
        return result;
    }
    const MediaSourceInfoPtr info = decoder.info();
    if (request.kind == QStringLiteral("metadata")) {
        result.metadata = info->toVariantMap();
        result.metadata.insert(QStringLiteral("kind"), QStringLiteral("metadata"));
        result.metadata.insert(QStringLiteral("mediaType"), info->hasVideo() ? QStringLiteral("video")
                                                                             : QStringLiteral("audio"));
        return result;
    }
    if (!info->hasVideo()) {
        result.error = failure(ErrorCode::Unsupported, QStringLiteral("The media has no video frames"));
        return result;
    }
    const auto rate = TimelineRate::normalized(request.rateNumerator, request.rateDenominator);
    if (request.timeTicks < 0 || !rate) {
        result.error = failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid video timestamp or rate"));
        return result;
    }
    bool byIndex = false;
    qint64 index = request.frameId.toLongLong(&byIndex);
    if (!byIndex) {
        const auto frame = info->frameAtTicks(request.timeTicks, *rate);
        if (!frame) {
            result.error = failure(ErrorCode::InvalidArgument, QStringLiteral("The requested time is outside the media"));
            return result;
        }
        index = *frame;
    }
    if (progress) progress(0.0);
    const int limit = std::min(4096, MediaDecoder::kMaxDimension);
    const QSize bound = request.size.width() > 0 || request.size.height() > 0
        ? QSize(std::clamp(std::max(request.size.width(), request.size.height()), 1, limit),
                std::clamp(std::max(request.size.width(), request.size.height()), 1, limit))
        : QSize(limit, limit);
    const MediaDecodedFrame decoded = decoder.decodeFrame(index, bound, cancel);
    if (decoded.error) {
        result.error = decoded.error;
        return result;
    }
    QBuffer buffer(&result.bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !decoded.image.save(&buffer, "PNG")) {
        result.error = failure(ErrorCode::BackendFailure, QStringLiteral("Cannot encode the video frame"));
        result.bytes.clear();
        return result;
    }
    result.mimeType = QStringLiteral("image/png");
    result.metadata = {{QStringLiteral("kind"), request.kind},
        {QStringLiteral("width"), decoded.image.width()}, {QStringLiteral("height"), decoded.image.height()},
        {QStringLiteral("requestedFrameId"), request.frameId},
        {QStringLiteral("exactFrameIndex"), true},
        {QStringLiteral("frameIndex"), QString::number(index)},
        {QStringLiteral("framePts"), QString::number(decoded.pts)},
        {QStringLiteral("corrupt"), decoded.corrupt},
        {QStringLiteral("timeTicks"), QString::number(request.timeTicks)},
        {QStringLiteral("rateNumerator"), QString::number(request.rateNumerator)},
        {QStringLiteral("rateDenominator"), QString::number(request.rateDenominator)}};
    if (progress) progress(1.0);
    return result;
}
} // namespace

bool isVideoFileSuffix(const QString &suffix)
{
    static const QStringList names{QStringLiteral("mp4"), QStringLiteral("mov"),
        QStringLiteral("mkv"), QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("m4v"), QStringLiteral("mpeg"), QStringLiteral("mpg")};
    return names.contains(suffix.toLower());
}

AssetResult fetchVideoAsset(const QString &path, const AssetRequest &request,
                            const CancellationToken &cancel, const JobProgress &progress)
{
    if (MediaDecoder::available()) return libraryVideo(path, request, cancel, progress);
    if (request.kind == QStringLiteral("metadata")) return videoMetadata(path, cancel);
    AssetResult result;
    if (request.timeTicks < 0 || request.rateNumerator <= 0 || request.rateDenominator <= 0) {
        result.error = failure(ErrorCode::InvalidArgument, QStringLiteral("Invalid video timestamp or rate"));
        return result;
    }
    if (progress) progress(0.0);
    const int dimension = std::clamp(std::max(request.size.width(), request.size.height()), 1, 4096);
    QStringList arguments{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"), QStringLiteral("-nostdin")};
    bool exactIndex = false;
    const qint64 index = request.frameId.toLongLong(&exactIndex);
    exactIndex = exactIndex && index >= 0;
    if (!exactIndex) {
        const long double seconds = static_cast<long double>(request.timeTicks)
            * static_cast<long double>(request.rateDenominator)
            / static_cast<long double>(request.rateNumerator);
        arguments << QStringLiteral("-ss") << QString::number(static_cast<double>(seconds), 'f', 9);
    }
    arguments << QStringLiteral("-i") << path << QStringLiteral("-frames:v") << QStringLiteral("1");
    QString filter;
    if (exactIndex) filter = QStringLiteral("select=eq(n\\,%1),").arg(index);
    filter += QStringLiteral("scale=%1:%1:force_original_aspect_ratio=decrease").arg(dimension);
    arguments << QStringLiteral("-vf") << filter << QStringLiteral("-f")
              << QStringLiteral("image2pipe") << QStringLiteral("-vcodec")
              << QStringLiteral("png") << QStringLiteral("pipe:1");
    result.bytes = runVideoTool(QStringLiteral("ffmpeg"), arguments, cancel, &result.error);
    if (result.error) return result;
    QImage image;
    if (!image.loadFromData(result.bytes, "PNG") || image.width() < 1 || image.height() < 1) {
        result.error = failure(ErrorCode::Validation, QStringLiteral("Requested video frame is unavailable"));
        result.bytes.clear();
        return result;
    }
    result.mimeType = QStringLiteral("image/png");
    result.metadata = {{QStringLiteral("kind"), request.kind},
        {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()},
        {QStringLiteral("requestedFrameId"), request.frameId},
        {QStringLiteral("exactFrameIndex"), exactIndex},
        {QStringLiteral("timeTicks"), QString::number(request.timeTicks)},
        {QStringLiteral("rateNumerator"), QString::number(request.rateNumerator)},
        {QStringLiteral("rateDenominator"), QString::number(request.rateDenominator)}};
    if (progress) progress(1.0);
    return result;
}

} // namespace QindaTK
