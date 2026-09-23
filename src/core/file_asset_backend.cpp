// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_provider.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr qint64 MaxAssetBytes = 256LL * 1024 * 1024;
constexpr qint64 MaxImagePixels = 100LL * 1000 * 1000;
constexpr qint64 MaxOutputPixels = 32LL * 1000 * 1000;
constexpr int MaxDimension = 32768;
constexpr int MaxPeakColumns = 8192;

OperationError error(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, QStringLiteral("file")};
}

bool localPath(const QString &uri, QString *path)
{
    const QUrl url(uri);
    if (!url.isValid()) return false;
    if (url.scheme().isEmpty()) *path = QFileInfo(uri).absoluteFilePath();
    else if (url.isLocalFile()) *path = url.toLocalFile();
    else return false;
    return !path->isEmpty();
}

quint16 u16(const QByteArray &bytes, qsizetype at)
{
    return quint16(quint8(bytes.at(at))) | (quint16(quint8(bytes.at(at + 1))) << 8);
}

quint32 u32(const QByteArray &bytes, qsizetype at)
{
    return quint32(u16(bytes, at)) | (quint32(u16(bytes, at + 2)) << 16);
}

bool cancelled(const CancellationToken &token, AssetResult *result)
{
    if (!token.isCancelled()) return false;
    result->error = error(ErrorCode::Cancelled, QStringLiteral("Asset request cancelled"));
    return true;
}

AssetResult readWave(const QString &path, const AssetRequest &request,
                     const CancellationToken &token, const JobProgress &progress)
{
    AssetResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = error(ErrorCode::Unavailable, file.errorString(), QStringLiteral("uri"));
        return result;
    }
    const qint64 fileSize = file.size();
    if (fileSize < 12 || fileSize > MaxAssetBytes) {
        result.error = error(ErrorCode::Validation, QStringLiteral("WAV file size is invalid or exceeds the read limit"));
        return result;
    }
    const QByteArray header = file.read(12);
    if (header.size() != 12 || header.left(4) != QByteArrayLiteral("RIFF")
        || header.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        result.error = error(ErrorCode::Unsupported, QStringLiteral("Only RIFF/WAVE audio is supported"));
        return result;
    }

    quint16 format = 0, channels = 0, bits = 0, blockAlign = 0;
    quint32 sampleRate = 0;
    qint64 dataOffset = -1;
    quint32 dataBytes = 0;
    while (file.pos() + 8 <= fileSize) {
        if (cancelled(token, &result)) return result;
        const QByteArray chunk = file.read(8);
        if (chunk.size() != 8) break;
        const quint32 length = u32(chunk, 4);
        const qint64 payload = file.pos();
        if (qint64(length) > fileSize - payload) {
            result.error = error(ErrorCode::Validation, QStringLiteral("WAV chunk extends beyond the file"));
            return result;
        }
        if (chunk.left(4) == QByteArrayLiteral("fmt ")) {
            if (length < 16 || length > 4096) {
                result.error = error(ErrorCode::Validation, QStringLiteral("WAV format chunk is invalid"));
                return result;
            }
            const QByteArray fmt = file.read(length);
            if (fmt.size() != int(length)) break;
            format = u16(fmt, 0); channels = u16(fmt, 2); sampleRate = u32(fmt, 4);
            blockAlign = u16(fmt, 12); bits = u16(fmt, 14);
        } else if (chunk.left(4) == QByteArrayLiteral("data")) {
            dataOffset = payload; dataBytes = length;
        }
        if (!file.seek(payload + length + (length & 1U))) break;
    }
    if (format != 1 || channels == 0 || channels > 32 || sampleRate == 0
        || (bits != 8 && bits != 16 && bits != 24 && bits != 32)
        || blockAlign != channels * (bits / 8) || dataOffset < 0) {
        result.error = error(ErrorCode::Unsupported,
            QStringLiteral("Waveform requests require integer PCM WAV with 8, 16, 24 or 32 bits"));
        return result;
    }
    const qint64 frames = dataBytes / blockAlign;
    if (frames <= 0) {
        result.error = error(ErrorCode::Validation, QStringLiteral("WAV contains no complete sample frames"));
        return result;
    }
    int columns = request.columns > 0 ? request.columns : request.size.width();
    columns = std::clamp(columns > 0 ? columns : 256, 1, MaxPeakColumns);
    columns = int(std::min<qint64>(columns, frames));
    QVector<double> peak(columns, 0.0);
    const int bytesPerSample = bits / 8;
    const quint64 totalFrames = quint64(frames);
    constexpr qint64 ChunkBytes = 256 * 1024;
    if (!file.seek(dataOffset)) {
        result.error = error(ErrorCode::BackendFailure, QStringLiteral("Unable to seek to WAV data"));
        return result;
    }
    qint64 consumed = 0;
    quint64 frame = 0;
    while (consumed < qint64(dataBytes)) {
        if (cancelled(token, &result)) return result;
        qint64 want = std::min(ChunkBytes, qint64(dataBytes) - consumed);
        if (consumed + want < qint64(dataBytes))
            want -= want % blockAlign;
        QByteArray chunk = file.read(want);
        if (chunk.isEmpty()) {
            result.error = error(ErrorCode::BackendFailure, QStringLiteral("Short read in WAV data"));
            return result;
        }
        const qsizetype complete = chunk.size() - (chunk.size() % blockAlign);
        for (qsizetype offset = 0; offset < complete; offset += blockAlign, ++frame) {
            const int column = int((frame * quint64(columns)) / totalFrames);
            double magnitude = 0.0;
            for (quint16 channel = 0; channel < channels; ++channel) {
                const qsizetype at = offset + channel * bytesPerSample;
                double sample = 0.0;
                if (bits == 8) sample = (int(quint8(chunk.at(at))) - 128) / 128.0;
                else if (bits == 16) sample = qint16(u16(chunk, at)) / 32768.0;
                else if (bits == 24) {
                    qint32 value = qint32(quint8(chunk.at(at)))
                        | (qint32(quint8(chunk.at(at + 1))) << 8)
                        | (qint32(quint8(chunk.at(at + 2))) << 16);
                    if (value & 0x800000) value |= qint32(0xff000000);
                    sample = value / 8388608.0;
                } else sample = qint32(u32(chunk, at)) / 2147483648.0;
                magnitude = std::max(magnitude, std::abs(sample));
            }
            peak[column] = std::max(peak[column], std::min(1.0, magnitude));
        }
        consumed += chunk.size();
        if (progress) progress(double(consumed) / double(dataBytes));
    }
    result.peaks.reserve(columns);
    for (double value : peak) result.peaks.push_back(value);
    result.metadata = {{QStringLiteral("kind"), QStringLiteral("waveform")},
        {QStringLiteral("channels"), channels}, {QStringLiteral("sampleRate"), sampleRate},
        {QStringLiteral("bitsPerSample"), bits}, {QStringLiteral("frames"), QVariant::fromValue<qlonglong>(frames)},
        {QStringLiteral("durationSeconds"), double(frames) / sampleRate},
        {QStringLiteral("columns"), columns}};
    if (progress) progress(1.0);
    return result;
}

AssetResult imageAsset(const QString &path, const AssetRequest &request,
                       const CancellationToken &token, const JobProgress &progress)
{
    AssetResult result;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() > MaxDimension
        || sourceSize.height() > MaxDimension
        || qint64(sourceSize.width()) * sourceSize.height() > MaxImagePixels) {
        result.error = error(ErrorCode::Unsupported, QStringLiteral("Unsupported or oversized image"));
        return result;
    }
    const QRect sourceBounds(QPoint(0, 0), sourceSize);
    QRect region = request.region.isValid() && !request.region.isEmpty()
        ? request.region.intersected(sourceBounds) : sourceBounds;
    if (region.isEmpty()) {
        result.error = error(ErrorCode::InvalidArgument, QStringLiteral("Image region is outside the source"), QStringLiteral("region"));
        return result;
    }
    int width = request.size.width() > 0 ? request.size.width() : 256;
    int height = request.size.height() > 0 ? request.size.height() : 256;
    width = std::clamp(width, 1, MaxDimension);
    height = std::clamp(height, 1, MaxDimension);
    QSize target = region.size();
    target.scale(width, height, Qt::KeepAspectRatio);
    if (qint64(target.width()) * target.height() > MaxOutputPixels) {
        target.scale(4096, 4096, Qt::KeepAspectRatio);
    }
    if (cancelled(token, &result)) return result;
    reader.setClipRect(region);
    reader.setScaledSize(target);
    QImage image = reader.read();
    if (image.isNull()) {
        result.error = error(ErrorCode::Unsupported, reader.errorString(), QStringLiteral("uri"));
        return result;
    }
    if (cancelled(token, &result)) return result;
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)) {
        result.error = error(ErrorCode::BackendFailure, QStringLiteral("Unable to allocate thumbnail encoder"));
        return result;
    }
    QImageWriter writer(&buffer, "png");
    writer.setQuality(std::clamp(request.quality, 0, 100));
    if (!writer.write(image)) {
        result.error = error(ErrorCode::BackendFailure, writer.errorString());
        return result;
    }
    result.bytes = std::move(encoded);
    result.mimeType = QStringLiteral("image/png");
    result.metadata = {{QStringLiteral("kind"), QStringLiteral("thumbnail")},
        {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()},
        {QStringLiteral("sourceWidth"), sourceSize.width()}, {QStringLiteral("sourceHeight"), sourceSize.height()},
        {QStringLiteral("format"), QString::fromLatin1(reader.format())},
        {QStringLiteral("quality"), std::clamp(request.quality, 0, 100)}};
    if (progress) progress(1.0);
    return result;
}
} // namespace

AssetResult FileAssetBackend::fetch(const AssetRequest &request, const CancellationToken &cancel,
                                    const JobProgress &progress)
{
    AssetResult result;
    if (cancelled(cancel, &result)) return result;
    QString path;
    if (!localPath(request.uri, &path)) {
        result.error = error(ErrorCode::Unsupported,
            QStringLiteral("File asset backend accepts local paths and file URIs"), QStringLiteral("uri"));
        return result;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        result.error = error(ErrorCode::Unavailable, QStringLiteral("Asset is missing or unreadable"), QStringLiteral("uri"));
        return result;
    }
    if (info.size() > MaxAssetBytes) {
        result.error = error(ErrorCode::Validation, QStringLiteral("Asset exceeds the 256 MiB read limit"), QStringLiteral("uri"));
        return result;
    }
    const QString kind = request.kind.trimmed().toLower();
    if (kind == QStringLiteral("filmstrip") || kind == QStringLiteral("frame")
        || kind == QStringLiteral("video")) {
        result.error = error(ErrorCode::Unsupported,
            QStringLiteral("Video decoding and filmstrip extraction are not available in the file backend"));
        return result;
    }
    if (kind == QStringLiteral("waveform")) {
        if (progress) progress(0.0);
        return readWave(path, request, cancel, progress);
    }
    if (kind == QStringLiteral("thumbnail")) {
        if (progress) progress(0.0);
        return imageAsset(path, request, cancel, progress);
    }
    if (kind != QStringLiteral("metadata")) {
        result.error = error(ErrorCode::Unsupported, QStringLiteral("Unknown asset request kind"), QStringLiteral("kind"));
        return result;
    }
    result.metadata = {{QStringLiteral("kind"), QStringLiteral("metadata")},
        {QStringLiteral("fileName"), info.fileName()}, {QStringLiteral("fileSize"), info.size()},
        {QStringLiteral("suffix"), info.suffix().toLower()},
        {QStringLiteral("lastModified"), info.lastModified().toUTC().toString(Qt::ISODateWithMs)}};
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isValid()) {
        result.metadata.insert(QStringLiteral("mediaType"), QStringLiteral("image"));
        result.metadata.insert(QStringLiteral("width"), size.width());
        result.metadata.insert(QStringLiteral("height"), size.height());
        result.metadata.insert(QStringLiteral("format"), QString::fromLatin1(reader.format()));
    } else if (info.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
        AssetRequest waveform = request;
        waveform.kind = QStringLiteral("waveform");
        AssetResult parsed = readWave(path, waveform, cancel, progress);
        if (parsed.error) return parsed;
        for (auto it = parsed.metadata.cbegin(); it != parsed.metadata.cend(); ++it)
            result.metadata.insert(it.key(), it.value());
        result.metadata.insert(QStringLiteral("kind"), QStringLiteral("metadata"));
        result.metadata.insert(QStringLiteral("mediaType"), QStringLiteral("audio"));
    } else {
        result.metadata.insert(QStringLiteral("mediaType"), QStringLiteral("file"));
    }
    if (progress) progress(1.0);
    return result;
}
} // namespace QindaTK
