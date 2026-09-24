// SPDX-License-Identifier: LGPL-3.0-or-later
#include "render_queue_model.h"
#include "caption_frame_renderer.h"
#include "caption_track_model.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace QindaTK {
RenderProduct RenderQueueModel::renderCaptionPngSequence(const QJsonObject &snapshot,
    const QString &stageDirectory, const CancellationToken &cancel, const JobProgress &progress)
{
    const QJsonObject settings = snapshot.value(QStringLiteral("settings")).toObject();
    const QJsonArray frames = settings.value(QStringLiteral("frames")).toArray();
    if (frames.isEmpty() || frames.size() > 10000)
        return {{}, QStringLiteral("Expected 1–10000 source PNG frames")};
    CaptionTrackModel track;
    if (!track.loadJson(settings.value(QStringLiteral("captionTrackJson")).toString()))
        return {{}, QStringLiteral("Invalid caption track: %1").arg(track.error())};
    CaptionFrameRenderer renderer;
    QJsonArray files;
    for (int i = 0; i < frames.size(); ++i) {
        if (cancel.isCancelled()) return {{}, QStringLiteral("Cancelled")};
        const QJsonObject frame = frames.at(i).toObject();
        const QString source = frame.value(QStringLiteral("path")).toString();
        const QString expected = frame.value(QStringLiteral("sha256")).toString();
        const QString time = frame.value(QStringLiteral("timeMs")).toString();
        QFile input(source);
        if (expected.size() != 64 || !input.open(QIODevice::ReadOnly)
            || input.size() <= 0 || input.size() > 256 * 1024 * 1024)
            return {{}, QStringLiteral("Source frame %1 is missing or exceeds 256 MiB").arg(i)};
        const QByteArray bytes = input.readAll();
        if (bytes.size() != input.size()
            || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != expected)
            return {{}, QStringLiteral("Source frame %1 changed or is unavailable").arg(i)};
        const QImage image = QImage::fromData(bytes, "PNG");
        if (image.isNull() || qint64(image.width()) * image.height() > 100000000)
            return {{}, QStringLiteral("Cannot decode source frame %1 within pixel limit").arg(i)};
        const QVariantMap rendered = renderer.renderFrame(image, &track, time);
        if (!rendered.value(QStringLiteral("ok")).toBool())
            return {{}, rendered.value(QStringLiteral("error")).toString()};
        const QString name = QStringLiteral("frame-%1.png").arg(i, 6, 10, QLatin1Char('0'));
        if (!renderer.saveRenderedFrame(QDir(stageDirectory).filePath(name)))
            return {{}, renderer.error()};
        QJsonArray cueIds;
        for (const QString &cue : renderer.activeCueIds()) cueIds.append(cue);
        files.append(QJsonObject{{QStringLiteral("file"), name},
                                 {QStringLiteral("timeMs"), time},
                                 {QStringLiteral("activeCueIds"), cueIds}});
        progress(double(i + 1) / double(frames.size()));
    }
    return {files, {}};
}
} // namespace QindaTK
