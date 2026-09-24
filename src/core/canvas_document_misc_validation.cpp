// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document_format.h"
#include <cmath>

namespace QindaTK::CanvasDocumentFormat {
namespace {
constexpr int layerOrderBound = 1 << 22;
constexpr quint64 idCeiling = IdCeiling;
OperationError problem(ErrorCode code, const QString &message, const QString &field)
{ return {code, message, field, QStringLiteral("canvas.format")}; }
bool finite(const QRectF &r)
{ return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) && std::isfinite(r.height()); }
bool validColor(const QColor &color)
{
    const int spec = int(color.spec());
    if (spec < int(QColor::Invalid) || spec > int(QColor::ExtendedRgb)) return false;
    if (spec == int(QColor::Invalid)) return true;
    float r = 0, g = 0, b = 0, a = 0;
    color.getRgbF(&r, &g, &b, &a);
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}
}
OperationError validateLayer(const CanvasLayer &layer, CanvasLayerId id, const CanvasDocumentLimits &limits)
{
    if (id == 0 || id >= idCeiling || layer.id != id)
        return problem(ErrorCode::InvalidArgument, QStringLiteral("Invalid layer identity"), QStringLiteral("layers.id"));
    if (layer.name.size() > limits.maximumNameLength)
        return problem(ErrorCode::Unsupported, QStringLiteral("Layer name is too long"), QStringLiteral("layers.name"));
    if (layer.order <= -layerOrderBound || layer.order >= layerOrderBound)
        return problem(ErrorCode::Validation, QStringLiteral("Layer order is out of range"), QStringLiteral("layers.order"));
    return {};
}

OperationError validateImage(const CanvasTiledImage &image, CanvasImageId id, const CanvasDocumentLimits &limits)
{
    if (id == 0 || id >= idCeiling || image.id != id)
        return problem(ErrorCode::InvalidArgument, QStringLiteral("Invalid image identity"), QStringLiteral("images.id"));
    if (image.size.width() < 1 || image.size.height() < 1
        || image.size.width() > limits.maximumImageSide || image.size.height() > limits.maximumImageSide)
        return problem(ErrorCode::Unsupported, QStringLiteral("Image dimensions exceed the limit"), QStringLiteral("images.size"));
    if (image.tileSize < limits.minimumTileSize || image.tileSize > limits.maximumTileSize)
        return problem(ErrorCode::Validation, QStringLiteral("Invalid image tile size"), QStringLiteral("images.tileSize"));
    if (qint64(image.tiles.size()) != qint64(image.columns()) * image.rows())
        return problem(ErrorCode::Validation, QStringLiteral("Image tile grid is incomplete"), QStringLiteral("images.tiles"));
    for (int row = 0; row < image.rows(); ++row) {
        for (int column = 0; column < image.columns(); ++column) {
            const QRect expected = image.tileRect(column, row);
            const QImage &tile = image.tiles.at(row * image.columns() + column);
            if (tile.isNull() || tile.format() != QImage::Format_ARGB32_Premultiplied
                || tile.width() < expected.width() || tile.width() > image.tileSize
                || tile.height() < expected.height() || tile.height() > image.tileSize)
                return problem(ErrorCode::Validation, QStringLiteral("Invalid image tile"), QStringLiteral("images.tiles"));
        }
    }
    if (imageBytes(image) > limits.maximumImageBytes)
        return problem(ErrorCode::Unsupported, QStringLiteral("Image exceeds the decoded size limit"), QStringLiteral("images.size"));
    return {};
}

OperationError validatePage(const QRectF &rect, const QColor &color)
{
    if (!finite(rect) || !rect.isValid() || !color.isValid() || !validColor(color))
        return problem(ErrorCode::Validation, QStringLiteral("Invalid page rectangle or colour"), QStringLiteral("page"));
    return {};
}

} // namespace QindaTK::CanvasDocumentFormat
