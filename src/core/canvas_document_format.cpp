// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document_format.h"
#include "canvas_transform.h"
#include "color_profile.h"
#include <QCryptographicHash>
#include <QColorSpace>
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK::CanvasDocumentFormat {
namespace {
constexpr qsizetype digestBytes = 32;
constexpr qsizetype headerBytes = SignatureBytes + 4 + 16 + 8 + 8;
constexpr qsizetype colorBytes = 11; // QDataStream: spec + alpha + four channels
constexpr qsizetype minimumLayerRecord = 8 + 4 + 4 + 1 + 1;
constexpr qsizetype minimumImageRecord = 8 + 4 + 4 + 4 + 4;
constexpr qsizetype minimumTileRecord = 4 + 4 + 4;
constexpr qsizetype minimumObjectRecordV1 = 8 + 8 + 1 + 32 + 4 + 72 + 2 * colorBytes + 8 + 8 + 8 + 1 + 1 + 4;
constexpr qsizetype minimumObjectRecordV2 = minimumObjectRecordV1 + 4;
constexpr qsizetype minimumObjectRecord = minimumObjectRecordV2 + 12;
// Keeps (order << 40) in CanvasDocumentAdapter::paintKey inside 63 bits.
constexpr quint64 idCeiling = IdCeiling;

OperationError problem(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, QStringLiteral("canvas.format")};
}

void configure(QDataStream &stream)
{
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
}

bool validColor(const QColor &color)
{
    const int spec = int(color.spec());
    if (spec < int(QColor::Invalid) || spec > int(QColor::ExtendedRgb)) return false;
    if (spec == int(QColor::Invalid)) return true;
    float r = 0, g = 0, b = 0, a = 0;
    color.getRgbF(&r, &g, &b, &a);
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}

template <typename Key, typename Value>
QList<Key> sortedKeys(const QHash<Key, Value> &hash)
{
    QList<Key> keys = hash.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

class Writer {
public:
    Writer() : m_stream(&m_bytes, QIODevice::WriteOnly) { configure(m_stream); }
    QDataStream &out() { return m_stream; }
    void raw(const char *data, qint64 size) { m_stream.writeRawData(data, size); }
    void bytes(const QByteArray &value)
    {
        m_stream << quint32(value.size());
        raw(value.constData(), value.size());
    }
    void text(const QString &value)
    {
        QByteArray units(value.size() * 2, Qt::Uninitialized);
        qToBigEndian<quint16>(value.utf16(), value.size(), units.data());
        m_stream << quint32(value.size());
        raw(units.constData(), units.size());
    }
    void rect(const QRectF &r) { m_stream << double(r.x()) << double(r.y()) << double(r.width()) << double(r.height()); }
    void transform(const QTransform &t)
    {
        m_stream << double(t.m11()) << double(t.m12()) << double(t.m13()) << double(t.m21())
                 << double(t.m22()) << double(t.m23()) << double(t.m31()) << double(t.m32())
                 << double(t.m33());
    }
    bool ok() const { return m_stream.status() == QDataStream::Ok; }
    QByteArray finish()
    {
        const QByteArray digest = QCryptographicHash::hash(m_bytes, QCryptographicHash::Sha256);
        return m_bytes + digest;
    }
private:
    QByteArray m_bytes;
    QDataStream m_stream;
};

class Reader {
public:
    explicit Reader(const QByteArray &bytes) : m_bytes(bytes), m_stream(m_bytes) { configure(m_stream); }
    qint64 remaining() const { return m_stream.device()->bytesAvailable(); }
    bool ok() const { return m_stream.status() == QDataStream::Ok; }
    template <typename T> bool read(T &value)
    {
        m_stream >> value;
        return ok();
    }
    bool raw(char *out, qint64 size) { return size <= remaining() && m_stream.readRawData(out, size) == size; }
    bool bytes(QByteArray &out, qint64 maximum)
    {
        quint32 size = 0;
        if (!read(size) || size > maximum || size > remaining()) return false;
        out.resize(size);
        return raw(out.data(), size);
    }
    bool text(QString &out, qsizetype maximumChars)
    {
        quint32 units = 0;
        if (!read(units) || units > maximumChars || qint64(units) * 2 > remaining()) return false;
        QByteArray raw(qsizetype(units) * 2, Qt::Uninitialized);
        if (!this->raw(raw.data(), raw.size())) return false;
        out = QString(qsizetype(units), Qt::Uninitialized);
        qFromBigEndian<quint16>(raw.constData(), units, out.data());
        return true;
    }
    bool rect(QRectF &r)
    {
        double x = 0, y = 0, w = 0, h = 0;
        if (!read(x) || !read(y) || !read(w) || !read(h)) return false;
        r = QRectF(x, y, w, h);
        return true;
    }
    bool transform(QTransform &t)
    {
        double m[9] = {};
        for (double &value : m)
            if (!read(value)) return false;
        t = QTransform(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        return true;
    }
    bool flag(bool &value)
    {
        quint8 stored = 0;
        if (!read(stored) || stored > 1) return false;
        value = stored == 1;
        return true;
    }
    bool color(QColor &value) { return read(value) && validColor(value); }
private:
    QByteArray m_bytes;
    QDataStream m_stream;
};
} // namespace

bool recognizes(const QByteArray &prefix)
{
    return prefix.startsWith(QByteArrayView(Signature, SignatureBytes));
}

qsizetype objectRecordBytes(const CanvasObject &object)
{
    return minimumObjectRecord + qsizetype(object.points.size()) * 16
        + qsizetype(object.name.size()) * 2 + object.managedFill.size()
        + qsizetype(object.pathNodes.size()) * 48 + qsizetype(object.dashPattern.size()) * 8;
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


std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error)
{
    if (error) *error = {};
    const auto fail = [error](const OperationError &e) -> std::optional<SnapshotHeader> {
        if (error) *error = e;
        return std::nullopt;
    };
    if (bytes.size() < headerBytes + digestBytes || !recognizes(bytes))
        return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Not a canvas document")));
    Reader in(bytes.first(headerBytes));
    char signature[SignatureBytes] = {};
    quint32 version = 0;
    char uuid[16] = {};
    quint64 revision = 0, generation = 0;
    if (!in.raw(signature, SignatureBytes) || !in.read(version))
        return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Truncated canvas header")));
    if (version < 1 || version > Version)
        return fail(problem(ErrorCode::Unsupported, QStringLiteral("Unsupported canvas format version"), QStringLiteral("version")));
    if (!in.raw(uuid, 16) || !in.read(revision) || !in.read(generation))
        return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Truncated canvas header")));
    const QUuid id = QUuid::fromRfc4122(QByteArrayView(uuid, 16));
    if (id.isNull())
        return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Invalid canvas document identity"), QStringLiteral("documentId")));
    return SnapshotHeader{QString::fromLatin1(SchemaId), DocumentId{id}, {revision}, generation};
}

QByteArray encode(const CanvasDocumentState &state, OperationError *error)
{
    if (error) *error = {};
    Writer w;
    w.raw(Signature, SignatureBytes);
    w.out() << Version;
    const QByteArray uuid = state.documentId.value.toRfc4122();
    w.raw(uuid.constData(), uuid.size());
    w.out() << quint64(state.revision) << quint64(state.resetGeneration) << quint64(state.nextObject)
            << quint64(state.nextLayer) << quint64(state.nextImage) << qint64(state.nextZ);
    w.rect(state.pageRect);
    w.out() << state.pageColor;
    w.bytes(state.outputProfileIcc);
    w.bytes(QJsonDocument(state.extensions).toJson(QJsonDocument::Compact));
    const auto layerIds = sortedKeys(state.layers);
    w.out() << quint32(layerIds.size());
    for (const CanvasLayerId id : layerIds) {
        const auto found = state.layers.constFind(id);
        const CanvasLayer &layer = *found;
        w.out() << quint64(layer.id);
        w.text(layer.name);
        w.out() << qint32(layer.order) << quint8(layer.visible) << quint8(layer.locked);
    }
    const auto imageIds = sortedKeys(state.images);
    w.out() << quint32(imageIds.size());
    for (const CanvasImageId id : imageIds) {
        const CanvasTiledImage &image = *state.images.constFind(id).value();
        w.out() << quint64(image.id) << qint32(image.size.width()) << qint32(image.size.height())
                << qint32(image.tileSize) << quint32(image.tiles.size());
        for (const QImage &source : image.tiles) {
            const QImage tile = source.format() == QImage::Format_ARGB32_Premultiplied
                ? source : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const qint64 row = qint64(tile.width()) * 4;
            w.out() << qint32(tile.width()) << qint32(tile.height()) << quint32(row * tile.height());
            for (int y = 0; y < tile.height(); ++y)
                w.raw(reinterpret_cast<const char *>(tile.constScanLine(y)), row);
        }
    }
    const auto objectIds = sortedKeys(state.objects);
    w.out() << quint32(objectIds.size());
    for (const CanvasObjectId id : objectIds) {
        const auto found = state.objects.constFind(id);
        const CanvasObject &o = *found;
        w.out() << quint64(o.id) << quint64(o.layer) << quint8(o.shape);
        w.rect(o.rect);
        w.out() << quint32(o.points.size());
        for (const QPointF &point : o.points) w.out() << double(point.x()) << double(point.y());
        w.transform(o.transform);
        w.out() << o.fill << o.stroke << double(o.strokeWidth) << quint64(o.image) << qint64(o.z)
                << quint8(o.visible) << quint8(o.locked);
        w.text(o.name);
        w.bytes(o.managedFill);
        w.out() << quint32(o.pathNodes.size());
        for (const auto &node : o.pathNodes)
            w.out() << double(node.anchor.x()) << double(node.anchor.y())
                    << double(node.in.x()) << double(node.in.y())
                    << double(node.out.x()) << double(node.out.y());
        w.out() << quint8(o.pathClosed) << quint8(o.fillRule)
                << quint8(o.strokeCap) << quint8(o.strokeJoin)
                << quint32(o.dashPattern.size());
        for (qreal dash : o.dashPattern) w.out() << double(dash);
    }
    if (!w.ok()) {
        if (error) *error = problem(ErrorCode::BackendFailure, QStringLiteral("Canvas encoding failed"));
        return {};
    }
    return w.finish();
}

std::optional<CanvasDocumentState> decode(const QByteArray &bytes, const CanvasDocumentLimits &limits,
                                          OperationError *error)
{
    if (error) *error = {};
    const auto fail = [error](const OperationError &e) -> std::optional<CanvasDocumentState> {
        if (error) *error = e;
        return std::nullopt;
    };
    const auto truncated = [&] {
        return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Truncated or malformed canvas document")));
    };
    if (bytes.size() > limits.maximumEncodedBytes)
        return fail(problem(ErrorCode::Unsupported, QStringLiteral("Canvas document exceeds the size limit")));
    OperationError headerError;
    const auto header = inspect(bytes, &headerError);
    if (!header) return fail(headerError);
    const qsizetype body = bytes.size() - digestBytes;
    const quint32 version = qFromBigEndian<quint32>(bytes.constData() + SignatureBytes);
    const QByteArray digest = QCryptographicHash::hash(QByteArrayView(bytes.constData(), body),
                                                       QCryptographicHash::Sha256);
    if (QByteArrayView(digest) != QByteArrayView(bytes.constData() + body, digestBytes))
        return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Canvas document checksum mismatch")));
    Reader in(QByteArray::fromRawData(bytes.constData(), body));
    char skipped[headerBytes];
    CanvasDocumentState state;
    quint64 nextObject = 0, nextLayer = 0, nextImage = 0;
    qint64 nextZ = 0;
    if (!in.raw(skipped, headerBytes) || !in.read(nextObject) || !in.read(nextLayer)
        || !in.read(nextImage) || !in.read(nextZ) || !in.rect(state.pageRect) || !in.color(state.pageColor))
        return truncated();
    if (nextObject < 1 || nextObject > idCeiling || nextLayer < 2 || nextLayer > idCeiling
        || nextImage < 1 || nextImage > idCeiling || nextZ > PaintOrderCeiling)
        return fail(problem(ErrorCode::Validation, QStringLiteral("Canvas identity counters are out of range"), QStringLiteral("counters")));
    if (const auto e = validatePage(state.pageRect, state.pageColor)) return fail(e);
    if (version >= 2) {
        if (!in.bytes(state.outputProfileIcc, 16 * 1024 * 1024)) return truncated();
        if (!state.outputProfileIcc.isEmpty()) {
            OperationError profileError;
            const ColorProfile profile = ColorProfile::fromIcc(state.outputProfileIcc, &profileError);
            const QColorSpace space = QColorSpace::fromIccProfile(state.outputProfileIcc);
            if (!profile.isValid() || !space.isValid() || !space.isValidTarget()
                || space.colorModel() != QColorSpace::ColorModel::Rgb)
                return fail(problem(ErrorCode::Validation, QStringLiteral("Invalid output ICC profile"),
                                    QStringLiteral("outputProfileIcc")));
        }
    }
    QByteArray extensionBytes;
    if (!in.bytes(extensionBytes, limits.maximumExtensionBytes)) return truncated();
    QJsonParseError parse{};
    const QJsonDocument extensions = QJsonDocument::fromJson(extensionBytes, &parse);
    if (parse.error != QJsonParseError::NoError || !extensions.isObject())
        return fail(problem(ErrorCode::Validation, QStringLiteral("Invalid canvas extensions"), QStringLiteral("extensions")));
    state.extensions = extensions.object();

    quint32 layerCount = 0;
    if (!in.read(layerCount) || qint64(layerCount) * minimumLayerRecord > in.remaining()) return truncated();
    if (layerCount < 1 || layerCount > limits.maximumLayers)
        return fail(problem(ErrorCode::Unsupported, QStringLiteral("Invalid canvas layer count"), QStringLiteral("layers")));
    state.layers.reserve(layerCount);
    for (quint32 i = 0; i < layerCount; ++i) {
        CanvasLayer layer;
        quint64 id = 0;
        qint32 order = 0;
        if (!in.read(id) || !in.text(layer.name, limits.maximumNameLength) || !in.read(order)
            || !in.flag(layer.visible) || !in.flag(layer.locked))
            return truncated();
        layer.id = id;
        layer.order = order;
        if (id >= nextLayer || state.layers.contains(id))
            return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Duplicate or out-of-range layer id"), QStringLiteral("layers.id")));
        if (const auto e = validateLayer(layer, id, limits)) return fail(e);
        state.layers.insert(id, layer);
    }

    quint32 imageCount = 0;
    if (!in.read(imageCount) || qint64(imageCount) * minimumImageRecord > in.remaining()) return truncated();
    if (imageCount > limits.maximumImages)
        return fail(problem(ErrorCode::Unsupported, QStringLiteral("Too many canvas images"), QStringLiteral("images")));
    qint64 decodedImageBytes = 0;
    for (quint32 i = 0; i < imageCount; ++i) {
        quint64 id = 0;
        qint32 width = 0, height = 0, tileSize = 0;
        quint32 tileCount = 0;
        if (!in.read(id) || !in.read(width) || !in.read(height) || !in.read(tileSize) || !in.read(tileCount))
            return truncated();
        if (id == 0 || id >= nextImage || state.images.contains(id))
            return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Duplicate or out-of-range image id"), QStringLiteral("images.id")));
        if (width < 1 || height < 1 || width > limits.maximumImageSide || height > limits.maximumImageSide)
            return fail(problem(ErrorCode::Unsupported, QStringLiteral("Image dimensions exceed the limit"), QStringLiteral("images.size")));
        if (tileSize < limits.minimumTileSize || tileSize > limits.maximumTileSize)
            return fail(problem(ErrorCode::Validation, QStringLiteral("Invalid image tile size"), QStringLiteral("images.tileSize")));
        // AGENT-GUARD: charge the declared pixels before the tile list is
        // reserved; tiles are at least their clipped size, so this is a floor.
        if (decodedImageBytes + qint64(width) * height * 4 > limits.maximumImageBytes)
            return fail(problem(ErrorCode::Unsupported, QStringLiteral("Decoded images exceed the size limit"), QStringLiteral("images")));
        CanvasTiledImage image;
        image.id = id;
        image.size = QSize(width, height);
        image.tileSize = tileSize;
        if (qint64(tileCount) != qint64(image.columns()) * image.rows())
            return fail(problem(ErrorCode::Validation, QStringLiteral("Image tile grid is incomplete"), QStringLiteral("images.tiles")));
        if (qint64(tileCount) * minimumTileRecord > in.remaining()) return truncated();
        image.tiles.reserve(tileCount);
        for (int row = 0; row < image.rows(); ++row) {
            for (int column = 0; column < image.columns(); ++column) {
                qint32 tileWidth = 0, tileHeight = 0;
                quint32 length = 0;
                if (!in.read(tileWidth) || !in.read(tileHeight) || !in.read(length)) return truncated();
                const QRect expected = image.tileRect(column, row);
                if (tileWidth < expected.width() || tileWidth > tileSize
                    || tileHeight < expected.height() || tileHeight > tileSize)
                    return fail(problem(ErrorCode::Validation, QStringLiteral("Invalid image tile size"), QStringLiteral("images.tiles")));
                const qint64 rowBytes = qint64(tileWidth) * 4;
                // AGENT-GUARD: charge the decoded total before allocating a
                // tile, so a small file cannot request unbounded pixels.
                decodedImageBytes += rowBytes * tileHeight;
                if (decodedImageBytes > limits.maximumImageBytes)
                    return fail(problem(ErrorCode::Unsupported, QStringLiteral("Decoded images exceed the size limit"), QStringLiteral("images")));
                if (qint64(length) != rowBytes * tileHeight)
                    return fail(problem(ErrorCode::Validation, QStringLiteral("Image tile data size mismatch"), QStringLiteral("images.tiles")));
                if (qint64(length) > in.remaining()) return truncated();
                QImage tile(tileWidth, tileHeight, QImage::Format_ARGB32_Premultiplied);
                if (tile.isNull())
                    return fail(problem(ErrorCode::BackendFailure, QStringLiteral("Cannot allocate an image tile"), QStringLiteral("images.tiles")));
                for (int y = 0; y < tileHeight; ++y)
                    if (!in.raw(reinterpret_cast<char *>(tile.scanLine(y)), rowBytes)) return truncated();
                image.tiles.append(tile);
            }
        }
        state.images.insert(id, std::make_shared<const CanvasTiledImage>(std::move(image)));
    }

    quint32 objectCount = 0;
    if (!in.read(objectCount) || qint64(objectCount)
        * (version >= 3 ? minimumObjectRecord : version == 2 ? minimumObjectRecordV2
                        : minimumObjectRecordV1) > in.remaining()) return truncated();
    if (objectCount > limits.maximumObjects)
        return fail(problem(ErrorCode::Unsupported, QStringLiteral("Too many canvas objects"), QStringLiteral("objects")));
    state.objects.reserve(objectCount);
    qint64 totalPoints = 0;
    for (quint32 i = 0; i < objectCount; ++i) {
        CanvasObject o;
        quint64 id = 0, layer = 0, image = 0;
        quint8 shape = 0;
        quint32 pointCount = 0;
        qint64 z = 0;
        double strokeWidth = 0;
        if (!in.read(id) || !in.read(layer) || !in.read(shape) || !in.rect(o.rect) || !in.read(pointCount))
            return truncated();
        totalPoints += pointCount;
        if (pointCount > limits.maximumPointsPerObject || totalPoints > limits.maximumTotalPoints)
            return fail(problem(ErrorCode::Unsupported, QStringLiteral("Canvas point count exceeds the limit"), QStringLiteral("objects.points")));
        if (qint64(pointCount) * 16 > in.remaining()) return truncated();
        o.points.reserve(pointCount);
        for (quint32 p = 0; p < pointCount; ++p) {
            double x = 0, y = 0;
            if (!in.read(x) || !in.read(y)) return truncated();
            o.points.append(QPointF(x, y));
        }
        if (!in.transform(o.transform) || !in.color(o.fill) || !in.color(o.stroke) || !in.read(strokeWidth)
            || !in.read(image) || !in.read(z) || !in.flag(o.visible) || !in.flag(o.locked)
            || !in.text(o.name, limits.maximumNameLength))
            return truncated();
        if (version >= 2 && !in.bytes(o.managedFill, 24 * 1024 * 1024)) return truncated();
        if (version >= 3) {
            quint32 nodeCount = 0, dashCount = 0;
            quint8 closed = 0, rule = 0, cap = 0, join = 0;
            if (!in.read(nodeCount) || nodeCount > limits.maximumPointsPerObject
                || totalPoints + nodeCount > limits.maximumTotalPoints
                || qint64(nodeCount) * 48 > in.remaining()) return truncated();
            totalPoints += nodeCount;
            o.pathNodes.reserve(nodeCount);
            for (quint32 n = 0; n < nodeCount; ++n) {
                double ax, ay, ix, iy, ox, oy;
                if (!in.read(ax) || !in.read(ay) || !in.read(ix) || !in.read(iy)
                    || !in.read(ox) || !in.read(oy)) return truncated();
                o.pathNodes.append({{ax, ay}, {ix, iy}, {ox, oy}});
            }
            if (!in.read(closed) || !in.read(rule) || !in.read(cap) || !in.read(join)
                || !in.read(dashCount) || dashCount > 4096
                || qint64(dashCount) * 8 > in.remaining()) return truncated();
            o.pathClosed = closed;
            o.fillRule = Qt::FillRule(rule);
            o.strokeCap = Qt::PenCapStyle(cap);
            o.strokeJoin = Qt::PenJoinStyle(join);
            o.dashPattern.reserve(dashCount);
            for (quint32 d = 0; d < dashCount; ++d) {
                double length;
                if (!in.read(length)) return truncated();
                o.dashPattern.append(length);
            }
        }
        o.id = id;
        o.layer = layer;
        o.shape = CanvasShape(shape);
        o.strokeWidth = strokeWidth;
        o.image = image;
        o.z = z;
        if (id >= nextObject || state.objects.contains(id))
            return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Duplicate or out-of-range object id"), QStringLiteral("objects.id")));
        if (const auto e = validateObject(o, id, limits)) return fail(e);
        if (!state.layers.contains(layer))
            return fail(problem(ErrorCode::Validation, QStringLiteral("Object references a missing layer"), QStringLiteral("objects.layer")));
        if (o.shape == CanvasShape::Image && !state.images.contains(image))
            return fail(problem(ErrorCode::Validation, QStringLiteral("Image object references a missing image"), QStringLiteral("objects.image")));
        if (z >= nextZ)
            return fail(problem(ErrorCode::Validation, QStringLiteral("Object paint order is beyond the document counter"), QStringLiteral("objects.z")));
        state.objects.insert(id, o);
    }
    if (!in.ok() || in.remaining() != 0)
        return fail(problem(ErrorCode::InvalidArgument, QStringLiteral("Unexpected bytes after canvas objects")));
    state.documentId = header->documentId;
    state.revision = header->revision.value;
    state.resetGeneration = header->resetGeneration;
    state.nextObject = nextObject;
    state.nextLayer = nextLayer;
    state.nextImage = nextImage;
    state.nextZ = nextZ;
    return state;
}

} // namespace QindaTK::CanvasDocumentFormat
