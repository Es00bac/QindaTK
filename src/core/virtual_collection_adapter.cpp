// SPDX-License-Identifier: LGPL-3.0-or-later
#include "virtual_collection_adapter.h"

namespace QindaTK {
namespace {

QString normalizedId(const QString &id)
{
    const QUuid uuid(id);
    return uuid.isNull() ? id : uuid.toString(QUuid::WithoutBraces);
}

} // namespace

VirtualCollectionAdapter::VirtualCollectionAdapter(QObject *parent) : QAbstractListModel(parent)
{
    reconnect();
}

void VirtualCollectionAdapter::setModel(QAbstractItemModel *model)
{
    if (m_model == model) return;
    m_model = model;
    if (!m_identityModelExplicit) m_identityModel = model;
    reconnect();
    rebuild(true);
    emit modelChanged();
    if (!m_identityModelExplicit) emit identityModelChanged();
}

void VirtualCollectionAdapter::setIdentityModel(QAbstractItemModel *model)
{
    if (m_identityModel == model && m_identityModelExplicit) return;
    m_identityModelExplicit = true;
    m_identityModel = model;
    reconnect();
    emit identityModelChanged();
}

void VirtualCollectionAdapter::setSelectionModel(SelectionModel *selection)
{
    if (m_selection == selection) return;
    m_selection = selection;
    emit selectionModelChanged();
}

void VirtualCollectionAdapter::setIdRole(const QString &name)
{
    const QString normalized = name.trimmed();
    if (normalized.isEmpty() || m_idRoleName == normalized) return;
    m_idRoleName = normalized;
    rebuild(false);
    emit idRoleChanged();
}

int VirtualCollectionAdapter::count() const
{
    return m_model ? m_model->rowCount() : 0;
}

int VirtualCollectionAdapter::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleIndexes.size();
}

QVariant VirtualCollectionAdapter::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleIndexes.size()) return {};
    const auto sourceIndex = QModelIndex(m_visibleIndexes[index.row()]);
    if (!sourceIndex.isValid() || !m_model) return {};
    if (role == m_treeDepthRole) return m_visibleDepths[index.row()];
    const QString id = idForIndex(sourceIndex);
    if (role == m_treeHasChildrenRole) return m_model->hasChildren(sourceIndex) || m_model->canFetchMore(sourceIndex);
    if (role == m_treeExpandedRole) return m_expandedIds.contains(id);
    if (role == m_treeLoadingRole) return false;
    if (role == m_treeParentIdRole)
        return sourceIndex.parent().isValid() ? idForIndex(sourceIndex.parent()) : QString{};
    return sourceIndex.data(role);
}

QHash<int, QByteArray> VirtualCollectionAdapter::roleNames() const
{
    QHash<int, QByteArray> roles = m_model ? m_model->roleNames() : QHash<int, QByteArray>{};
    roles.insert(m_treeDepthRole, QByteArrayLiteral("treeDepth"));
    roles.insert(m_treeHasChildrenRole, QByteArrayLiteral("treeHasChildren"));
    roles.insert(m_treeExpandedRole, QByteArrayLiteral("treeExpanded"));
    roles.insert(m_treeParentIdRole, QByteArrayLiteral("treeParentId"));
    roles.insert(m_treeLoadingRole, QByteArrayLiteral("treeLoading"));
    return roles;
}

int VirtualCollectionAdapter::roleForName(const QAbstractItemModel *model, const QString &name) const
{
    if (!model) return -1;
    const auto roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (QString::fromUtf8(it.value()) == name) return it.key();
    return -1;
}

QString VirtualCollectionAdapter::idAt(int row) const
{
    return row >= 0 && row < m_rootIds.size() ? m_rootIds[row] : QString{};
}

QString VirtualCollectionAdapter::treeIdAt(int row) const
{
    return row >= 0 && row < m_visibleIndexes.size() ? idForIndex(m_visibleIndexes[row]) : QString{};
}

int VirtualCollectionAdapter::treeIndexOfId(const QString &id) const
{
    return m_visibleRows.value(normalizedId(id), -1);
}

QVariant VirtualCollectionAdapter::treeDataAt(int row, const QString &roleName) const
{
    return data(index(row, 0), roleForName(this, roleName));
}

QString VirtualCollectionAdapter::idForIndex(const QModelIndex &index) const
{
    if (!m_model || !index.isValid() || index.model() != m_model) return {};
    const int role = roleForName(m_model, m_idRoleName);
    return role < 0 ? QString{} : normalizedId(index.data(role).toString());
}

QVariant VirtualCollectionAdapter::dataForId(const QString &id, const QString &roleName) const
{
    const QModelIndex index = indexForId(id);
    const int role = roleForName(m_model, roleName);
    return index.isValid() && role >= 0 ? index.data(role) : QVariant{};
}

QModelIndex VirtualCollectionAdapter::indexForId(const QString &id) const
{
    const auto found = m_indexes.constFind(normalizedId(id));
    if (found == m_indexes.cend()) return {};
    return QModelIndex(found.value());
}

QVariant VirtualCollectionAdapter::dataAt(int row, const QString &roleName) const
{
    if (!m_model || row < 0 || row >= count()) return {};
    const int role = roleForName(m_model, roleName);
    return role < 0 ? QVariant{} : m_model->index(row, 0).data(role);
}

int VirtualCollectionAdapter::indexOfId(const QString &id) const
{
    const auto found = m_indexes.constFind(normalizedId(id));
    if (found == m_indexes.cend() || !found.value().isValid() || found.value().parent().isValid())
        return -1;
    return found.value().row();
}

QStringList VirtualCollectionAdapter::rangeIds(int firstRow, int lastRow) const
{
    QStringList result;
    if (!m_model || count() == 0) return result;
    const int first = qBound(0, qMin(firstRow, lastRow), count() - 1);
    const int last = qBound(0, qMax(firstRow, lastRow), count() - 1);
    // AGENT-GUARD: range selection must never copy the full model into JS.
    // The hard ceiling protects misuse on enormous ranges; caller may page it.
    if (last - first >= 4096) return result;
    result.reserve(last - first + 1);
    for (int row = first; row <= last; ++row) result.push_back(idAt(row));
    return result;
}

bool VirtualCollectionAdapter::canFetchMoreForId(const QString &parentId) const
{
    if (!m_model) return false;
    const QModelIndex parent = indexForId(parentId);
    return parent.isValid() && m_model->canFetchMore(parent);
}

bool VirtualCollectionAdapter::fetchMoreForId(const QString &parentId)
{
    if (!canFetchMoreForId(parentId)) return false;
    const QModelIndex parent = indexForId(parentId);
    if (!parent.isValid()) return false;
    m_model->fetchMore(parent);
    return true;
}

bool VirtualCollectionAdapter::canFetchMore() const
{
    return m_model && m_model->canFetchMore(QModelIndex{});
}

bool VirtualCollectionAdapter::fetchMore()
{
    if (!canFetchMore()) return false;
    m_model->fetchMore(QModelIndex{});
    return true;
}

bool VirtualCollectionAdapter::setExpanded(const QString &id, bool expanded)
{
    const QString key = normalizedId(id);
    const QModelIndex parent = indexForId(key);
    if (!parent.isValid()) return false;
    if (expanded == m_expandedIds.contains(key)) return true;
    if (expanded) m_expandedIds.insert(key);
    else m_expandedIds.remove(key);
    if (expanded) {
        if (m_model->canFetchMore(parent)) m_model->fetchMore(parent);
        else insertVisibleSubtree(parent, 0, m_model->rowCount(parent) - 1);
    } else removeVisibleDescendants(parent);
    const int row = treeIndexOfId(key);
    if (row >= 0) emit dataChanged(index(row), index(row), {m_treeExpandedRole});
    emit changed();
    return true;
}

void VirtualCollectionAdapter::captureRemoval(QAbstractItemModel *source, int first, int last,
                                               const QModelIndex &parent)
{
    if (!source) return;
    const int role = roleForName(source, m_idRoleName);
    m_removing.clear();
    if (role < 0) return;
    constexpr int maxRemovalNodes = 1'000'000;
    constexpr int maxTreeDepth = 256;
    QVector<QPair<QModelIndex, int>> pending;
    if (last - first + 1 > maxRemovalNodes) {
        emit invalidModel(QStringLiteral("Removed range exceeds the bounded identity scan"));
        return;
    }
    if (!parent.isValid()) m_removing.reserve(last - first + 1);
    for (int row = last; row >= first; --row)
        pending.push_back({source->index(row, 0, parent), parent.isValid() ? 1 : 0});
    while (!pending.isEmpty()) {
        const auto [node, depth] = pending.takeLast();
        if (depth > maxTreeDepth || m_removing.size() >= maxRemovalNodes) {
            m_removing.clear();
            emit invalidModel(QStringLiteral("Removed subtree exceeds the bounded identity scan"));
            return;
        }
        const QString id = normalizedId(node.data(role).toString());
        if (!id.isEmpty()) m_removing.push_back(id);
        if (depth == maxTreeDepth && source->rowCount(node) > 0) {
            m_removing.clear();
            emit invalidModel(QStringLiteral("Removed subtree exceeds the bounded identity depth"));
            return;
        }
        for (int child = source->rowCount(node) - 1; child >= 0; --child) {
            if (pending.size() + m_removing.size() >= maxRemovalNodes) {
                m_removing.clear();
                emit invalidModel(QStringLiteral("Removed subtree exceeds the bounded identity scan"));
                return;
            }
            pending.push_back({source->index(child, 0, node), depth + 1});
        }
    }
}

void VirtualCollectionAdapter::reconnect()
{
    for (const auto &connection : std::as_const(m_modelConnections)) disconnect(connection);
    for (const auto &connection : std::as_const(m_identityConnections)) disconnect(connection);
    m_modelConnections.clear();
    m_identityConnections.clear();
    if (m_model) {
        m_modelConnections.push_back(connect(m_model, &QObject::destroyed, this, [this] {
            m_model = nullptr;
            if (!m_identityModel) {
                m_identityModelExplicit = false;
                emit identityModelChanged();
            }
            reconnect();
            rebuild(true);
            emit modelChanged();
        }));
        m_modelConnections.push_back(connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
            rebuild(true);
        }));
        m_modelConnections.push_back(connect(m_model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &parent, int first, int last) {
                if (!parent.isValid()) { rebuild(true); return; }
                if (!indexInsertedSubtree(parent, first, last)) { rebuild(true); return; }
                insertVisibleSubtree(parent, first, last);
                ++m_generation;
                emit changed();
            }));
        m_modelConnections.push_back(connect(m_model, &QAbstractItemModel::rowsMoved, this,
                                        [this] { rebuild(true); }));
        m_modelConnections.push_back(connect(m_model, &QAbstractItemModel::layoutChanged, this,
                                             [this] { rebuild(true); }));
        m_modelConnections.push_back(connect(m_model, &QAbstractItemModel::rowsRemoved, this,
                                             [this] { rebuild(true); }));
        m_modelConnections.push_back(connect(m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                const int idRole = roleForName(m_model, m_idRoleName);
                if (roles.isEmpty() || roles.contains(idRole)) rebuild(false);
                else {
                    if (!m_visibleIndexes.isEmpty())
                        emit dataChanged(index(0), index(m_visibleIndexes.size() - 1), roles);
                    emit changed();
                }
            }));
    }
    if (m_identityModel) {
        m_identityConnections.push_back(connect(m_identityModel, &QObject::destroyed, this, [this] {
            m_identityModel = nullptr;
            if (!m_model) m_identityModelExplicit = false;
            reconnect();
            emit identityModelChanged();
        }));
        m_identityConnections.push_back(connect(m_identityModel, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex &parent, int first, int last) {
                captureRemoval(m_identityModel, first, last, parent);
            }));
        m_identityConnections.push_back(connect(m_identityModel, &QAbstractItemModel::rowsRemoved, this,
            [this] {
                if (!m_removing.isEmpty()) {
                    if (m_selection) m_selection->removeIds(m_removing);
                    emit identityRemoved(m_removing);
                    m_removing.clear();
                }
                if (m_identityModel != m_model) rebuild(true);
            }));
        if (m_identityModel != m_model) {
            m_identityConnections.push_back(connect(m_identityModel, &QAbstractItemModel::modelReset, this,
                                                    [this] { emit changed(); }));
            m_identityConnections.push_back(connect(m_identityModel, &QAbstractItemModel::rowsInserted, this,
                                                    [this] { emit changed(); }));
        }
    }
}

void VirtualCollectionAdapter::rebuild(bool advanceGeneration)
{
    m_indexes.clear();
    m_rootIds.clear();
    m_valid = true;
    if (m_model) {
        const int role = roleForName(m_model, m_idRoleName);
        if (role < 0) {
            m_valid = false;
        } else {
            int nextRole = Qt::UserRole + 1;
            const auto sourceRoles = m_model->roleNames();
            for (auto it = sourceRoles.cbegin(); it != sourceRoles.cend(); ++it)
                nextRole = qMax(nextRole, it.key() + 1);
            m_treeDepthRole = nextRole++;
            m_treeHasChildrenRole = nextRole++;
            m_treeExpandedRole = nextRole++;
            m_treeParentIdRole = nextRole++;
            m_treeLoadingRole = nextRole;
            constexpr int maxIndexedNodes = 1'000'000;
            constexpr int maxTreeDepth = 256;
            m_indexes.reserve(qMin(m_model->rowCount(), maxIndexedNodes));
            const auto indexNode = [this, role](const QModelIndex &index) {
                const QString id = normalizedId(index.data(role).toString());
                if (id.isEmpty() || m_indexes.contains(id)) {
                    m_valid = false;
                    return false;
                }
                m_indexes.insert(id, QPersistentModelIndex(index));
                return true;
            };
            for (int row = 0; row < m_model->rowCount() && m_valid; ++row) {
                const QModelIndex index = m_model->index(row, 0);
                const QString id = normalizedId(index.data(role).toString());
                if (!indexNode(index)) break;
                m_rootIds.push_back(id);
            }
            // Walk child nodes iteratively so malformed/deep models cannot
            // exhaust the C++ call stack. Work is capped at one million IDs.
            struct PendingNode { QModelIndex index; int depth; };
            QVector<PendingNode> pending;
            for (int row = m_model->rowCount() - 1; row >= 0 && m_valid; --row)
                pending.push_back({m_model->index(row, 0), 0});
            while (!pending.isEmpty() && m_valid) {
                const PendingNode node = pending.takeLast();
                if (node.depth >= maxTreeDepth) {
                    if (m_model->rowCount(node.index) > 0) m_valid = false;
                    continue;
                }
                for (int child = m_model->rowCount(node.index) - 1; child >= 0; --child) {
                    if (m_indexes.size() >= maxIndexedNodes) { m_valid = false; break; }
                    const QModelIndex childIndex = m_model->index(child, 0, node.index);
                    if (!indexNode(childIndex)) break;
                    pending.push_back({childIndex, node.depth + 1});
                }
            }
            if (!m_valid) { m_indexes.clear(); m_rootIds.clear(); }
        }
    }
    rebuildProjection();
    for (auto it = m_expandedIds.begin(); it != m_expandedIds.end();) {
        if (!m_indexes.contains(*it)) it = m_expandedIds.erase(it);
        else ++it;
    }
    if (advanceGeneration) ++m_generation;
    if (!m_valid) emit invalidModel(QStringLiteral("Stable ID role is missing, empty, or duplicated"));
    emit changed();
}

void VirtualCollectionAdapter::rebuildProjection()
{
    QVector<QPersistentModelIndex> nextIndexes;
    QVector<int> nextDepths;
    constexpr int maxVisibleRows = 1'000'000;
    constexpr int maxTreeDepth = 256;
    struct PendingNode { QModelIndex index; int depth; };
    QVector<PendingNode> pending;
    if (m_model && m_valid) {
        for (int row = m_model->rowCount() - 1; row >= 0; --row)
            pending.push_back({m_model->index(row, 0), 0});
        while (!pending.isEmpty() && nextIndexes.size() < maxVisibleRows) {
            const auto node = pending.takeLast();
            nextIndexes.push_back(QPersistentModelIndex(node.index));
            nextDepths.push_back(node.depth);
            const QString id = idForIndex(node.index);
            if (!m_expandedIds.contains(id)) continue;
            if (node.depth >= maxTreeDepth) continue;
            for (int child = m_model->rowCount(node.index) - 1; child >= 0; --child)
                pending.push_back({m_model->index(child, 0, node.index), node.depth + 1});
        }
    }
    int prefix = 0;
    while (prefix < m_visibleIndexes.size() && prefix < nextIndexes.size()
           && m_visibleIndexes[prefix] == nextIndexes[prefix]
           && m_visibleDepths[prefix] == nextDepths[prefix]) ++prefix;
    int oldSuffix = m_visibleIndexes.size();
    int newSuffix = nextIndexes.size();
    while (oldSuffix > prefix && newSuffix > prefix
           && m_visibleIndexes[oldSuffix - 1] == nextIndexes[newSuffix - 1]
           && m_visibleDepths[oldSuffix - 1] == nextDepths[newSuffix - 1]) {
        --oldSuffix;
        --newSuffix;
    }
    const int removeCount = oldSuffix - prefix;
    const int insertCount = newSuffix - prefix;
    if (removeCount > 0) {
        beginRemoveRows({}, prefix, oldSuffix - 1);
        m_visibleIndexes.remove(prefix, removeCount);
        m_visibleDepths.remove(prefix, removeCount);
        endRemoveRows();
    }
    if (insertCount > 0) {
        beginInsertRows({}, prefix, prefix + insertCount - 1);
        for (int i = prefix; i < newSuffix; ++i) {
            m_visibleIndexes.insert(i, nextIndexes[i]);
            m_visibleDepths.insert(i, nextDepths[i]);
        }
        endInsertRows();
    }
    m_visibleRows.clear();
    for (int row = 0; row < m_visibleIndexes.size(); ++row)
        m_visibleRows.insert(idForIndex(m_visibleIndexes[row]), row);
    if (removeCount == 0 && insertCount == 0 && !m_visibleIndexes.isEmpty())
        emit dataChanged(index(0), index(m_visibleIndexes.size() - 1), {});
}

bool VirtualCollectionAdapter::indexInsertedSubtree(const QModelIndex &parent, int first, int last)
{
    if (!m_model || first < 0 || last < first) return false;
    const int role = roleForName(m_model, m_idRoleName);
    if (role < 0) return false;
    constexpr int maxNodes = 1'000'000;
    constexpr int maxDepth = 256;
    struct PendingNode { QModelIndex index; int depth; };
    QVector<PendingNode> pending;
    for (int row = last; row >= first; --row) pending.push_back({m_model->index(row, 0, parent), 1});
    while (!pending.isEmpty()) {
        const auto item = pending.takeLast();
        if (item.depth > maxDepth || m_indexes.size() >= maxNodes) return false;
        const QString id = normalizedId(item.index.data(role).toString());
        if (id.isEmpty() || m_indexes.contains(id)) return false;
        m_indexes.insert(id, QPersistentModelIndex(item.index));
        for (int child = m_model->rowCount(item.index) - 1; child >= 0; --child)
            pending.push_back({m_model->index(child, 0, item.index), item.depth + 1});
    }
    return true;
}

void VirtualCollectionAdapter::insertVisibleSubtree(const QModelIndex &parent, int first, int last)
{
    const QString parentId = idForIndex(parent);
    const int parentRow = treeIndexOfId(parentId);
    if (parentRow < 0 || !m_expandedIds.contains(parentId) || first < 0 || last < first) return;
    struct VisibleNode { QPersistentModelIndex index; int depth; };
    QVector<VisibleNode> inserted;
    QVector<QPair<QModelIndex, int>> pending;
    const int depth = m_visibleDepths[parentRow] + 1;
    for (int row = last; row >= first; --row) pending.push_back({m_model->index(row, 0, parent), depth});
    constexpr int maxNodes = 1'000'000;
    constexpr int maxDepth = 256;
    while (!pending.isEmpty() && inserted.size() < maxNodes) {
        const auto [node, nodeDepth] = pending.takeLast();
        inserted.push_back({QPersistentModelIndex(node), nodeDepth});
        const QString id = idForIndex(node);
        if (!m_expandedIds.contains(id) || nodeDepth >= maxDepth) continue;
        for (int child = m_model->rowCount(node) - 1; child >= 0; --child)
            pending.push_back({m_model->index(child, 0, node), nodeDepth + 1});
    }
    if (inserted.isEmpty()) return;
    int insertion = parentRow + 1;
    while (insertion < m_visibleIndexes.size() && m_visibleDepths[insertion] > m_visibleDepths[parentRow]) {
        const QModelIndex existing(m_visibleIndexes[insertion]);
        if (existing.parent() == parent && existing.row() >= first) break;
        ++insertion;
    }
    beginInsertRows({}, insertion, insertion + inserted.size() - 1);
    for (int i = 0; i < inserted.size(); ++i) {
        m_visibleIndexes.insert(insertion + i, inserted[i].index);
        m_visibleDepths.insert(insertion + i, inserted[i].depth);
    }
    endInsertRows();
    for (int row = insertion; row < m_visibleIndexes.size(); ++row)
        m_visibleRows.insert(idForIndex(m_visibleIndexes[row]), row);
}

void VirtualCollectionAdapter::removeVisibleDescendants(const QModelIndex &parent)
{
    const int parentRow = treeIndexOfId(idForIndex(parent));
    if (parentRow < 0) return;
    int end = parentRow + 1;
    while (end < m_visibleDepths.size() && m_visibleDepths[end] > m_visibleDepths[parentRow]) ++end;
    if (end == parentRow + 1) return;
    beginRemoveRows({}, parentRow + 1, end - 1);
    for (int row = parentRow + 1; row < end; ++row) m_visibleRows.remove(idForIndex(m_visibleIndexes[row]));
    m_visibleIndexes.remove(parentRow + 1, end - parentRow - 1);
    m_visibleDepths.remove(parentRow + 1, end - parentRow - 1);
    endRemoveRows();
    for (int row = parentRow + 1; row < m_visibleIndexes.size(); ++row)
        m_visibleRows.insert(idForIndex(m_visibleIndexes[row]), row);
}

} // namespace QindaTK
