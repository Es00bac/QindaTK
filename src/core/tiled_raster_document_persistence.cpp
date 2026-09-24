// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tiled_raster_document.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int TileSize = 256;
constexpr qsizetype MaximumFileBytes = 192 * 1024 * 1024;
constexpr qsizetype MaximumTileBytes = 128 * 1024 * 1024;
constexpr int MaximumLayers = 256;

bool integer(const QJsonValue &value, int minimum, int maximum, int *output)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum)
        return false;
    *output = int(number);
    return true;
}

QByteArray encodeTile(const QImage &image, bool mask)
{
    QByteArray raw;
    const int channels = mask ? 1 : 4;
    raw.resize(image.width() * image.height() * channels);
    qsizetype offset = 0;
    for (int y = 0; y < image.height(); ++y) {
        if (mask) {
            const uchar *row = image.constScanLine(y);
            for (int x = 0; x < image.width(); ++x) raw[offset++] = char(row[x]);
        } else {
            const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = row[x];
                raw[offset++] = char(qRed(pixel)); raw[offset++] = char(qGreen(pixel));
                raw[offset++] = char(qBlue(pixel)); raw[offset++] = char(qAlpha(pixel));
            }
        }
    }
    return raw;
}
}

bool TiledRasterDocument::saveToFile(const QString &path)
{
    const QString output = path.trimmed();
    if (m_strokeActive || output.isEmpty() || m_allocatedTileBytes > MaximumTileBytes
        || !QDir().mkpath(QFileInfo(output).absolutePath())) {
        setError(QStringLiteral("Saving needs an idle raster document and a writable path.")); return false;
    }
    QJsonArray layers;
    for (const Layer &layer : m_layers) {
        auto encodeTiles = [&](const QVector<QImage> &tiles, bool mask) {
            QJsonArray array;
            for (int index = 0; index < tiles.size(); ++index) {
                const QImage &tile = tiles.at(index);
                if (tile.isNull()) continue;
                const QByteArray raw = encodeTile(tile, mask);
                array.append(QJsonObject{{QStringLiteral("index"), index},
                                         {QStringLiteral("data"), QString::fromLatin1(raw.toBase64())}});
            }
            return array;
        };
        QJsonObject record{{QStringLiteral("id"), layer.id}, {QStringLiteral("name"), layer.name},
                           {QStringLiteral("visible"), layer.visible},
                           {QStringLiteral("hasMask"), layer.hasMask},
                           {QStringLiteral("maskEnabled"), layer.maskEnabled},
                           {QStringLiteral("pixels"), encodeTiles(layer.pixels, false)},
                           {QStringLiteral("mask"), layer.hasMask ? encodeTiles(layer.mask, true) : QJsonArray{}}};
        layers.append(record);
    }
    const QVariantMap preset = brushPreset();
    const QJsonObject brush{{QStringLiteral("diameter"), preset.value(QStringLiteral("diameter")).toDouble()},
                            {QStringLiteral("hardness"), preset.value(QStringLiteral("hardness")).toDouble()},
                            {QStringLiteral("spacing"), preset.value(QStringLiteral("spacing")).toDouble()},
                            {QStringLiteral("opacity"), preset.value(QStringLiteral("opacity")).toDouble()},
                            {QStringLiteral("pressureSize"), preset.value(QStringLiteral("pressureSize")).toDouble()},
                            {QStringLiteral("pressureOpacity"), preset.value(QStringLiteral("pressureOpacity")).toDouble()}};
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("qindatk.tiled-raster")},
                           {QStringLiteral("version"), 1}, {QStringLiteral("tileSize"), TileSize},
                           {QStringLiteral("width"), width()}, {QStringLiteral("height"), height()},
                           {QStringLiteral("brushPreset"), brush}, {QStringLiteral("layers"), layers}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaximumFileBytes) {
        setError(QStringLiteral("Raster document encoding exceeds the 192 MiB file bound.")); return false;
    }
    QSaveFile file(output); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Atomic raster document save failed: %1").arg(file.errorString())); return false;
    }
    m_error.clear(); emit changed(); return true;
}

bool TiledRasterDocument::loadFromFile(const QString &path)
{
    auto fail = [&](const QString &message) { setError(message); return false; };
    const QString input = path.trimmed();
    if (m_strokeActive || input.isEmpty()) return fail(QStringLiteral("Loading needs an idle raster document and a file path."));
    QFile file(input);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cannot open raster document: %1").arg(file.errorString()));
    if (file.size() <= 0 || file.size() > MaximumFileBytes)
        return fail(QStringLiteral("Raster document must be non-empty and no larger than 192 MiB."));
    const QByteArray bytes = file.read(MaximumFileBytes + 1);
    if (bytes.size() != file.size() || bytes.size() > MaximumFileBytes)
        return fail(QStringLiteral("Could not read the bounded raster document."));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("Invalid raster document JSON."));
    const QJsonObject root = document.object();
    int storedVersion = 0, storedTileSize = 0, storedWidth = 0, storedHeight = 0;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("qindatk.tiled-raster")
        || !integer(root.value(QStringLiteral("version")), 1, 1, &storedVersion)
        || !integer(root.value(QStringLiteral("tileSize")), TileSize, TileSize, &storedTileSize)
        || !integer(root.value(QStringLiteral("width")), 1, 8192, &storedWidth)
        || !integer(root.value(QStringLiteral("height")), 1, 8192, &storedHeight)
        || qint64(storedWidth) * storedHeight > 16 * 1024 * 1024)
        return fail(QStringLiteral("Unsupported raster format/version, tile size, or canvas dimensions."));
    Q_UNUSED(storedVersion);
    Q_UNUSED(storedTileSize);

    const QJsonObject brushObject = root.value(QStringLiteral("brushPreset")).toObject();
    const QStringList brushFields{QStringLiteral("diameter"), QStringLiteral("hardness"), QStringLiteral("spacing"),
                                  QStringLiteral("opacity"), QStringLiteral("pressureSize"), QStringLiteral("pressureOpacity")};
    QVariantMap brushMap;
    for (const QString &field : brushFields) {
        const QJsonValue value = brushObject.value(field);
        if (!value.isDouble() || !std::isfinite(value.toDouble())) return fail(QStringLiteral("Invalid brush preset field: %1").arg(field));
        brushMap.insert(field, value.toDouble());
    }
    Brush brush;
    if (!brushFromMap(brushMap, &brush)) return fail(QStringLiteral("Brush preset values exceed the supported finite ranges."));

    const int tileColumns = (storedWidth + TileSize - 1) / TileSize;
    const int tileRows = (storedHeight + TileSize - 1) / TileSize;
    const int tileCount = tileColumns * tileRows;
    const QJsonArray records = root.value(QStringLiteral("layers")).toArray();
    if (!root.value(QStringLiteral("layers")).isArray() || records.size() > MaximumLayers)
        return fail(QStringLiteral("Raster layer list is missing or exceeds the 256-layer bound."));
    QVector<Layer> layers;
    QSet<QString> layerIds;
    qsizetype allocatedBytes = 0;
    auto decodeTiles = [&](const QJsonValue &value, bool mask, QVector<QImage> *tiles) -> bool {
        if (!value.isArray()) return false;
        const QJsonArray array = value.toArray();
        QSet<int> seen;
        for (const QJsonValue &entryValue : array) {
            if (!entryValue.isObject()) return false;
            const QJsonObject entry = entryValue.toObject();
            int tileIndex = -1;
            if (!integer(entry.value(QStringLiteral("index")), 0, tileCount - 1, &tileIndex) || seen.contains(tileIndex)) return false;
            seen.insert(tileIndex);
            const int tileX = (tileIndex % tileColumns) * TileSize;
            const int tileY = (tileIndex / tileColumns) * TileSize;
            const int tileWidth = qMin(TileSize, storedWidth - tileX);
            const int tileHeight = qMin(TileSize, storedHeight - tileY);
            const qsizetype expectedBytes = qsizetype(tileWidth) * tileHeight * (mask ? 1 : 4);
            const qsizetype encodedLimit = ((expectedBytes + 2) / 3) * 4;
            const QString encodedString = entry.value(QStringLiteral("data")).toString();
            const QByteArray encoded = encodedString.toLatin1();
            if (!entry.value(QStringLiteral("data")).isString() || encoded.size() != encodedLimit) return false;
            const QByteArray raw = QByteArray::fromBase64(encoded);
            if (raw.size() != expectedBytes || raw.toBase64() != encoded
                || allocatedBytes + qsizetype(tileWidth) * tileHeight * (mask ? 1 : 4) > MaximumTileBytes) return false;
            const qsizetype tileBytes = qsizetype(tileWidth) * tileHeight * (mask ? 1 : 4);
            QImage tile(QSize(tileWidth, tileHeight), mask ? QImage::Format_Grayscale8 : QImage::Format_ARGB32_Premultiplied);
            if (tile.isNull()) return false;
            qsizetype offset = 0;
            for (int y = 0; y < tileHeight; ++y) {
                if (mask) {
                    uchar *row = tile.scanLine(y);
                    for (int x = 0; x < tileWidth; ++x) row[x] = uchar(quint8(raw.at(offset++)));
                } else {
                    QRgb *row = reinterpret_cast<QRgb *>(tile.scanLine(y));
                    for (int x = 0; x < tileWidth; ++x) {
                        const int red = quint8(raw.at(offset++)), green = quint8(raw.at(offset++));
                        const int blue = quint8(raw.at(offset++)), alpha = quint8(raw.at(offset++));
                        if (red > alpha || green > alpha || blue > alpha) return false;
                        row[x] = qRgba(red, green, blue, alpha);
                    }
                }
            }
            allocatedBytes += tileBytes;
            tiles->replace(tileIndex, tile);
        }
        return true;
    };

    for (const QJsonValue &recordValue : records) {
        if (!recordValue.isObject()) return fail(QStringLiteral("Invalid raster layer record."));
        const QJsonObject record = recordValue.toObject();
        Layer layer;
        layer.id = record.value(QStringLiteral("id")).toString();
        layer.name = record.value(QStringLiteral("name")).toString();
        if (layer.id.trimmed().isEmpty() || layer.name.trimmed().isEmpty() || layer.id.size() > 256
            || layer.name.size() > 256 || layerIds.contains(layer.id)
            || !record.value(QStringLiteral("visible")).isBool()
            || !record.value(QStringLiteral("hasMask")).isBool()
            || !record.value(QStringLiteral("maskEnabled")).isBool())
            return fail(QStringLiteral("Invalid or duplicate stable raster layer identity/flags."));
        layerIds.insert(layer.id);
        layer.visible = record.value(QStringLiteral("visible")).toBool();
        layer.hasMask = record.value(QStringLiteral("hasMask")).toBool();
        layer.maskEnabled = record.value(QStringLiteral("maskEnabled")).toBool();
        layer.pixels.resize(tileCount);
        if (!decodeTiles(record.value(QStringLiteral("pixels")), false, &layer.pixels))
            return fail(QStringLiteral("Invalid raster pixel tile payload."));
        if (!record.value(QStringLiteral("mask")).isArray())
            return fail(QStringLiteral("Layer mask payload must be an array."));
        if (!layer.hasMask && !record.value(QStringLiteral("mask")).toArray().isEmpty())
            return fail(QStringLiteral("Layer without a mask contains mask tiles."));
        if (layer.hasMask) {
            layer.mask.resize(tileCount);
            if (!decodeTiles(record.value(QStringLiteral("mask")), true, &layer.mask))
                return fail(QStringLiteral("Invalid raster mask tile payload."));
        }
        layers.append(std::move(layer));
    }

    m_size = QSize(storedWidth, storedHeight);
    m_layers = std::move(layers); m_brush = brush; m_allocatedTileBytes = allocatedBytes;
    m_history.clear(); m_cursor = 0; m_historyBytes = 0; ++m_revision;
    m_error.clear(); updatePreview(); return true;
}

} // namespace QindaTK
