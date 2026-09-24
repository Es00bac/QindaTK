// SPDX-License-Identifier: LGPL-3.0-or-later
// Raster outputs besides PNG and state files: deterministic brush previews
// and placement of the flattened composite on a CanvasDocument (D-085).
#include "tiled_raster_document.h"

#include "canvas_document.h"

#include <QColorSpace>
#include <cmath>
#include <memory>

namespace QindaTK {
namespace {
constexpr int CanvasTileSize = 256;

// The canvas convention is untagged sRGB-encoded ARGB32 premultiplied tiles
// (docs/canvas.md#object-model); the raster composite has exactly that
// encoding, so only the tag is dropped.
QImage untaggedComposite(QImage image)
{
    image.setColorSpace(QColorSpace());
    return image;
}

// AGENT-NOTE: a tile equal to the same tile of `previous` reuses that
// implicitly shared QImage, so a publish retains only the changed tiles.
CanvasTiledImage canvasTiles(const QImage &composite, CanvasImageId id,
                             const CanvasTiledImage *previous)
{
    CanvasTiledImage image;
    image.id = id;
    image.size = composite.size();
    image.tileSize = CanvasTileSize;
    for (int row = 0; row < image.rows(); ++row) {
        for (int column = 0; column < image.columns(); ++column) {
            const int index = row * image.columns() + column;
            QImage tile = composite.copy(image.tileRect(column, row));
            if (previous && index < previous->tiles.size() && previous->tiles.at(index) == tile)
                tile = previous->tiles.at(index);
            image.tiles.append(tile);
        }
    }
    return image;
}
} // namespace

QImage TiledRasterDocument::brushPreview(const QVariantMap &preset, const QColor &color,
                                         int width, int height) const
{
    Brush brush;
    if (width < 16 || height < 8 || width > 512 || height > 256 || !color.isValid()
        || !brushFromMap(preset, &brush))
        return {};
    TiledRasterDocument sample;
    sample.m_livePreview = false;
    const QString layer = QStringLiteral("preview");
    if (!sample.resizeCanvas(width, height) || !sample.createLayer(layer, layer)
        || !sample.beginStroke(layer, false, color, brushMap(brush)))
        return {};
    // AGENT-CONTRACT (docs/raster-document.md#brush-previews): one left-to-right
    // stroke through the vertical centre, pressure rising linearly 0.1 → 1,
    // no tilt. Hosts compare previews byte for byte, so keep it fixed.
    constexpr int Samples = 24;
    const qreal margin = qMin<qreal>(width / 4.0, brush.diameter / 2 + 2);
    for (int i = 0; i <= Samples; ++i) {
        const qreal t = qreal(i) / Samples;
        if (!sample.sampleStroke(margin + t * (width - 2 * margin), height / 2.0, 0.1 + 0.9 * t))
            return {};
    }
    return sample.endStroke() ? sample.composite() : QImage();
}

QString TiledRasterDocument::brushPreviewDataUrl(const QVariantMap &preset, const QColor &color,
                                                 int width, int height) const
{
    const QImage image = brushPreview(preset, color, width, height);
    return image.isNull() ? QString() : pngDataUrl(image);
}

qulonglong TiledRasterDocument::placeOnCanvas(CanvasDocument *canvas, qreal x, qreal y)
{
    if (!canvas || m_strokeActive || !std::isfinite(x) || !std::isfinite(y)) {
        setError(QStringLiteral("Placing a raster needs a canvas document, an idle raster and a finite position."));
        return 0;
    }
    const CanvasImageId imageId = canvas->state().nextImage;
    CanvasObject object;
    object.id = canvas->reserveObjectId();
    object.layer = canvas->activeLayer();
    object.shape = CanvasShape::Image;
    object.image = imageId;
    object.rect = QRectF(0, 0, m_size.width(), m_size.height());
    object.transform = QTransform::fromTranslate(x, y);
    object.fill = QColor(Qt::white); // image objects are filled for picking
    object.z = canvas->reserveZ();
    object.name = tr("Raster");
    CanvasEdit edit;
    edit.label = tr("Place raster");
    edit.images.append({imageId, {}, std::make_shared<const CanvasTiledImage>(
        canvasTiles(untaggedComposite(composite()), imageId, nullptr))});
    edit.changes.append({object.id, std::nullopt, object});
    if (const OperationError problem = canvas->commitEdit(edit)) {
        setError(problem.message);
        return 0;
    }
    m_error.clear(); emit changed();
    return object.id;
}

bool TiledRasterDocument::publishToCanvas(CanvasDocument *canvas, qulonglong objectId)
{
    const CanvasObject *object = canvas ? canvas->object(objectId) : nullptr;
    const CanvasTiledImagePtr current = object && object->shape == CanvasShape::Image
        ? canvas->image(object->image) : CanvasTiledImagePtr();
    if (m_strokeActive || !current || current->size != m_size || current->tileSize != CanvasTileSize) {
        setError(QStringLiteral("Publishing needs an idle raster and a canvas image object of the same pixel size."));
        return false;
    }
    auto next = std::make_shared<const CanvasTiledImage>(
        canvasTiles(untaggedComposite(composite()), current->id, current.get()));
    bool unchanged = true;
    for (qsizetype i = 0; unchanged && i < next->tiles.size(); ++i)
        unchanged = next->tiles.at(i).cacheKey() == current->tiles.at(i).cacheKey();
    if (!unchanged) {
        CanvasEdit edit;
        edit.label = tr("Update raster pixels");
        edit.images.append({current->id, current, next});
        if (const OperationError problem = canvas->commitEdit(edit)) {
            setError(problem.message);
            return false;
        }
    }
    m_error.clear(); emit changed();
    return true;
}

QPointF TiledRasterDocument::canvasToRaster(CanvasDocument *canvas, qulonglong objectId,
                                            qreal documentX, qreal documentY) const
{
    const CanvasObject *object = canvas ? canvas->object(objectId) : nullptr;
    bool invertible = false;
    const QTransform inverse = object && object->shape == CanvasShape::Image && !object->rect.isEmpty()
        ? object->transform.inverted(&invertible) : QTransform();
    if (!invertible) return {qQNaN(), qQNaN()}; // sampleStroke rejects non-finite points
    const QPointF local = inverse.map(QPointF(documentX, documentY));
    return {(local.x() - object->rect.left()) * m_size.width() / object->rect.width(),
            (local.y() - object->rect.top()) * m_size.height() / object->rect.height()};
}

} // namespace QindaTK
