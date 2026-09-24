// SPDX-License-Identifier: LGPL-3.0-or-later
#include "render_queue_model.h"
#include "caption_frame_renderer.h"
#include "caption_track_model.h"
#include "rich_text_document.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

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

RenderProduct RenderQueueModel::renderRichDocumentPdf(const QJsonObject &snapshot,
    const QString &stageDirectory, const CancellationToken &cancel, const JobProgress &progress)
{
    if (cancel.isCancelled()) return {{}, QStringLiteral("Cancelled")};
    const QJsonObject settings = snapshot.value(QStringLiteral("settings")).toObject();
    const QByteArray bytes = settings.value(QStringLiteral("documentJson")).toString().toUtf8();
    if (bytes.isEmpty() || bytes.size() > 16 * 1024 * 1024)
        return {{}, QStringLiteral("Expected a rich document snapshot within 16 MiB")};
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        bytes, QCryptographicHash::Sha256).toHex());
    if (settings.value(QStringLiteral("documentSha256")).toString() != digest
        || snapshot.value(QStringLiteral("sourceRevision")).toString()
            != QStringLiteral("sha256:") + digest)
        return {{}, QStringLiteral("Rich document snapshot hash or revision changed")};
    RichTextDocument document;
    if (!document.loadJson(QString::fromUtf8(bytes)))
        return {{}, QStringLiteral("Invalid rich document: %1").arg(document.lastError())};
    progress(0.25);
    if (cancel.isCancelled()) return {{}, QStringLiteral("Cancelled")};
    const QString name = QStringLiteral("document.pdf");
    if (!document.exportPdf(QDir(stageDirectory).filePath(name)))
        return {{}, QStringLiteral("PDF export failed: %1").arg(document.lastError())};
    progress(1.0);
    return {QJsonArray{QJsonObject{{QStringLiteral("file"), name}}}, {}};
}

RenderProduct renderPcm16WavGain(const QJsonObject &snapshot, const QString &stageDirectory,
    const CancellationToken &cancel, const JobProgress &progress)
{
    if (cancel.isCancelled()) return {{}, QStringLiteral("Cancelled")};
    const QJsonObject settings = snapshot.value(QStringLiteral("settings")).toObject();
    const QString sourcePath = settings.value(QStringLiteral("sourcePath")).toString();
    const QJsonValue gainValue = settings.value(QStringLiteral("gain"));
    const double gain = gainValue.toDouble(-1);
    QFile source(sourcePath);
    if (!QFileInfo(sourcePath).isAbsolute() || !gainValue.isDouble()
        || !std::isfinite(gain) || gain < 0 || gain > 4
        || !source.open(QIODevice::ReadOnly) || source.size() < 44
        || source.size() > 64 * 1024 * 1024)
        return {{}, QStringLiteral("Expected an absolute PCM16 WAV within 64 MiB and gain 0–4")};
    const QByteArray original = source.readAll();
    if (original.size() != source.size()) return {{}, QStringLiteral("Cannot read source WAV")};
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        original, QCryptographicHash::Sha256).toHex());
    if (settings.value(QStringLiteral("sourceSha256")).toString() != digest
        || snapshot.value(QStringLiteral("sourceRevision")).toString()
            != QStringLiteral("sha256:") + digest)
        return {{}, QStringLiteral("Source WAV hash or revision changed")};
    auto byte = [&](int offset) { return quint32(quint8(original.at(offset))); };
    auto u16 = [&](int offset) { return byte(offset) | (byte(offset + 1) << 8); };
    auto u32 = [&](int offset) { return u16(offset) | (u16(offset + 2) << 16); };
    if (original.left(4) != "RIFF" || original.mid(8, 4) != "WAVE"
        || quint64(u32(4)) + 8 > quint64(original.size()))
        return {{}, QStringLiteral("Invalid RIFF/WAVE header")};
    bool formatSeen = false;
    int dataAt = -1, dataBytes = 0;
    quint32 channels = 0, rate = 0;
    for (quint64 pos = 12; pos + 8 <= quint64(original.size());) {
        const int at = int(pos);
        const quint32 size = u32(at + 4);
        const quint64 end = pos + 8 + quint64(size);
        if (end > quint64(original.size())) return {{}, QStringLiteral("Truncated WAV chunk")};
        const QByteArray tag = original.mid(at, 4);
        if (tag == "fmt " && !formatSeen) {
            if (size < 16) return {{}, QStringLiteral("Short WAV format chunk")};
            channels = u16(at + 10);
            rate = u32(at + 12);
            formatSeen = u16(at + 8) == 1 && (channels == 1 || channels == 2)
                && rate >= 8000 && rate <= 192000 && u16(at + 20) == channels * 2
                && u16(at + 22) == 16 && u32(at + 16) == rate * channels * 2;
        } else if (tag == "data" && dataAt < 0) {
            dataAt = at + 8;
            dataBytes = int(size);
        }
        pos = end + (size & 1);
    }
    if (!formatSeen || dataAt < 0 || dataBytes <= 0
        || dataBytes % int(channels * 2) != 0)
        return {{}, QStringLiteral("Only uncompressed mono/stereo PCM16 WAV is supported")};
    QByteArray rendered = original;
    for (int at = dataAt; at < dataAt + dataBytes; at += 2) {
        if (((at - dataAt) & 8191) == 0) {
            if (cancel.isCancelled()) return {{}, QStringLiteral("Cancelled")};
            progress(double(at - dataAt) / double(dataBytes));
        }
        const int unsignedSample = int(byte(at) | (byte(at + 1) << 8));
        const int sample = unsignedSample >= 32768 ? unsignedSample - 65536 : unsignedSample;
        const int scaled = std::clamp(int(std::lround(double(sample) * gain)), -32768, 32767);
        rendered[at] = char(scaled & 255);
        rendered[at + 1] = char((scaled >> 8) & 255);
    }
    const QString name = QStringLiteral("audio.wav");
    QSaveFile target(QDir(stageDirectory).filePath(name));
    if (!target.open(QIODevice::WriteOnly) || target.write(rendered) != rendered.size()
        || !target.commit()) return {{}, QStringLiteral("Cannot write rendered WAV")};
    progress(1.0);
    return {QJsonArray{QJsonObject{{QStringLiteral("file"), name},
        {QStringLiteral("sampleRate"), int(rate)}, {QStringLiteral("channels"), int(channels)}}}, {}};
}
} // namespace QindaTK
