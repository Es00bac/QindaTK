// SPDX-License-Identifier: LGPL-3.0-or-later
// QML facade and deterministic fixture of CanvasDocument.
#include "canvas_document.h"
#include "canvas_transform.h"

#include <QtMath>
#include <cmath>

namespace QindaTK {

namespace {

CanvasObject shaped(CanvasShape shape, qreal x, qreal y, qreal w, qreal h, const QColor &fill,
                    const QColor &stroke, qreal strokeWidth)
{
    CanvasObject o;
    o.shape = shape;
    o.rect = QRectF(0, 0, w, h);
    o.transform = QTransform::fromTranslate(x, y);
    o.fill = fill;
    o.stroke = stroke;
    o.strokeWidth = strokeWidth;
    return o;
}

// xorshift32: stable across platforms and Qt versions, unlike QRandomGenerator
// seeding which the fixture must not depend on.
struct Rng {
    quint32 state;
    quint32 next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    qreal unit() { return (next() & 0xFFFFFF) / qreal(0x1000000); }
};

QImage fixtureTile(int column, int row, int tileSize, QSize full)
{
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < tileSize; ++y) {
        auto *line = reinterpret_cast<QRgb *>(tile.scanLine(y));
        const int gy = row * tileSize + y;
        for (int x = 0; x < tileSize; ++x) {
            const int gx = column * tileSize + x;
            const bool grid = (gx % 128) == 0 || (gy % 128) == 0;
            const int r = 40 + (160 * gx) / std::max(1, full.width());
            const int g = 60 + (140 * gy) / std::max(1, full.height());
            const int b = ((gx / 64 + gy / 64) % 2) ? 150 : 110;
            line[x] = grid ? qRgb(230, 230, 230) : qRgb(r, g, b);
        }
    }
    return tile;
}

} // namespace

qulonglong CanvasDocument::addRect(qreal x, qreal y, qreal w, qreal h, const QColor &fill,
                                   const QColor &stroke, qreal strokeWidth)
{
    return insert(shaped(CanvasShape::Rect, x, y, w, h, fill, stroke, strokeWidth));
}

qulonglong CanvasDocument::addEllipse(qreal x, qreal y, qreal w, qreal h, const QColor &fill,
                                      const QColor &stroke, qreal strokeWidth)
{
    return insert(shaped(CanvasShape::Ellipse, x, y, w, h, fill, stroke, strokeWidth));
}

qulonglong CanvasDocument::addPolyline(const QVariantList &points, const QColor &stroke,
                                       qreal strokeWidth)
{
    CanvasObject o;
    o.shape = CanvasShape::Polyline;
    o.stroke = stroke;
    o.strokeWidth = strokeWidth;
    for (const QVariant &v : points) {
        o.points << v.toPointF();
    }
    if (o.points.isEmpty()) {
        return 0;
    }
    return insert(o);
}

bool CanvasDocument::setObjectTransform(qulonglong id, const QVariantList &m)
{
    const CanvasObject *current = object(id);
    if (!current || m.size() != 6) {
        return false;
    }
    CanvasObject next = *current;
    next.transform = QTransform(m[0].toDouble(), m[1].toDouble(), m[2].toDouble(),
                                m[3].toDouble(), m[4].toDouble(), m[5].toDouble());
    // Non-finite input is rejected; a finite singular matrix is accepted and
    // makes the object degenerate (see CanvasGeometry::isRenderable).
    if (!CanvasTransform::isFinite(next.transform)) {
        return false;
    }
    CanvasEdit edit;
    edit.label = tr("Transform");
    edit.changes.append({id, *current, next});
    return commit(edit);
}

bool CanvasDocument::setObjectVisible(qulonglong id, bool visible)
{
    const CanvasObject *current = object(id);
    if (!current || current->visible == visible) {
        return false;
    }
    CanvasObject next = *current;
    next.visible = visible;
    CanvasEdit edit;
    edit.label = visible ? tr("Show") : tr("Hide");
    edit.changes.append({id, *current, next});
    return commit(edit);
}

bool CanvasDocument::setObjectLocked(qulonglong id, bool locked)
{
    const CanvasObject *current = object(id);
    if (!current || current->locked == locked) {
        return false;
    }
    CanvasObject next = *current;
    next.locked = locked;
    CanvasEdit edit;
    edit.label = locked ? tr("Lock") : tr("Unlock");
    edit.changes.append({id, *current, next});
    return commit(edit);
}

bool CanvasDocument::moveObjects(const QVariantList &ids, qreal dx, qreal dy)
{
    CanvasEdit edit;
    edit.label = tr("Move");
    for (const QVariant &v : ids) {
        const CanvasObject *current = object(v.toULongLong());
        if (!current || effectiveLocked(*current)) {
            continue;
        }
        CanvasObject next = *current;
        next.transform = next.transform * QTransform::fromTranslate(dx, dy);
        edit.changes.append({current->id, *current, next});
    }
    return commit(edit);
}

bool CanvasDocument::removeObject(qulonglong id)
{
    const CanvasObject *current = object(id);
    if (!current) {
        return false;
    }
    CanvasEdit edit;
    edit.label = tr("Delete");
    edit.changes.append({id, *current, std::nullopt});
    return commit(edit);
}

QVariantMap CanvasDocument::objectInfo(qulonglong id) const
{
    const CanvasObject *o = object(id);
    if (!o) {
        return {};
    }
    static const char *const kinds[] = {"rect", "ellipse", "polyline", "image", "path"};
    const QTransform &t = o->transform;
    const QRectF b = m_bounds.value(o->id);
    return {{QStringLiteral("id"), qulonglong(o->id)},
            {QStringLiteral("layer"), qulonglong(o->layer)},
            {QStringLiteral("kind"), QString::fromLatin1(kinds[int(o->shape)])},
            {QStringLiteral("name"), o->name},
            {QStringLiteral("visible"), o->visible},
            {QStringLiteral("locked"), o->locked},
            {QStringLiteral("effectiveVisible"), effectiveVisible(*o)},
            {QStringLiteral("effectiveLocked"), effectiveLocked(*o)},
            {QStringLiteral("degenerate"), !CanvasGeometry::isRenderable(*o)},
            {QStringLiteral("bounds"), b},
            {QStringLiteral("matrix"), QVariantList{t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()}}};
}

qulonglong CanvasDocument::populateFixture(int count, int seed, bool withImage)
{
    count = std::clamp(count, 0, 200000);
    Rng rng{quint32(seed == 0 ? 0x9E3779B9u : quint32(seed))};
    const int columns = std::max(1, int(std::ceil(std::sqrt(double(count)))));
    constexpr qreal pitch = 48.0;
    const qreal extent = columns * pitch;
    m_pageRect = QRectF(0, 0, extent, extent);

    CanvasImageId imageId = 0;
    const CanvasLayerId imageLayer = m_activeLayer;
    if (withImage) {
        CanvasTiledImage image;
        image.size = QSize(2048, 1536);
        image.tileSize = 256;
        for (int row = 0; row < image.rows(); ++row) {
            for (int column = 0; column < image.columns(); ++column) {
                image.tiles << fixtureTile(column, row, image.tileSize, image.size);
            }
        }
        imageId = m_nextImage;
        image.id = imageId;
        placeImage(imageId, std::make_shared<const CanvasTiledImage>(std::move(image)));
        CanvasObject o;
        o.shape = CanvasShape::Image;
        o.layer = imageLayer;
        o.image = imageId;
        o.rect = QRectF(0, 0, 2048, 1536);
        o.fill = QColor(255, 255, 255); // image objects are "filled" for picking
        // Scaled to span the page so the image sits under every shape.
        const qreal s = extent / 2048.0;
        o.transform = QTransform::fromScale(s, s);
        o.name = QStringLiteral("Tiled image");
        o.z = m_nextZ++;
        o.id = allocateId();
        QVector<QRectF> ignored;
        place(o, &ignored);
    }
    CanvasLayer shapeLayer;
    shapeLayer.id = m_nextLayer;
    shapeLayer.name = tr("Shapes");
    shapeLayer.order = nextLayerOrder();
    placeLayer(shapeLayer);
    refreshLayerTree();
    const CanvasLayerId shapes = shapeLayer.id;
    for (int i = 0; i < count; ++i) {
        const int cx = i % columns;
        const int cy = i / columns;
        const qreal w = 16 + rng.unit() * 24;
        const qreal h = 16 + rng.unit() * 24;
        const QColor fill = QColor::fromHsvF(rng.unit(), 0.55, 0.9, 0.85);
        const QColor stroke = QColor::fromHsvF(rng.unit(), 0.4, 0.3);
        const int kind = int(rng.next() % 3);
        CanvasObject o = shaped(kind == 0 ? CanvasShape::Rect : CanvasShape::Ellipse,
                                cx * pitch + 4, cy * pitch + 4, w, h, fill, stroke, 1.0);
        if (kind == 2) {
            o.shape = CanvasShape::Polyline;
            o.fill = {};
            o.strokeWidth = 2.0;
            o.points = {QPointF(0, h), QPointF(w / 2, 0), QPointF(w, h * 0.6)};
        }
        if (i % 7 == 3) { // some rotated about their own centre
            const QPointF c(w / 2, h / 2);
            o.transform = QTransform::fromTranslate(-c.x(), -c.y())
                * QTransform().rotate(rng.unit() * 90.0)
                * QTransform::fromTranslate(c.x() + cx * pitch + 4, c.y() + cy * pitch + 4);
        }
        o.layer = shapes;
        o.locked = (i % 97) == 41;
        o.visible = (i % 101) != 57;
        o.z = m_nextZ++;
        o.id = allocateId();
        QVector<QRectF> ignored;
        place(o, &ignored);
    }
    m_activeLayer = shapes;
    resetModel();
    finishChange({}, true);
    return imageId;
}

} // namespace QindaTK
