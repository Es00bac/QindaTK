// SPDX-License-Identifier: LGPL-3.0-or-later
// Layer hierarchy of the reference canvas (D-092): derived paint ranks, the
// hierarchy check every layer edit passes, and the commands a layer tree
// sends. Each command is one recorded CanvasEdit, so undo restores the whole
// structural change at once.
#include "canvas_document.h"
#include "canvas_document_format.h"

#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
constexpr qint64 zMask = (qint64(1) << 40) - 1; // as in CanvasDocument::paintKey

bool layerBefore(const CanvasLayer *a, const CanvasLayer *b)
{
    return a->order != b->order ? a->order < b->order : a->id < b->id;
}

bool objectBefore(const CanvasObject *a, const CanvasObject *b)
{
    const qint64 za = a->z & zMask;
    const qint64 zb = b->z & zMask;
    return za != zb ? za < zb : a->id < b->id;
}

// Renumbers `ids` (paint order, bottom first) as the children of `parent`.
void renumber(QHash<CanvasLayerId, CanvasLayer> &next, const CanvasDocument &document,
              const QVector<CanvasLayerId> &ids, CanvasLayerId parent)
{
    for (int i = 0; i < ids.size(); ++i) {
        const auto found = next.constFind(ids.at(i));
        CanvasLayer layer = found != next.cend() ? found.value() : *document.layer(ids.at(i));
        layer.parent = parent;
        layer.order = i;
        next.insert(ids.at(i), layer);
    }
}
} // namespace

void CanvasDocument::refreshLayerTree()
{
    m_layerRank.clear();
    m_layerChildren.clear();
    QHash<CanvasLayerId, QVector<const CanvasLayer *>> children;
    for (const CanvasLayer &l : std::as_const(m_layers)) children[l.parent].append(&l);
    for (auto it = children.begin(); it != children.end(); ++it) {
        std::sort(it->begin(), it->end(), layerBefore);
        QVector<CanvasLayerId> ids;
        ids.reserve(it->size());
        for (const CanvasLayer *l : std::as_const(*it)) ids.append(l->id);
        m_layerChildren.insert(it.key(), ids);
    }
    // Depth first, bottom to top: a group's descendants paint in its slot.
    struct Frame {
        const QVector<CanvasLayerId> *ids;
        qsizetype next;
    };
    QVector<Frame> stack;
    const auto top = m_layerChildren.constFind(0);
    if (top != m_layerChildren.cend()) stack.append({&top.value(), 0});
    int rank = 0;
    while (!stack.isEmpty()) {
        if (stack.last().next >= stack.last().ids->size()) {
            stack.removeLast();
            continue;
        }
        const CanvasLayerId id = stack.last().ids->at(stack.last().next++);
        m_layerRank.insert(id, rank++);
        const auto nested = m_layerChildren.constFind(id);
        if (nested != m_layerChildren.cend() && stack.size() < 256) stack.append({&nested.value(), 0});
    }
}

OperationError CanvasDocument::prepareLayerTree(const CanvasEdit &edit, bool forward,
                                                const QHash<CanvasLayerId, qsizetype> &layerUse) const
{
    QHash<CanvasLayerId, CanvasLayer> result = m_layers;
    for (const auto &l : edit.layers) {
        const auto &target = forward ? l.after : l.before;
        if (!target) {
            result.remove(l.id);
            continue;
        }
        // AGENT-GUARD: a layer that is a group afterwards holds no objects,
        // counting the objects this same edit adds, moves or removes.
        if (target->group && m_layerUse.value(l.id) + layerUse.value(l.id) > 0)
            return {ErrorCode::Validation, tr("A layer group cannot hold objects"),
                    QStringLiteral("layers.group"), QStringLiteral("canvas")};
        result.insert(l.id, *target);
    }
    return CanvasDocumentFormat::validateLayerForest(result, m_limits);
}

QVariantList CanvasDocument::layerList() const
{
    QVector<const CanvasLayer *> sorted;
    sorted.reserve(m_layers.size());
    for (const CanvasLayer &l : m_layers) sorted.append(&l);
    std::sort(sorted.begin(), sorted.end(), [this](const CanvasLayer *a, const CanvasLayer *b) {
        return layerRank(a->id) < layerRank(b->id);
    });
    QVariantList out;
    out.reserve(sorted.size());
    for (const CanvasLayer *l : std::as_const(sorted)) {
        out << QVariantMap{{QStringLiteral("id"), qulonglong(l->id)},
                           {QStringLiteral("name"), l->name},
                           {QStringLiteral("order"), l->order},
                           {QStringLiteral("visible"), l->visible},
                           {QStringLiteral("locked"), l->locked},
                           {QStringLiteral("parent"), qulonglong(l->parent)},
                           {QStringLiteral("group"), l->group},
                           {QStringLiteral("depth"), layerDepth(l->id)},
                           {QStringLiteral("rank"), layerRank(l->id)},
                           {QStringLiteral("effectiveVisible"), layerEffectivelyVisible(l->id)},
                           {QStringLiteral("effectiveLocked"), layerEffectivelyLocked(l->id)}};
    }
    return out;
}

QVector<CanvasLayerId> CanvasDocument::childLayers(CanvasLayerId parent) const
{
    return m_layerChildren.value(parent);
}

QHash<CanvasLayerId, QVector<CanvasObjectId>> CanvasDocument::objectsByLayer() const
{
    QHash<CanvasLayerId, QVector<const CanvasObject *>> grouped;
    for (const CanvasObject &o : m_objects) grouped[o.layer].append(&o);
    QHash<CanvasLayerId, QVector<CanvasObjectId>> out;
    out.reserve(grouped.size());
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        std::sort(it->begin(), it->end(), objectBefore);
        QVector<CanvasObjectId> &ids = out[it.key()];
        ids.reserve(it->size());
        for (const CanvasObject *o : std::as_const(*it)) ids.append(o->id);
    }
    return out;
}

QVector<CanvasObjectId> CanvasDocument::objectsOnLayer(CanvasLayerId layerId) const
{
    QVector<const CanvasObject *> stack;
    for (const CanvasObject &o : m_objects)
        if (o.layer == layerId) stack.append(&o);
    std::sort(stack.begin(), stack.end(), objectBefore);
    QVector<CanvasObjectId> ids;
    ids.reserve(stack.size());
    for (const CanvasObject *o : std::as_const(stack)) ids.append(o->id);
    return ids;
}

int CanvasDocument::layerDepth(CanvasLayerId id) const
{
    const CanvasLayer *l = layer(id);
    int depth = -1;
    for (int step = 0; l && step < 256; ++step) {
        ++depth;
        l = l->parent ? layer(l->parent) : nullptr;
    }
    return depth;
}

bool CanvasDocument::isLayerAncestor(CanvasLayerId ancestor, CanvasLayerId id) const
{
    const CanvasLayer *l = layer(id);
    for (int step = 0; l && l->parent && step < 256; ++step) {
        if (l->parent == ancestor) return true;
        l = layer(l->parent);
    }
    return false;
}

QVariantList CanvasDocument::childLayerIds(qulonglong parent) const
{
    QVariantList out;
    for (const CanvasLayerId id : childLayers(parent)) out << qulonglong(id);
    return out;
}

QVariantList CanvasDocument::layerObjectIds(qulonglong layerId) const
{
    QVariantList out;
    for (const CanvasObjectId id : objectsOnLayer(layerId)) out << qulonglong(id);
    return out;
}

bool CanvasDocument::refuse(const QString &message)
{
    m_lastError = message;
    emit historyChanged();
    return false;
}

bool CanvasDocument::unchanged()
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit historyChanged();
    }
    return false;
}

bool CanvasDocument::commitRecorded(const CanvasEdit &edit)
{
    if (const OperationError error = commitEdit(edit)) return refuse(error.message);
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit historyChanged();
    }
    return true;
}

bool CanvasDocument::commitLayers(const QString &label, const QHash<CanvasLayerId, CanvasLayer> &layers,
                                  const QVector<CanvasLayerId> &removed,
                                  const QVector<CanvasEdit::Change> &objects)
{
    CanvasEdit edit;
    edit.label = label;
    edit.changes = objects;
    QList<CanvasLayerId> ids = layers.keys();
    std::sort(ids.begin(), ids.end());
    for (const CanvasLayerId id : std::as_const(ids)) {
        const CanvasLayer &after = *layers.constFind(id);
        const CanvasLayer *before = layer(id);
        if (before && *before == after) continue;
        edit.layers.append({id, before ? std::optional<CanvasLayer>(*before) : std::nullopt, after});
    }
    for (const CanvasLayerId id : removed)
        if (const CanvasLayer *before = layer(id)) edit.layers.append({id, *before, std::nullopt});
    if (edit.isEmpty()) return unchanged();
    return commitRecorded(edit);
}

CanvasLayerId CanvasDocument::addLayerAt(const QString &name, bool group, CanvasLayerId parent, int index)
{
    if (parent != 0) {
        const CanvasLayer *target = layer(parent);
        if (!target || !target->group) {
            refuse(tr("Layers can only be placed in a layer group"));
            return 0;
        }
    }
    CanvasLayer created;
    created.id = m_nextLayer;
    created.name = name;
    created.group = group;
    QHash<CanvasLayerId, CanvasLayer> next{{created.id, created}};
    QVector<CanvasLayerId> siblings = childLayers(parent);
    siblings.insert(std::clamp(index, 0, int(siblings.size())), created.id);
    renumber(next, *this, siblings, parent);
    return commitLayers(group ? tr("Add layer group") : tr("Add layer"), next) ? created.id : 0;
}

CanvasLayerId CanvasDocument::addLayerGroup(const QString &name)
{
    return addLayerAt(name, true, 0, int(childLayers(0).size()));
}

bool CanvasDocument::moveLayer(CanvasLayerId id, CanvasLayerId parent, int index)
{
    if (!layer(id)) return refuse(tr("The layer no longer exists"));
    if (parent != 0) {
        const CanvasLayer *target = layer(parent);
        if (!target || !target->group) return refuse(tr("Layers can only be placed in a layer group"));
        // AGENT-GUARD: a group moved into itself or a descendant would form a
        // cycle. Refuse with a reason here; the commit check is the backstop.
        if (parent == id || isLayerAncestor(id, parent))
            return refuse(tr("A layer group cannot be placed inside itself"));
    }
    QVector<CanvasLayerId> siblings = childLayers(parent);
    siblings.removeAll(id);
    siblings.insert(std::clamp(index, 0, int(siblings.size())), id);
    QHash<CanvasLayerId, CanvasLayer> next;
    renumber(next, *this, siblings, parent);
    return commitLayers(tr("Move layer"), next);
}

bool CanvasDocument::renameLayer(CanvasLayerId id, const QString &name)
{
    const CanvasLayer *current = layer(id);
    if (!current) return refuse(tr("The layer no longer exists"));
    CanvasLayer renamed = *current;
    renamed.name = name;
    return commitLayers(tr("Rename layer"), {{id, renamed}});
}

CanvasLayerId CanvasDocument::groupLayers(const QVector<CanvasLayerId> &ids, const QString &name)
{
    const CanvasLayer *first = ids.isEmpty() ? nullptr : layer(ids.first());
    if (!first) {
        refuse(tr("Choose layers to group"));
        return 0;
    }
    QSet<CanvasLayerId> chosen;
    for (const CanvasLayerId id : ids) {
        const CanvasLayer *l = layer(id);
        if (!l || l->parent != first->parent) {
            refuse(tr("Only sibling layers can be grouped together"));
            return 0;
        }
        chosen.insert(id);
    }
    CanvasLayer created;
    created.id = m_nextLayer;
    created.name = name;
    created.group = true;
    QVector<CanvasLayerId> siblings;
    QVector<CanvasLayerId> members;
    qsizetype place = 0;
    for (const CanvasLayerId id : childLayers(first->parent)) {
        if (chosen.contains(id)) {
            members.append(id);
            place = siblings.size(); // the new group takes the topmost member's place
        } else {
            siblings.append(id);
        }
    }
    siblings.insert(place, created.id);
    QHash<CanvasLayerId, CanvasLayer> next{{created.id, created}};
    renumber(next, *this, members, created.id);
    renumber(next, *this, siblings, first->parent);
    return commitLayers(tr("Group layers"), next) ? created.id : 0;
}

qulonglong CanvasDocument::groupLayersById(const QVariantList &ids, const QString &name)
{
    QVector<CanvasLayerId> layers;
    for (const QVariant &value : ids) layers.append(value.toULongLong());
    return groupLayers(layers, name);
}

bool CanvasDocument::ungroupLayer(CanvasLayerId id)
{
    const CanvasLayer *group = layer(id);
    if (!group || !group->group) return refuse(tr("Only a layer group can be ungrouped"));
    QVector<CanvasLayerId> siblings = childLayers(group->parent);
    const qsizetype at = siblings.indexOf(id);
    if (at < 0) return refuse(tr("The layer hierarchy changed"));
    const QVector<CanvasLayerId> members = childLayers(id);
    siblings.removeAt(at);
    for (qsizetype i = 0; i < members.size(); ++i) siblings.insert(at + i, members.at(i));
    QHash<CanvasLayerId, CanvasLayer> next;
    renumber(next, *this, siblings, group->parent);
    return commitLayers(tr("Ungroup layers"), next, {id});
}

bool CanvasDocument::removeLayerTree(CanvasLayerId id)
{
    const CanvasLayer *root = layer(id);
    if (!root) return refuse(tr("The layer no longer exists"));
    if (layerEffectivelyLocked(id)) return refuse(tr("Unlock the layer before deleting it"));
    QSet<CanvasLayerId> doomed;
    QVector<CanvasLayerId> pending{id};
    while (!pending.isEmpty()) {
        const CanvasLayerId current = pending.takeLast();
        if (layer(current)->locked) return refuse(tr("Unlock the nested layers before deleting them"));
        doomed.insert(current);
        pending += childLayers(current);
    }
    QVector<CanvasEdit::Change> objects;
    for (const CanvasObject &o : std::as_const(m_objects)) {
        if (!doomed.contains(o.layer)) continue;
        if (o.locked) return refuse(tr("Unlock the objects on this layer before deleting it"));
        objects.append({o.id, o, std::nullopt});
    }
    std::sort(objects.begin(), objects.end(),
              [](const CanvasEdit::Change &a, const CanvasEdit::Change &b) { return a.id < b.id; });
    QVector<CanvasLayerId> removed(doomed.cbegin(), doomed.cend());
    std::sort(removed.begin(), removed.end());
    return commitLayers(root->group ? tr("Delete layer group") : tr("Delete layer"), {}, removed, objects);
}

bool CanvasDocument::moveObjectToLayer(CanvasObjectId id, CanvasLayerId layerId, int index)
{
    const CanvasObject *moving = object(id);
    const CanvasLayer *target = layer(layerId);
    if (!moving) return refuse(tr("The object no longer exists"));
    if (!target || target->group) return refuse(tr("Objects can only be placed on a content layer"));
    // AGENT-NOTE: a lock blocks edits to what it covers
    // (docs/canvas.md#layer-hierarchy): a locked object keeps its place and a
    // locked layer receives nothing. Neighbours only take fresh paint orders.
    if (effectiveLocked(*moving)) return refuse(tr("A locked object cannot be moved"));
    if (moving->layer != layerId && layerEffectivelyLocked(layerId))
        return refuse(tr("A locked layer cannot receive objects"));
    QVector<const CanvasObject *> stack;
    for (const CanvasObject &o : std::as_const(m_objects))
        if (o.layer == layerId && o.id != id) stack.append(&o);
    std::sort(stack.begin(), stack.end(), objectBefore);
    index = std::clamp(index, 0, int(stack.size()));
    if (moving->layer == layerId) {
        const auto below = std::count_if(stack.cbegin(), stack.cend(), [moving](const CanvasObject *o) {
            return objectBefore(o, moving);
        });
        if (below == index) return unchanged(); // already there
    }
    // Fresh paint orders exceed every existing one: the moved object first,
    // then the objects that were above the insertion point, in order.
    CanvasObject placed = *moving;
    placed.layer = layerId;
    placed.z = reserveZ();
    QVector<CanvasEdit::Change> changes{{id, *moving, placed}};
    for (qsizetype i = index; i < stack.size(); ++i) {
        CanvasObject raised = *stack.at(i);
        raised.z = reserveZ();
        changes.append({raised.id, *stack.at(i), raised});
    }
    CanvasEdit edit;
    edit.label = moving->layer == layerId ? tr("Reorder object") : tr("Move object to layer");
    edit.changes = changes;
    return commitRecorded(edit);
}

} // namespace QindaTK
