// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document.h"
#include "canvas_transform.h"

#include <QUuid>
#include <algorithm>

namespace QindaTK {

bool CanvasDocumentAdapter::effectiveVisible(const CanvasObject &object) const
{
    const CanvasLayer *l = layer(object.layer);
    return object.visible && (l == nullptr || l->visible);
}

bool CanvasDocumentAdapter::effectiveLocked(const CanvasObject &object) const
{
    const CanvasLayer *l = layer(object.layer);
    return object.locked || (l != nullptr && l->locked);
}

qint64 CanvasDocumentAdapter::paintKey(const CanvasObject &object) const
{
    const CanvasLayer *l = layer(object.layer);
    return (qint64(l ? l->order : 0) << 40) + (object.z & ((qint64(1) << 40) - 1));
}

CanvasDocument::CanvasDocument(QObject *parent)
    : CanvasDocumentAdapter(parent),
      m_documentId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_activeLayer = addLayer(tr("Layer 1"));
}

void CanvasDocument::setPageRect(const QRectF &rect)
{
    if (rect == m_pageRect || !rect.isValid()) {
        return;
    }
    const QRectF old = m_pageRect;
    m_pageRect = rect;
    finishChange({old, rect}, false);
}

void CanvasDocument::setPageColor(const QColor &color)
{
    if (color == m_pageColor || !color.isValid()) {
        return;
    }
    m_pageColor = color;
    finishChange({m_pageRect}, false);
}

QRectF CanvasDocument::contentBounds() const
{
    QRectF bounds = m_pageRect;
    for (const QRectF &b : m_bounds) {
        bounds |= b;
    }
    return bounds;
}

const CanvasObject *CanvasDocument::object(CanvasObjectId id) const
{
    const auto it = m_objects.constFind(id);
    return it == m_objects.cend() ? nullptr : &it.value();
}

const CanvasLayer *CanvasDocument::layer(CanvasLayerId id) const
{
    const auto it = m_layers.constFind(id);
    return it == m_layers.cend() ? nullptr : &it.value();
}

CanvasTiledImagePtr CanvasDocument::image(CanvasImageId id) const
{
    return m_images.value(id);
}

QVector<CanvasObjectId> CanvasDocument::candidates(const QRectF &region) const
{
    return m_index.query(region);
}

qint64 CanvasDocument::paintKey(const CanvasObject &object) const
{
    return CanvasDocumentAdapter::paintKey(object);
}

void CanvasDocument::setActiveLayer(CanvasLayerId id)
{
    if (id != m_activeLayer && m_layers.contains(id)) {
        m_activeLayer = id;
        emit changed();
    }
}

QVariantList CanvasDocument::layerList() const
{
    QList<CanvasLayer> sorted = m_layers.values();
    std::sort(sorted.begin(), sorted.end(),
              [](const CanvasLayer &a, const CanvasLayer &b) { return a.order < b.order; });
    QVariantList out;
    for (const CanvasLayer &l : sorted) {
        out << QVariantMap{{QStringLiteral("id"), qulonglong(l.id)},
                           {QStringLiteral("name"), l.name},
                           {QStringLiteral("order"), l.order},
                           {QStringLiteral("visible"), l.visible},
                           {QStringLiteral("locked"), l.locked}};
    }
    return out;
}

QVector<CanvasObjectId> CanvasDocument::degenerateObjects() const
{
    QVector<CanvasObjectId> out;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (!CanvasGeometry::isRenderable(it.value())) {
            out << it.key();
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

QVector<CanvasObjectId> CanvasDocument::objectIds() const
{
    QVector<CanvasObjectId> ids = m_objects.keys().toVector();
    std::sort(ids.begin(), ids.end());
    return ids;
}

QString CanvasDocument::undoText() const
{
    return canUndo() ? m_history.at(m_cursor - 1).label : QString{};
}

void CanvasDocument::setHistoryLimit(int limit)
{
    limit = std::max(1, limit);
    if (limit == m_historyLimit) {
        return;
    }
    m_historyLimit = limit;
    while (m_history.size() > m_historyLimit) {
        m_history.removeFirst();
        m_cursor = std::max<qsizetype>(0, m_cursor - 1);
    }
    emit changed();
}

std::optional<CanvasObject> CanvasDocument::copy(CanvasObjectId id) const
{
    const CanvasObject *o = object(id);
    return o ? std::optional<CanvasObject>(*o) : std::nullopt;
}

void CanvasDocument::place(const CanvasObject &object, QVector<QRectF> *damage)
{
    unplace(object.id, damage);
    m_objects.insert(object.id, object);
    const QRectF bounds = CanvasGeometry::documentBounds(object);
    m_bounds.insert(object.id, bounds);
    m_index.insert(object.id, bounds);
    if (!bounds.isNull()) {
        damage->append(bounds);
    }
    m_nextZ = std::max(m_nextZ, object.z + 1);
    m_nextId = std::max(m_nextId, object.id + 1);
}

void CanvasDocument::unplace(CanvasObjectId id, QVector<QRectF> *damage)
{
    if (!m_objects.contains(id)) {
        return;
    }
    const QRectF bounds = m_bounds.take(id);
    if (!bounds.isNull()) {
        damage->append(bounds);
    }
    m_index.remove(id);
    m_objects.remove(id);
}

void CanvasDocument::apply(const CanvasEdit &edit, bool forward, QVector<QRectF> *damage)
{
    auto step = [&](const CanvasEdit::Change &c) {
        const auto &target = forward ? c.after : c.before;
        if (target) {
            place(*target, damage);
        } else {
            unplace(c.id, damage);
        }
    };
    if (forward) {
        std::for_each(edit.changes.cbegin(), edit.changes.cend(), step);
    } else {
        std::for_each(edit.changes.crbegin(), edit.changes.crend(), step);
    }
}

bool CanvasDocument::commit(const CanvasEdit &edit)
{
    if (edit.isEmpty()) {
        return false;
    }
    // Validate against the current state before touching anything, so a
    // stale edit (built against an older revision) is rejected whole.
    for (const auto &c : edit.changes) {
        const CanvasObject *current = object(c.id);
        if (bool(current) != c.before.has_value() || (current && !(*current == *c.before))) {
            return false;
        }
        if (c.after && (c.after->id != c.id || !m_layers.contains(c.after->layer))) {
            return false;
        }
    }
    QVector<QRectF> damage;
    apply(edit, true, &damage);
    m_history.resize(m_cursor);
    m_history.append(edit);
    if (m_history.size() > m_historyLimit) {
        m_history.removeFirst();
    }
    m_cursor = m_history.size();
    finishChange(damage, false);
    return true;
}

bool CanvasDocument::undo()
{
    if (!canUndo()) {
        return false;
    }
    QVector<QRectF> damage;
    apply(m_history.at(--m_cursor), false, &damage);
    finishChange(damage, false);
    return true;
}

bool CanvasDocument::redo()
{
    if (!canRedo()) {
        return false;
    }
    QVector<QRectF> damage;
    apply(m_history.at(m_cursor++), true, &damage);
    finishChange(damage, false);
    return true;
}

void CanvasDocument::finishChange(const QVector<QRectF> &damage, bool full)
{
    ++m_revision;
    if (full) {
        emit reset(m_revision);
    } else if (!damage.isEmpty()) {
        emit damaged(damage, m_revision);
    }
    emit edited(m_revision);
    emit changed();
}

CanvasObjectId CanvasDocument::insert(CanvasObject object, bool record)
{
    if (object.id == 0) {
        object.id = allocateId();
    }
    if (!m_layers.contains(object.layer)) {
        object.layer = m_activeLayer;
    }
    if (object.z == 0) {
        object.z = m_nextZ++;
    }
    if (record) {
        CanvasEdit edit;
        edit.label = tr("Add object");
        edit.changes.append({object.id, std::nullopt, object});
        return commit(edit) ? object.id : 0;
    }
    QVector<QRectF> damage;
    place(object, &damage);
    finishChange(damage, false);
    return object.id;
}

CanvasLayerId CanvasDocument::addLayer(const QString &name)
{
    CanvasLayer l;
    l.id = m_nextLayer++;
    l.name = name;
    l.order = int(m_layers.size());
    m_layers.insert(l.id, l);
    // A new empty layer on top changes no pixels but invalidates order keys
    // cached by consumers.
    finishChange({}, true);
    return l.id;
}

CanvasImageId CanvasDocument::addImage(CanvasTiledImage image)
{
    image.id = m_nextImage++;
    const CanvasImageId id = image.id;
    m_images.insert(id, std::make_shared<const CanvasTiledImage>(std::move(image)));
    return id;
}

void CanvasDocument::layerDamage(CanvasLayerId id, QVector<QRectF> *damage) const
{
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (it->layer == id) {
            const QRectF b = m_bounds.value(it.key());
            if (!b.isNull()) {
                damage->append(b);
            }
        }
    }
}

bool CanvasDocument::setLayerVisible(CanvasLayerId id, bool visible)
{
    auto it = m_layers.find(id);
    if (it == m_layers.end() || it->visible == visible) {
        return false;
    }
    it->visible = visible;
    QVector<QRectF> damage;
    layerDamage(id, &damage);
    finishChange(damage, false);
    return true;
}

bool CanvasDocument::setLayerLocked(CanvasLayerId id, bool locked)
{
    auto it = m_layers.find(id);
    if (it == m_layers.end() || it->locked == locked) {
        return false;
    }
    it->locked = locked;
    finishChange({}, false); // no pixels change
    return true;
}

void CanvasDocument::clear()
{
    m_objects.clear();
    m_bounds.clear();
    m_index.clear();
    m_images.clear();
    m_history.clear();
    m_cursor = 0;
    finishChange({}, true);
}

} // namespace QindaTK
