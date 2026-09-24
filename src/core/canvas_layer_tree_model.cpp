// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_layer_tree_model.h"

#include <algorithm>

namespace QindaTK {
namespace {
QString layerKey(quint64 id) { return QStringLiteral("layer:%1").arg(id); }
QString objectKey(quint64 id) { return QStringLiteral("object:%1").arg(id); }

QString shapeIcon(CanvasShape shape)
{
    switch (shape) {
    case CanvasShape::Rect: return QStringLiteral("square");
    case CanvasShape::Ellipse: return QStringLiteral("circle");
    case CanvasShape::Polyline: return QStringLiteral("spline");
    case CanvasShape::Image: return QStringLiteral("image");
    case CanvasShape::Path: return QStringLiteral("pen-tool");
    }
    return QStringLiteral("square");
}

QString shapeLabel(CanvasShape shape)
{
    switch (shape) {
    case CanvasShape::Rect: return CanvasLayerTreeModel::tr("Rectangle");
    case CanvasShape::Ellipse: return CanvasLayerTreeModel::tr("Ellipse");
    case CanvasShape::Polyline: return CanvasLayerTreeModel::tr("Polyline");
    case CanvasShape::Image: return CanvasLayerTreeModel::tr("Image");
    case CanvasShape::Path: return CanvasLayerTreeModel::tr("Path");
    }
    return CanvasLayerTreeModel::tr("Object");
}

const QList<int> &dataRoles()
{
    // Never KeyRole: VirtualCollectionAdapter re-indexes on an id-role change.
    static const QList<int> roles{
        CanvasLayerTreeModel::KindRole, CanvasLayerTreeModel::ItemIdRole,
        CanvasLayerTreeModel::LabelRole, CanvasLayerTreeModel::DetailRole,
        CanvasLayerTreeModel::IconNameRole, CanvasLayerTreeModel::ShownRole,
        CanvasLayerTreeModel::LockedRole, CanvasLayerTreeModel::EffectiveVisibleRole,
        CanvasLayerTreeModel::EffectiveLockedRole, CanvasLayerTreeModel::SelectedRole,
        CanvasLayerTreeModel::ActiveRole, CanvasLayerTreeModel::ChildCountRole,
        CanvasLayerTreeModel::ParentKeyRole};
    return roles;
}
} // namespace

CanvasLayerTreeModel::CanvasLayerTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

void CanvasLayerTreeModel::setDocument(CanvasDocument *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &CanvasDocument::changed, this, &CanvasLayerTreeModel::rebuild);
        connect(document, &QObject::destroyed, this, [this] {
            rebuild();
            emit documentChanged();
        });
        if (m_selectionModel && !m_selectionModel->document()) m_selectionModel->setDocument(document);
    }
    rebuild();
    emit documentChanged();
}

void CanvasLayerTreeModel::setSelectionModel(CanvasSelectionModel *model)
{
    if (m_selectionModel == model) return;
    if (m_selectionModel) disconnect(m_selectionModel, nullptr, this, nullptr);
    m_selectionModel = model;
    if (model) {
        connect(model, &CanvasSelectionModel::selectionChanged, this,
                &CanvasLayerTreeModel::onSelectionChanged);
        connect(model, &QObject::destroyed, this, &CanvasLayerTreeModel::onSelectionChanged);
        if (m_document && !model->document()) model->setDocument(m_document);
    }
    onSelectionChanged();
    emit selectionModelChanged();
}

void CanvasLayerTreeModel::setShowObjects(bool show)
{
    if (m_showObjects == show) return;
    m_showObjects = show;
    rebuild();
    emit showObjectsChanged();
}

QString CanvasLayerTreeModel::activeKey() const { return m_activeKey; }

QStringList CanvasLayerTreeModel::selectedKeys() const
{
    QStringList keys;
    if (!m_selectionModel) return keys;
    for (const CanvasObjectId id : m_selectionModel->selection()) keys << objectKey(id);
    return keys;
}

QModelIndex CanvasLayerTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0) return {};
    if (parent.isValid() && parent.internalId() >= quintptr(m_nodes.size())) return {};
    const QVector<int> &list = parent.isValid() ? m_nodes.at(int(parent.internalId())).children : m_roots;
    return row < list.size() ? createIndex(row, 0, quintptr(list.at(row))) : QModelIndex{};
}

QModelIndex CanvasLayerTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() >= quintptr(m_nodes.size())) return {};
    return indexOf(m_nodes.at(int(child.internalId())).parent);
}

int CanvasLayerTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) return 0;
    if (!parent.isValid()) return int(m_roots.size());
    if (parent.internalId() >= quintptr(m_nodes.size())) return 0;
    return int(m_nodes.at(int(parent.internalId())).children.size());
}

int CanvasLayerTreeModel::columnCount(const QModelIndex &parent) const
{
    return parent.column() > 0 ? 0 : 1;
}

QVariant CanvasLayerTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.internalId() >= quintptr(m_nodes.size())) return {};
    const Node &node = m_nodes.at(int(index.internalId()));
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole: return node.label;
    case KeyRole: return node.key;
    case KindRole:
        return node.kind == Kind::Group ? QStringLiteral("group")
             : node.kind == Kind::Layer ? QStringLiteral("layer") : QStringLiteral("object");
    case ItemIdRole: return QString::number(node.id);
    case DetailRole: return node.detail;
    case IconNameRole: return node.iconName;
    case ShownRole: return node.shown;
    case LockedRole: return node.locked;
    case EffectiveVisibleRole: return node.effectiveVisible;
    case EffectiveLockedRole: return node.effectiveLocked;
    case SelectedRole: return node.selected;
    case ActiveRole: return node.active;
    case ChildCountRole: return int(node.children.size());
    case ParentKeyRole: return node.parent < 0 ? QString() : m_nodes.at(node.parent).key;
    default: return {};
    }
}

QHash<int, QByteArray> CanvasLayerTreeModel::roleNames() const
{
    return {{KeyRole, "key"}, {KindRole, "kind"}, {ItemIdRole, "itemId"}, {LabelRole, "label"},
            {DetailRole, "detail"}, {IconNameRole, "iconName"}, {ShownRole, "shown"},
            {LockedRole, "locked"}, {EffectiveVisibleRole, "effectiveVisible"},
            {EffectiveLockedRole, "effectiveLocked"}, {SelectedRole, "selected"},
            {ActiveRole, "active"}, {ChildCountRole, "childCount"}, {ParentKeyRole, "parentKey"}};
}

QModelIndex CanvasLayerTreeModel::indexOf(int node) const
{
    if (node < 0 || node >= m_nodes.size()) return {};
    return createIndex(m_nodes.at(node).row, 0, quintptr(node));
}

const CanvasLayerTreeModel::Node *CanvasLayerTreeModel::find(const QString &key) const
{
    const auto found = m_byKey.constFind(key);
    return found == m_byKey.cend() ? nullptr : &m_nodes.at(found.value());
}

CanvasLayerTreeModel::Node CanvasLayerTreeModel::objectNode(const CanvasObject &object, int parent,
                                                            int row) const
{
    Node node;
    node.key = objectKey(object.id);
    node.kind = Kind::Object;
    node.id = object.id;
    node.parent = parent;
    node.row = row;
    node.detail = shapeLabel(object.shape);
    node.label = object.name.isEmpty() ? QStringLiteral("%1 %2").arg(node.detail).arg(object.id)
                                       : object.name;
    node.iconName = shapeIcon(object.shape);
    node.shown = object.visible;
    node.locked = object.locked;
    node.effectiveVisible = m_document->effectiveVisible(object);
    node.effectiveLocked = m_document->effectiveLocked(object);
    node.selected = m_selected.contains(object.id);
    return node;
}

void CanvasLayerTreeModel::appendLayers(QVector<Node> &nodes, QVector<int> &siblings, int parent,
                                        CanvasLayerId parentLayer,
                                        const QHash<CanvasLayerId, QVector<CanvasObjectId>> &objects,
                                        int depth) const
{
    if (depth > 256) return; // validated documents nest at most maximumLayerDepth
    const QVector<CanvasLayerId> layers = m_document->childLayers(parentLayer);
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) { // front-most first
        const CanvasLayer *layer = m_document->layer(*it);
        if (!layer) continue;
        Node node;
        node.key = layerKey(layer->id);
        node.kind = layer->group ? Kind::Group : Kind::Layer;
        node.id = layer->id;
        node.parent = parent;
        node.row = int(siblings.size());
        node.label = layer->name;
        node.iconName = layer->group ? QStringLiteral("folder") : QStringLiteral("layers");
        node.shown = layer->visible;
        node.locked = layer->locked;
        node.effectiveVisible = m_document->layerEffectivelyVisible(layer->id);
        node.effectiveLocked = m_document->layerEffectivelyLocked(layer->id);
        node.active = !layer->group && layer->id == m_document->activeLayer();
        const int at = int(nodes.size());
        nodes.append(node);
        siblings.append(at);
        QVector<int> children;
        if (layer->group) {
            appendLayers(nodes, children, at, layer->id, objects, depth + 1);
            nodes[at].detail = tr("%n layer(s)", nullptr, int(children.size()));
        } else {
            const QVector<CanvasObjectId> ids = objects.value(layer->id);
            nodes[at].detail = tr("%n object(s)", nullptr, int(ids.size()));
            for (auto id = ids.crbegin(); m_showObjects && id != ids.crend(); ++id) {
                if (const CanvasObject *object = m_document->object(*id)) {
                    children.append(int(nodes.size()));
                    nodes.append(objectNode(*object, at, int(children.size()) - 1));
                }
            }
        }
        nodes[at].children = children;
    }
}

void CanvasLayerTreeModel::rebuild()
{
    m_selected.clear();
    if (m_selectionModel)
        for (const CanvasObjectId id : m_selectionModel->selection()) m_selected.insert(id);
    QVector<Node> nodes;
    QVector<int> roots;
    if (m_document) appendLayers(nodes, roots, -1, 0, m_document->objectsByLayer(), 0);
    bool same = nodes.size() == m_nodes.size();
    for (qsizetype i = 0; same && i < nodes.size(); ++i)
        same = nodes.at(i).key == m_nodes.at(i).key && nodes.at(i).parent == m_nodes.at(i).parent;
    if (!same) {
        beginResetModel();
        m_nodes = std::move(nodes);
        m_roots = std::move(roots);
        m_byKey.clear();
        for (qsizetype i = 0; i < m_nodes.size(); ++i) m_byKey.insert(m_nodes.at(i).key, int(i));
        endResetModel();
        emit structureChanged();
    } else {
        // Same rows: one dataChanged per parent over the rows that changed,
        // so a visibility toggle never resets the view or its expansion.
        QHash<int, QPair<int, int>> ranges;
        for (qsizetype i = 0; i < nodes.size(); ++i) {
            const Node &a = nodes.at(i);
            const Node &b = m_nodes.at(i);
            if (a.label == b.label && a.detail == b.detail && a.iconName == b.iconName
                && a.shown == b.shown && a.locked == b.locked
                && a.effectiveVisible == b.effectiveVisible && a.effectiveLocked == b.effectiveLocked
                && a.selected == b.selected && a.active == b.active)
                continue;
            const auto range = ranges.find(a.parent);
            if (range == ranges.end()) {
                ranges.insert(a.parent, {a.row, a.row});
            } else {
                range->first = std::min(range->first, a.row);
                range->second = std::max(range->second, a.row);
            }
        }
        m_nodes = std::move(nodes);
        for (auto it = ranges.cbegin(); it != ranges.cend(); ++it) {
            const QModelIndex parent = indexOf(it.key());
            emit dataChanged(index(it->first, 0, parent), index(it->second, 0, parent), dataRoles());
        }
    }
    const QString active = m_document ? layerKey(m_document->activeLayer()) : QString{};
    if (active != m_activeKey) {
        m_activeKey = active;
        emit activeKeyChanged();
    }
    emit selectedKeysChanged();
}

void CanvasLayerTreeModel::onSelectionChanged()
{
    const QVector<CanvasObjectId> selection = m_selectionModel ? m_selectionModel->selection()
                                                               : QVector<CanvasObjectId>{};
    QSet<CanvasObjectId> next;
    for (const CanvasObjectId id : selection) next.insert(id);
    QVector<CanvasObjectId> touched;
    for (const CanvasObjectId id : std::as_const(m_selected))
        if (!next.contains(id)) touched.append(id);
    QString reveal;
    for (const CanvasObjectId id : selection) {
        if (m_selected.contains(id)) continue;
        touched.append(id);
        // AGENT-GUARD (D-094): reveal only selections made elsewhere; the
        // tree's own clicks already show their row, and scrolling to them
        // would move the list under the pointer.
        if (!m_selecting && reveal.isEmpty()) reveal = objectKey(id);
    }
    m_selected = next;
    for (const CanvasObjectId id : std::as_const(touched)) {
        const int node = m_byKey.value(objectKey(id), -1);
        if (node < 0) continue;
        m_nodes[node].selected = m_selected.contains(id);
        const QModelIndex at = indexOf(node);
        emit dataChanged(at, at, {SelectedRole});
    }
    emit selectedKeysChanged();
    if (!reveal.isEmpty() && m_byKey.contains(reveal)) emit revealRequested(reveal);
}

} // namespace QindaTK
