// SPDX-License-Identifier: LGPL-3.0-or-later
// CanvasLayerTreeModel requests: each one is a CanvasDocument command (one
// undo step) or a write to the shared CanvasSelectionModel (D-094).
#include "canvas_layer_tree_model.h"

#include <algorithm>

namespace QindaTK {
namespace {
QString layerKey(quint64 id) { return QStringLiteral("layer:%1").arg(id); }
} // namespace

bool CanvasLayerTreeModel::fail(const QString &message)
{
    m_lastError = message;
    emit errorChanged();
    return false;
}

bool CanvasLayerTreeModel::succeed()
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit errorChanged();
    }
    return true;
}

bool CanvasLayerTreeModel::select(const QString &key, bool toggle)
{
    const Node *node = find(key);
    if (!node || !m_document) return fail(tr("The row no longer exists"));
    if (node->kind == Kind::Group) return succeed(); // a group row is only the tree's current row
    if (node->kind != Kind::Object) return activate(key);
    if (!m_selectionModel) return fail(tr("The tree has no shared selection model"));
    const CanvasObject *object = m_document->object(node->id);
    if (!object) return fail(tr("The object no longer exists"));
    // The same rule as canvas selection: hidden or locked objects are not
    // selectable anywhere (CanvasSelectionModel would drop them anyway).
    if (!m_document->effectiveVisible(*object) || m_document->effectiveLocked(*object))
        return fail(tr("Hidden or locked objects cannot be selected"));
    const CanvasObjectId id = object->id;
    const CanvasLayerId layer = object->layer;
    QVector<CanvasObjectId> next = toggle ? m_selectionModel->selection() : QVector<CanvasObjectId>{};
    const bool adding = !next.contains(id);
    if (adding) next.append(id);
    else next.removeAll(id);
    m_selecting = true;
    m_selectionModel->setSelection(next);
    m_selecting = false;
    if (adding) m_document->setActiveLayer(layer);
    return succeed();
}

bool CanvasLayerTreeModel::activate(const QString &key)
{
    const Node *node = find(key);
    if (!node || !m_document) return fail(tr("The row no longer exists"));
    if (node->kind == Kind::Group) return fail(tr("A layer group cannot be the active layer"));
    CanvasLayerId layer = node->id;
    if (node->kind == Kind::Object) {
        const CanvasObject *object = m_document->object(node->id);
        if (!object) return fail(tr("The object no longer exists"));
        layer = object->layer;
    }
    m_document->setActiveLayer(layer);
    return succeed();
}

bool CanvasLayerTreeModel::setShown(const QString &key, bool shown)
{
    const Node *node = find(key);
    if (!node || !m_document) return fail(tr("The row no longer exists"));
    if (node->shown == shown) return false;
    const bool ok = node->kind == Kind::Object ? m_document->setObjectVisible(node->id, shown)
                                               : m_document->setLayerVisible(node->id, shown);
    return ok ? succeed() : fail(tr("The visibility change was refused"));
}

bool CanvasLayerTreeModel::setLocked(const QString &key, bool locked)
{
    const Node *node = find(key);
    if (!node || !m_document) return fail(tr("The row no longer exists"));
    if (node->locked == locked) return false;
    const bool ok = node->kind == Kind::Object ? m_document->setObjectLocked(node->id, locked)
                                               : m_document->setLayerLocked(node->id, locked);
    return ok ? succeed() : fail(tr("The lock change was refused"));
}

bool CanvasLayerTreeModel::rename(const QString &key, const QString &name)
{
    const Node *node = find(key);
    if (!node || !m_document) return fail(tr("The row no longer exists"));
    if (node->kind != Kind::Object) {
        const CanvasLayer *layer = m_document->layer(node->id);
        if (!layer) return fail(tr("The layer no longer exists"));
        if (layer->name == name) return false;
        return m_document->renameLayer(node->id, name) ? succeed() : fail(m_document->lastError());
    }
    const CanvasObject *object = m_document->object(node->id);
    if (!object) return fail(tr("The object no longer exists"));
    if (object->name == name) return false;
    CanvasObject renamed = *object;
    renamed.name = name;
    CanvasEdit edit;
    edit.label = tr("Rename object");
    edit.changes.append({renamed.id, *object, renamed});
    if (const OperationError error = m_document->commitEdit(edit)) return fail(error.message);
    return succeed();
}

bool CanvasLayerTreeModel::resolveMove(const QString &key, const QString &targetKey,
                                       const QString &placement, Move *plan) const
{
    const Node *node = find(key);
    const Node *target = find(targetKey);
    if (!m_document || !node || !target || node == target) return false;
    const bool above = placement == QLatin1String("above");
    const bool inside = placement == QLatin1String("inside");
    if (!above && !inside && placement != QLatin1String("below")) return false;
    if (node->kind == Kind::Object) {
        const CanvasObject *object = m_document->object(node->id);
        if (!object || m_document->effectiveLocked(*object)) return false;
        CanvasLayerId layer = 0;
        int index = 0;
        if (target->kind == Kind::Layer) { // any placement on a layer row: on top of it
            layer = target->id;
            QVector<CanvasObjectId> stack = m_document->objectsOnLayer(layer);
            stack.removeAll(node->id);
            index = int(stack.size());
        } else if (target->kind == Kind::Object && !inside) {
            const CanvasObject *anchor = m_document->object(target->id);
            if (!anchor) return false;
            layer = anchor->layer;
            QVector<CanvasObjectId> stack = m_document->objectsOnLayer(layer);
            stack.removeAll(node->id);
            index = int(stack.indexOf(target->id)) + (above ? 1 : 0);
        } else {
            return false;
        }
        if (layer != object->layer && m_document->layerEffectivelyLocked(layer)) return false;
        *plan = {false, node->id, layer, index};
        return true;
    }
    if (target->kind == Kind::Object) return false;
    CanvasLayerId parent = 0;
    int index = 0;
    if (inside) {
        if (target->kind != Kind::Group) return false;
        parent = target->id;
        QVector<CanvasLayerId> siblings = m_document->childLayers(parent);
        siblings.removeAll(node->id);
        index = int(siblings.size()); // the top row of the group
    } else {
        const CanvasLayer *anchor = m_document->layer(target->id);
        if (!anchor) return false;
        parent = anchor->parent;
        QVector<CanvasLayerId> siblings = m_document->childLayers(parent);
        siblings.removeAll(node->id);
        index = int(siblings.indexOf(target->id)) + (above ? 1 : 0);
    }
    // A group never moves into itself or below itself (the document refuses
    // it too; checking here keeps drop feedback honest).
    if (parent != 0 && (parent == node->id || m_document->isLayerAncestor(node->id, parent)))
        return false;
    *plan = {true, node->id, parent, index};
    return true;
}

bool CanvasLayerTreeModel::canMove(const QString &key, const QString &targetKey,
                                   const QString &placement) const
{
    Move plan;
    return resolveMove(key, targetKey, placement, &plan);
}

bool CanvasLayerTreeModel::move(const QString &key, const QString &targetKey, const QString &placement)
{
    Move plan;
    if (!resolveMove(key, targetKey, placement, &plan)) return fail(tr("The row cannot be placed there"));
    const bool ok = plan.layer ? m_document->moveLayer(plan.id, plan.parent, plan.index)
                               : m_document->moveObjectToLayer(plan.id, plan.parent, plan.index);
    if (ok) return succeed();
    return m_document->lastError().isEmpty() ? false : fail(m_document->lastError());
}

bool CanvasLayerTreeModel::step(const QString &key, int rows)
{
    const Node *node = find(key);
    if (!node || !m_document || rows == 0) return false;
    if (node->kind == Kind::Object) {
        const CanvasObject *object = m_document->object(node->id);
        if (!object) return fail(tr("The object no longer exists"));
        const QVector<CanvasObjectId> stack = m_document->objectsOnLayer(object->layer);
        const int at = int(stack.indexOf(object->id));
        const int to = std::clamp(at - rows, 0, int(stack.size()) - 1);
        if (to == at) return false;
        return m_document->moveObjectToLayer(object->id, object->layer, to)
            ? succeed() : fail(m_document->lastError());
    }
    const CanvasLayer *layer = m_document->layer(node->id);
    if (!layer) return fail(tr("The layer no longer exists"));
    const CanvasLayerId parent = layer->parent;
    const QVector<CanvasLayerId> siblings = m_document->childLayers(parent);
    const int at = int(siblings.indexOf(node->id));
    const int to = std::clamp(at - rows, 0, int(siblings.size()) - 1);
    if (to == at) return false;
    return m_document->moveLayer(node->id, parent, to) ? succeed() : fail(m_document->lastError());
}

bool CanvasLayerTreeModel::indent(const QString &key)
{
    const Node *node = find(key);
    const CanvasLayer *layer = node && m_document && node->kind != Kind::Object
        ? m_document->layer(node->id) : nullptr;
    if (!layer) return false;
    const QVector<CanvasLayerId> siblings = m_document->childLayers(layer->parent);
    const qsizetype at = siblings.indexOf(node->id);
    // The row shown directly above is the next sibling in paint order.
    const CanvasLayer *above = at + 1 < siblings.size() ? m_document->layer(siblings.at(at + 1)) : nullptr;
    if (!above || !above->group) return fail(tr("Indenting needs a layer group directly above"));
    // Bottom of that group: its last row, so the row keeps its place on screen.
    return m_document->moveLayer(node->id, above->id, 0) ? succeed() : fail(m_document->lastError());
}

bool CanvasLayerTreeModel::outdent(const QString &key)
{
    const Node *node = find(key);
    if (!node || !m_document || node->kind == Kind::Object) return false;
    const CanvasLayer *layer = m_document->layer(node->id);
    const CanvasLayer *group = layer ? m_document->layer(layer->parent) : nullptr;
    if (!group) return false;
    const QVector<CanvasLayerId> siblings = m_document->childLayers(group->parent);
    const int index = int(siblings.indexOf(group->id)) + 1; // just above its old group
    return m_document->moveLayer(node->id, group->parent, index) ? succeed()
                                                                 : fail(m_document->lastError());
}

QString CanvasLayerTreeModel::addLayer(const QString &name, const QString &nearKey, bool group)
{
    if (!m_document) {
        fail(tr("No canvas document"));
        return {};
    }
    CanvasLayerId parent = 0;
    int index = int(m_document->childLayers(0).size());
    if (const Node *near = find(nearKey)) {
        CanvasLayerId anchor = near->id;
        if (near->kind == Kind::Object) {
            const CanvasObject *object = m_document->object(near->id);
            anchor = object ? object->layer : 0;
        }
        if (const CanvasLayer *layer = m_document->layer(anchor)) {
            parent = layer->parent;
            index = int(m_document->childLayers(parent).indexOf(anchor)) + 1;
        }
    }
    const CanvasLayerId id = m_document->addLayerAt(name, group, parent, index);
    if (!id) {
        fail(m_document->lastError());
        return {};
    }
    if (!group) m_document->setActiveLayer(id);
    succeed();
    return layerKey(id);
}

QString CanvasLayerTreeModel::groupLayers(const QStringList &keys, const QString &name)
{
    QVector<CanvasLayerId> ids;
    for (const QString &key : keys) {
        const Node *node = find(key);
        if (node && node->kind != Kind::Object) ids.append(node->id);
    }
    if (!m_document || ids.isEmpty()) {
        fail(tr("Choose layers to group"));
        return {};
    }
    const CanvasLayerId id = m_document->groupLayers(ids, name);
    if (!id) {
        fail(m_document->lastError());
        return {};
    }
    succeed();
    return layerKey(id);
}

bool CanvasLayerTreeModel::ungroup(const QString &key)
{
    const Node *node = find(key);
    if (!node || !m_document || node->kind != Kind::Group) return fail(tr("Choose a layer group"));
    return m_document->ungroupLayer(node->id) ? succeed() : fail(m_document->lastError());
}

bool CanvasLayerTreeModel::remove(const QString &key)
{
    const Node *node = find(key);
    if (!node || !m_document) return fail(tr("The row no longer exists"));
    if (node->kind != Kind::Object)
        return m_document->removeLayerTree(node->id) ? succeed() : fail(m_document->lastError());
    const CanvasObject *object = m_document->object(node->id);
    if (!object) return fail(tr("The object no longer exists"));
    if (m_document->effectiveLocked(*object)) return fail(tr("A locked object cannot be deleted"));
    return m_document->removeObject(node->id) ? succeed() : fail(tr("The object could not be deleted"));
}

QStringList CanvasLayerTreeModel::ancestorKeys(const QString &key) const
{
    QStringList keys;
    const Node *node = find(key);
    for (int parent = node ? node->parent : -1; parent >= 0; parent = m_nodes.at(parent).parent)
        keys.prepend(m_nodes.at(parent).key);
    return keys;
}

QStringList CanvasLayerTreeModel::groupKeys() const
{
    QStringList keys;
    for (const Node &node : m_nodes)
        if (node.kind == Kind::Group) keys << node.key;
    return keys;
}

QVariantMap CanvasLayerTreeModel::nodeInfo(const QString &key) const
{
    const auto found = m_byKey.constFind(key);
    if (found == m_byKey.cend()) return {};
    const QModelIndex at = indexOf(found.value());
    QVariantMap info;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        info.insert(QString::fromUtf8(it.value()), data(at, it.key()));
    info.insert(QStringLiteral("row"), at.row());
    return info;
}

QString CanvasLayerTreeModel::keyForObject(const QString &objectId) const
{
    const QString key = QStringLiteral("object:") + objectId;
    return m_byKey.contains(key) ? key : QString{};
}

QString CanvasLayerTreeModel::keyForLayer(const QString &layerId) const
{
    const QString key = QStringLiteral("layer:") + layerId;
    return m_byKey.contains(key) ? key : QString{};
}

} // namespace QindaTK
