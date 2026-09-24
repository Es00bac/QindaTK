// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document_format.h"

namespace QindaTK::CanvasDocumentFormat {
namespace {
constexpr qsizetype minimumLayerRecord = 8 + 4 + 4 + 1 + 1;
constexpr qsizetype minimumImageRecord = 8 + 4 + 4 + 4 + 4;
constexpr qsizetype minimumTileRecord = 4 + 4 + 4;
constexpr qsizetype minimumObjectRecordV1 = 8 + 8 + 1 + 32 + 4 + 72 + 2 * 11 + 8 + 8 + 8 + 1 + 1 + 4;
constexpr qsizetype minimumObjectRecord = minimumObjectRecordV1 + 4 + 12 + 4;
}
qsizetype objectRecordBytes(const CanvasObject &object)
{
    qsizetype bytes = minimumObjectRecord + qsizetype(object.points.size()) * 16
        + qsizetype(object.name.size()) * 2 + object.managedFill.size()
        + qsizetype(object.pathNodes.size()) * 48 + qsizetype(object.dashPattern.size()) * 8;
    for (const auto &contour : object.extraContours)
        bytes += 5 + qsizetype(contour.nodes.size()) * 48;
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
