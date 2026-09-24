// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document_format.h"
#include "canvas_appearance.h"
#include <QUuid>
#include <QtEndian>

namespace QindaTK::CanvasDocumentFormat {
namespace {
constexpr qsizetype minimumLayerRecord = 8 + 4 + 4 + 1 + 1;
constexpr qsizetype minimumImageRecord = 8 + 4 + 4 + 4 + 4;
constexpr qsizetype minimumTileRecord = 4 + 4 + 4;
constexpr qsizetype minimumObjectRecordV1 = 8 + 8 + 1 + 32 + 4 + 72 + 2 * 11 + 8 + 8 + 8 + 1 + 1 + 4;
constexpr qsizetype minimumObjectRecord = minimumObjectRecordV1 + 4 + 12 + 4 + 21 + 4;
}
bool recognizes(const QByteArray &prefix)
{
    return prefix.startsWith(QByteArrayView(Signature, SignatureBytes));
}

// The identity header sits at fixed offsets: signature, version, UUID,
// revision, reset generation; the digest follows the body.
std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error)
{
    if (error) *error = {};
    const auto fail = [error](ErrorCode code, const QString &message,
                              const QString &field = {}) -> std::optional<SnapshotHeader> {
        if (error) *error = {code, message, field, QStringLiteral("canvas.format")};
        return std::nullopt;
    };
    constexpr qsizetype headerBytes = SignatureBytes + 4 + 16 + 8 + 8;
    if (bytes.size() < headerBytes + 32 || !recognizes(bytes))
        return fail(ErrorCode::InvalidArgument, QStringLiteral("Not a canvas document"));
    const char *data = bytes.constData() + SignatureBytes;
    const quint32 version = qFromBigEndian<quint32>(data);
    if (version < 1 || version > Version)
        return fail(ErrorCode::Unsupported, QStringLiteral("Unsupported canvas format version"),
                    QStringLiteral("version"));
    const QUuid id = QUuid::fromRfc4122(QByteArrayView(data + 4, 16));
    if (id.isNull())
        return fail(ErrorCode::InvalidArgument, QStringLiteral("Invalid canvas document identity"),
                    QStringLiteral("documentId"));
    const quint64 revision = qFromBigEndian<quint64>(data + 20);
    const quint64 generation = qFromBigEndian<quint64>(data + 28);
    return SnapshotHeader{QString::fromLatin1(SchemaId), DocumentId{id}, {revision}, generation};
}

qsizetype objectRecordBytes(const CanvasObject &object)
{
    qsizetype bytes = minimumObjectRecord + qsizetype(object.points.size()) * 16
        + qsizetype(object.name.size()) * 2 + object.managedFill.size()
        + qsizetype(object.pathNodes.size()) * 48 + qsizetype(object.dashPattern.size()) * 8;
    for (const auto &contour : object.extraContours)
        bytes += 5 + qsizetype(contour.nodes.size()) * 48;
    if (!object.appearance.isDefault()) bytes += CanvasAppearanceCodec::encode(object.appearance).size();
    return bytes;
}

qsizetype layerRecordBytes(const CanvasLayer &layer)
{
    return minimumLayerRecord + qsizetype(layer.name.size()) * 2;
}

qsizetype imageRecordBytes(const CanvasTiledImage &image)
{
    return minimumImageRecord + qsizetype(image.tiles.size()) * minimumTileRecord + imageBytes(image);
}

qsizetype imageBytes(const CanvasTiledImage &image)
{
    qsizetype total = 0;
    for (const QImage &tile : image.tiles) total += qsizetype(tile.width()) * tile.height() * 4;
    return total;
}


} // namespace QindaTK::CanvasDocumentFormat
