// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_canvas_document.h"
#include "canvas_geometry.h"
#include "slide_deck.h"
#include "slide_render.h"

#include <QJsonDocument>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace QindaTK {
using namespace Slides;

namespace {
constexpr int TileSize = 256;

// Everything renderObjectImage reads, so an unchanged object keeps its image.
QString imageKey(const ResolvedObject &o, const QString &foreground, qreal scale, bool prompts)
{
    // Concatenated, never QString::arg: text may itself contain "%1".
    return QStringList{o.kind, o.fill, foreground, prompts && o.text.plain.isEmpty() ? o.prompt : QString(),
                       QString::number(o.fontSize), QString::number(o.bounds.width()),
                       QString::number(o.bounds.height()), QString::number(scale),
                       QString::fromUtf8(QJsonDocument(o.text.rich).toJson(QJsonDocument::Compact))}
        .join(QChar(0x1f));
}

CanvasTiledImagePtr tiled(const QImage &image, CanvasImageId id)
{
    auto out = std::make_shared<CanvasTiledImage>();
    out->id = id;
    out->size = image.size();
    out->tileSize = TileSize;
    for (int row = 0; row < out->rows(); ++row)
        for (int column = 0; column < out->columns(); ++column)
            out->tiles << image.copy(out->tileRect(column, row));
    return out;
}
} // namespace

SlideCanvasDocument::SlideCanvasDocument(QObject *parent) : CanvasDocumentAdapter(parent)
{
    m_layer.id = 1;
    m_layer.name = QStringLiteral("Slide");
}

void SlideCanvasDocument::setDeck(SlideDeck *deck)
{
    if (m_deck == deck) return;
    disconnect(m_deckConnection);
    m_deck = deck;
    if (deck) m_deckConnection = connect(deck, &SlideDeck::changed, this, &SlideCanvasDocument::rebuild);
    emit sourceChanged();
    rebuild();
}

void SlideCanvasDocument::setSlideId(const QString &id)
{
    if (m_slideId == id) return;
    m_slideId = id;
    emit sourceChanged();
    rebuild();
}

void SlideCanvasDocument::setShowPrompts(bool show)
{
    if (m_showPrompts == show) return;
    m_showPrompts = show;
    emit sourceChanged();
    rebuild();
}

void SlideCanvasDocument::setRenderScale(qreal scale)
{
    scale = std::isfinite(scale) ? std::clamp<qreal>(scale, 0.25, 8) : 2;
    if (qFuzzyCompare(scale, m_renderScale)) return;
    m_renderScale = scale;
    emit sourceChanged();
    rebuild();
}

QString SlideCanvasDocument::documentId() const
{
    return m_deck ? m_deck->documentId() + u'#' + m_slideId : QString();
}

QRectF SlideCanvasDocument::pageRect() const
{
    return m_deck ? QRectF(QPointF(0, 0), m_deck->state().size) : QRectF(0, 0, 960, 540);
}

QRectF SlideCanvasDocument::contentBounds() const
{
    QRectF bounds = pageRect();
    for (const CanvasObject &o : m_objects) bounds |= CanvasGeometry::documentBounds(o);
    return bounds;
}

QColor SlideCanvasDocument::pageColor() const
{
    const int i = m_deck ? slideIndex(m_deck->state(), m_slideId) : -1;
    const Master *master = i < 0 ? nullptr : findMaster(m_deck->state(), m_deck->state().slides.at(i).masterId);
    const QColor color(master ? master->background : QString());
    return color.isValid() ? color : QColor(Qt::white);
}

const CanvasObject *SlideCanvasDocument::object(CanvasObjectId id) const
{
    const int i = m_index.value(id, -1);
    return i < 0 ? nullptr : &m_objects.at(i);
}

const CanvasLayer *SlideCanvasDocument::layer(CanvasLayerId id) const
{
    return id == m_layer.id ? &m_layer : nullptr;
}

CanvasTiledImagePtr SlideCanvasDocument::image(CanvasImageId id) const
{
    return m_images.value(id).image;
}

QVector<CanvasObjectId> SlideCanvasDocument::candidates(const QRectF &region) const
{
    QVector<CanvasObjectId> out;
    for (const CanvasObject &o : m_objects)
        if (CanvasGeometry::documentBounds(o).intersects(region)) out << o.id;
    std::sort(out.begin(), out.end());
    return out;
}

QVariantList SlideCanvasDocument::guides() const
{
    const QRectF page = pageRect();
    return {QVariantMap{{"id", "slide-centre-vertical"}, {"orientation", "vertical"}, {"position", page.center().x()}},
            QVariantMap{{"id", "slide-centre-horizontal"}, {"orientation", "horizontal"}, {"position", page.center().y()}}};
}

QStringList SlideCanvasDocument::slideObjectIds(const QVariantList &canvasIds) const
{
    QStringList out;
    for (const QVariant &id : canvasIds) {
        const QString slideId = m_slideIds.value(id.toULongLong());
        if (!slideId.isEmpty()) out << slideId;
    }
    return out;
}

QVariantList SlideCanvasDocument::canvasObjectIds(const QStringList &slideObjectIds) const
{
    QVariantList out;
    for (const QString &id : slideObjectIds)
        if (const CanvasObjectId canvasId = m_canvasIds.value(id)) out << QVariant::fromValue<qulonglong>(canvasId);
    return out;
}

CanvasObjectId SlideCanvasDocument::idFor(const QString &slideObjectId)
{
    auto it = m_canvasIds.constFind(slideObjectId);
    if (it != m_canvasIds.constEnd()) return *it;
    const CanvasObjectId id = m_nextId++;
    m_canvasIds.insert(slideObjectId, id);
    m_slideIds.insert(id, slideObjectId);
    return id;
}

QTransform SlideCanvasDocument::objectTransform(const QRectF &bounds, double rotation)
{
    QTransform t;
    t.translate(bounds.center().x(), bounds.center().y());
    t.rotate(rotation);
    t.translate(-bounds.width() / 2, -bounds.height() / 2);
    return t;
}

void SlideCanvasDocument::projectTransform(const QRectF &localRect, const QTransform &transform,
                                           QRectF *bounds, double *rotation)
{
    // Local x axis (m11, m12) gives the angle and x scale; the y scale is
    // the determinant over it (the y axis component perpendicular to x).
    const qreal sx = std::hypot(transform.m11(), transform.m12());
    const qreal sy = sx > 0 ? std::abs(transform.determinant()) / sx : 0;
    const QPointF centre = transform.map(localRect.center());
    const QSizeF size(localRect.width() * sx, localRect.height() * sy);
    *bounds = QRectF(centre - QPointF(size.width() / 2, size.height() / 2), size);
    double degrees = qRadiansToDegrees(std::atan2(transform.m12(), transform.m11()));
    if (degrees <= -180) degrees += 360;
    if (degrees > 180) degrees -= 360;
    if (std::abs(degrees) < 1e-9) degrees = 0;
    *rotation = degrees;
}

void SlideCanvasDocument::rebuild()
{
    QVector<CanvasObject> objects;
    QHash<CanvasObjectId, CachedImage> images;
    const int index = m_deck ? slideIndex(m_deck->state(), m_slideId) : -1;
    if (index >= 0) {
        const DeckState &deck = m_deck->state();
        const Slide &slide = deck.slides.at(index);
        const Master *master = findMaster(deck, slide.masterId);
        const QString foreground = master ? master->foreground : QString();
        const QVector<ResolvedObject> resolved = resolveObjects(deck, slide);
        objects.reserve(resolved.size());
        for (int z = 0; z < resolved.size(); ++z) {
            const ResolvedObject &r = resolved.at(z);
            CanvasObject o;
            o.id = idFor(r.id);
            o.layer = m_layer.id;
            o.shape = CanvasShape::Image;
            o.rect = QRectF(QPointF(0, 0), r.bounds.size());
            o.transform = objectTransform(r.bounds, r.rotation);
            o.fill = QColor(255, 255, 255); // image objects are "filled" for picking
            o.image = o.id;
            o.z = z;
            o.visible = r.visible;
            o.locked = r.locked;
            o.name = !r.text.plain.isEmpty() ? r.text.plain.section(u'\n', 0, 0).left(40)
                   : !r.role.isEmpty() ? r.role : r.kind;
            // Reuse the image while nothing it depends on changed.
            const QString key = imageKey(r, foreground, m_renderScale, m_showPrompts);
            CachedImage cached = m_images.value(o.id);
            if (!cached.image || cached.key != key) {
                const QImage pixels = renderObjectImage(deck, slide.id, r, m_renderScale, m_showPrompts);
                cached = {key, tiled(pixels.isNull() ? QImage(1, 1, QImage::Format_ARGB32_Premultiplied) : pixels, o.id)};
                ++m_imageRenders;
            }
            images.insert(o.id, cached);
            objects << o;
        }
    }
    m_objects = std::move(objects);
    m_images = std::move(images);
    m_index.clear();
    for (int i = 0; i < m_objects.size(); ++i) m_index.insert(m_objects.at(i).id, i);
    ++m_revision;
    emit reset(m_revision);
    emit edited(m_revision);
    emit changed();
}

bool SlideCanvasDocument::commit(const CanvasEdit &edit)
{
    if (!m_deck || !edit.layers.isEmpty() || !edit.images.isEmpty() || edit.page || edit.guides || edit.artboards)
        return false;
    if (edit.changes.isEmpty()) return true;
    QVector<SlideDeck::ObjectGeometry> geometry;
    QStringList removed;
    for (const CanvasEdit::Change &change : edit.changes) {
        const CanvasObject *current = object(change.id);
        // Stale, or an insert: slide objects are created through the deck.
        if (!current || !change.before || !(*change.before == *current)) return false;
        const QString id = m_slideIds.value(change.id);
        if (!change.after) { removed << id; continue; }
        if (change.after->shape != current->shape || change.after->rect != current->rect) return false;
        QRectF bounds;
        double rotation = 0;
        projectTransform(current->rect, change.after->transform, &bounds, &rotation);
        geometry.append({id, bounds, rotation});
    }
    const QString label = edit.label.isEmpty() ? QStringLiteral("Transform objects") : edit.label;
    return m_deck->editObjects(label, geometry, removed);
}

} // namespace QindaTK
