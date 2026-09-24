// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_export.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace QindaTK {
namespace {
constexpr qint64 kHashChunk = 1024 * 1024;

OperationError exportError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("media-export")};
}

// SHA-256 hex of a local file, or an empty string when it cannot be read.
QString hashFile(const QString &path, const CancellationToken &cancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel.isCancelled()) return {};
        const QByteArray chunk = file.read(kHashChunk);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
        digest.addData(chunk);
    }
    return QString::fromLatin1(digest.result().toHex());
}

QVector<MediaClipSpan> allSpans(const MediaProgram &program)
{
    return program.video + program.audio;
}
} // namespace

QString MediaExport::providerId()
{
    return QStringLiteral("qindatk.media-program-mkv");
}

MediaExportResult MediaExport::exportProgram(const MediaProgram &program, const QString &path, qint64 firstFrame,
                                             qint64 frameCount, const CancellationToken &cancel,
                                             const JobProgress &progress)
{
    MediaExportResult result;
    const auto fail = [&result](const OperationError &error) {
        result.error = error;
        return result;
    };
    if (program.isEmpty())
        return fail(exportError(ErrorCode::InvalidArgument, QStringLiteral("The program has no frames to export")));
    if (program.kind == MediaProgram::Kind::Source && program.sourceInfo && !program.sourceInfo->constantRate)
        return fail(exportError(ErrorCode::Unsupported,
            QStringLiteral("Export a variable-rate source through a timeline program, which conforms it")));
    if (firstFrame < 0 || frameCount < 1 || firstFrame > program.frameCount - frameCount)
        return fail(exportError(ErrorCode::InvalidArgument, QStringLiteral("Frames %1–%2 are outside the program's %3 frames")
                                   .arg(firstFrame).arg(firstFrame + frameCount - 1).arg(program.frameCount)));
    if (!program.unresolved.isEmpty()) {
        result.missingResourceIds = program.unresolved;
        return fail(exportError(ErrorCode::Unavailable, QStringLiteral("Missing media: %1").arg(program.unresolved.join(QStringLiteral(", ")))));
    }
    MediaEncodeSettings settings;
    settings.frameSize = program.frameSize;
    settings.frameRate = program.frameRate;
    settings.sampleRate = program.sampleRate;
    settings.channels = program.channels;
    settings.gopSize = 1;
    if (!program.finishing.embeddedCaptions.isEmpty()) {
        // Embedded captions are timed from the export's first frame (D-126).
        const auto offset = program.frameStartMilliseconds(firstFrame);
        if (!offset) return fail(exportError(ErrorCode::Validation, QStringLiteral("The first frame has no program time")));
        settings.subtitles = program.finishing.subtitlesFrom(*offset);
        settings.subtitleLanguage = program.finishing.embeddedLanguage;
    }
    MediaEncoder encoder;
    if (OperationError error = encoder.open(path, settings)) return fail(error);
    MediaDecoderPool videoPool;
    MediaDecoderPool audioPool;
    const auto abort = [&](const OperationError &error, const QStringList &missing = {}) {
        encoder.abort();
        result.missingResourceIds = missing;
        result.error = error;
        return result;
    };
    for (qint64 frame = firstFrame; frame < firstFrame + frameCount; ++frame) {
        if (cancel.isCancelled()) return abort(exportError(ErrorCode::Cancelled, QStringLiteral("Export cancelled")));
        const MediaRenderedFrame rendered = program.render(frame, program.frameSize, videoPool, cancel);
        if (!rendered.missingResourceIds.isEmpty())
            return abort(exportError(ErrorCode::Unavailable, QStringLiteral("Missing media: %1")
                                         .arg(rendered.missingResourceIds.join(QStringLiteral(", ")))),
                         rendered.missingResourceIds);
        if (rendered.error) return abort(rendered.error);
        if (OperationError error = encoder.writeVideo(rendered.image)) return abort(error);
        const auto from = program.frameStartSample(frame);
        const auto to = program.frameStartSample(frame + 1);
        if (!from || !to || *to < *from)
            return abort(exportError(ErrorCode::Validation, QStringLiteral("Frame %1 has no exact sample span").arg(frame)));
        const MediaAudioMix mix = program.mixAudio(*from, *to - *from, audioPool, cancel);
        if (!mix.missingResourceIds.isEmpty())
            return abort(exportError(ErrorCode::Unavailable, QStringLiteral("Missing media: %1")
                                         .arg(mix.missingResourceIds.join(QStringLiteral(", ")))),
                         mix.missingResourceIds);
        if (mix.error) return abort(mix.error);
        if (OperationError error = encoder.writeAudio(mix.samples.constData(), *to - *from)) return abort(error);
        if (progress) progress(double(frame - firstFrame + 1) / double(frameCount));
    }
    if (OperationError error = encoder.finish()) return fail(error);
    result.frames = encoder.videoFramesWritten();
    result.samples = encoder.audioSamplesWritten();
    result.subtitles = encoder.subtitlesWritten();
    return result;
}

RenderProduct MediaExport::renderProvider(const QJsonObject &snapshot, const QString &stageDirectory,
                                          const CancellationToken &cancel, const JobProgress &progress)
{
    const QJsonObject settings = snapshot.value(QStringLiteral("settings")).toObject();
    QString error;
    const auto program = MediaProgram::fromJson(settings.value(QStringLiteral("program")).toObject(), &error);
    if (!program) return {{}, error};
    QHash<QString, QString> verified;
    for (const QJsonValue &value : settings.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject resource = value.toObject();
        const QString id = resource.value(QStringLiteral("resourceId")).toString();
        const QString path = resource.value(QStringLiteral("path")).toString();
        const QString expected = resource.value(QStringLiteral("sha256")).toString();
        if (cancel.isCancelled()) return {{}, QStringLiteral("Cancelled")};
        if (id.isEmpty() || !QFileInfo(path).isAbsolute() || expected.size() != 64
            || hashFile(path, cancel) != expected)
            return {{}, QStringLiteral("Source media %1 changed or went offline after the export was queued").arg(id)};
        verified.insert(id, path);
    }
    for (QVector<MediaClipSpan> *spans : {&program->video, &program->audio}) {
        for (MediaClipSpan &span : *spans) {
            if (!verified.contains(span.resourceId))
                return {{}, QStringLiteral("Source media %1 has no verified revision").arg(span.resourceId)};
            span.uri = verified.value(span.resourceId);
        }
    }
    const auto first = TimelineTime::parseDecimal(settings.value(QStringLiteral("firstFrame")).toString());
    const auto count = TimelineTime::parseDecimal(settings.value(QStringLiteral("frameCount")).toString());
    if (!first || !count) return {{}, QStringLiteral("The export range must be exact decimal frames")};
    const QString name = QStringLiteral("program.mkv");
    const MediaExportResult exported = exportProgram(*program, QDir(stageDirectory).filePath(name), *first, *count,
                                                     cancel, progress);
    if (exported.error) return {{}, exported.error.message};
    QJsonObject product{{QStringLiteral("file"), name},
        {QStringLiteral("container"), QStringLiteral("matroska")},
        {QStringLiteral("videoCodec"), QStringLiteral("ffv1")},
        {QStringLiteral("audioCodec"), QStringLiteral("pcm_s16le")},
        {QStringLiteral("frames"), QString::number(exported.frames)},
        {QStringLiteral("samples"), QString::number(exported.samples)},
        {QStringLiteral("frameRate"), QStringLiteral("%1/%2").arg(program->frameRate.numerator)
                                          .arg(program->frameRate.denominator)},
        {QStringLiteral("sampleRate"), program->sampleRate},
        {QStringLiteral("channels"), program->channels},
        {QStringLiteral("width"), program->frameSize.width()},
        {QStringLiteral("height"), program->frameSize.height()}};
    if (exported.subtitles > 0) {
        product.insert(QStringLiteral("subtitleCodec"), QStringLiteral("subrip"));
        product.insert(QStringLiteral("subtitleLanguage"), program->finishing.embeddedLanguage);
        product.insert(QStringLiteral("subtitles"), QString::number(exported.subtitles));
    }
    if (program->finishing.changesPictures())
        product.insert(QStringLiteral("finishing"), program->finishing.digest());
    return {QJsonArray{product}, {}};
}

QVariantMap MediaExport::queueSnapshot(const MediaProgram &program, const QString &outputPath, const QString &label,
                                       const QHash<QString, QString> &knownHashes, QString *error)
{
    const auto reject = [error](const QString &message) {
        if (error) *error = message;
        return QVariantMap();
    };
    if (program.kind != MediaProgram::Kind::Timeline || program.isEmpty())
        return reject(QStringLiteral("Only a non-empty timeline program can be queued for export"));
    if (!program.unresolved.isEmpty())
        return reject(QStringLiteral("Missing media: %1").arg(program.unresolved.join(QStringLiteral(", "))));
    QVariantList resources;
    QStringList digests;
    QSet<QString> seen;
    for (const MediaClipSpan &span : allSpans(program)) {
        if (seen.contains(span.resourceId)) continue;
        seen.insert(span.resourceId);
        const QString path = mediaLocalPath(span.uri);
        if (path.isEmpty() || !QFileInfo(path).isFile())
            return reject(QStringLiteral("Missing media: %1").arg(span.resourceId));
        QString digest = knownHashes.value(span.resourceId);
        if (digest.size() != 64) digest = hashFile(path, {});
        if (digest.size() != 64) return reject(QStringLiteral("Cannot read media %1").arg(span.resourceId));
        resources.append(QVariantMap{{QStringLiteral("resourceId"), span.resourceId},
                                     {QStringLiteral("path"), path}, {QStringLiteral("sha256"), digest}});
        digests << span.resourceId + QLatin1Char('=') + digest;
    }
    const QJsonObject programJson = program.toJson();
    const QByteArray identity = QJsonDocument(programJson).toJson(QJsonDocument::Compact)
        + digests.join(QLatin1Char(';')).toUtf8();
    const QString revision = QStringLiteral("sha256:")
        + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    if (error) error->clear();
    return {{QStringLiteral("label"), label}, {QStringLiteral("providerId"), providerId()},
            {QStringLiteral("providerVersion"), QStringLiteral("1")}, {QStringLiteral("format"), QStringLiteral("mkv")},
            {QStringLiteral("capabilityId"), QStringLiteral("media-program-mkv")},
            {QStringLiteral("sourceId"), program.programId.isEmpty() ? QStringLiteral("program") : program.programId},
            {QStringLiteral("sourceRevision"), revision}, {QStringLiteral("outputPath"), outputPath},
            {QStringLiteral("resourceSlots"), 1},
            {QStringLiteral("settings"), QVariantMap{{QStringLiteral("program"), programJson.toVariantMap()},
                {QStringLiteral("firstFrame"), QStringLiteral("0")},
                {QStringLiteral("frameCount"), QString::number(program.frameCount)},
                {QStringLiteral("resources"), resources}}}};
}

} // namespace QindaTK
