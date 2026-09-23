// SPDX-License-Identifier: LGPL-3.0-or-later
#include "selection_model.h"
#include "virtual_collection_adapter.h"

#include <algorithm>
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSortFilterProxyModel>
#include <QtTest>
#include <memory>

using namespace QindaTK;

namespace {

QString stableId(int value)
{
    return QStringLiteral("{00000000-0000-4000-8000-%1}").arg(value, 12, 16, QLatin1Char('0'));
}

QString canonicalId(int value)
{
    return QUuid(stableId(value)).toString(QUuid::WithoutBraces);
}

class GeneratedModel final : public QAbstractListModel {
public:
    enum Role { StableIdRole = Qt::UserRole + 1, LabelRole, ResourceRole };
    explicit GeneratedModel(int count, QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
        m_ids.reserve(count);
        m_labels.reserve(count);
        for (int i = 0; i < count; ++i) {
            m_ids.push_back(stableId(i));
            m_labels.push_back(QStringLiteral("Row %1").arg(count - i, 6, 10, QLatin1Char('0')));
        }
    }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_ids.size(); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_ids.size()) return {};
        switch (role) {
        case Qt::DisplayRole:
        case LabelRole: return m_labels[index.row()];
        case StableIdRole: return m_ids[index.row()];
        case ResourceRole: return QStringLiteral("asset:%1").arg(index.row());
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{StableIdRole, "id"}, {LabelRole, "label"}, {ResourceRole, "resourceId"}};
    }
    void removeRowAt(int row)
    {
        beginRemoveRows({}, row, row);
        m_ids.removeAt(row);
        m_labels.removeAt(row);
        endRemoveRows();
    }
    void resetKeepingIdentity()
    {
        beginResetModel();
        std::reverse(m_ids.begin(), m_ids.end());
        std::reverse(m_labels.begin(), m_labels.end());
        endResetModel();
    }
private:
    QStringList m_ids;
    QStringList m_labels;
};

class LazyTreeModel final : public QAbstractItemModel {
public:
    enum { StableIdRole = Qt::UserRole + 1, LabelRole, CollisionRole = Qt::UserRole + 500 };
    struct Node { QString id; QString label; Node *parent = nullptr; QVector<Node *> children; bool canLoad = false; bool loaded = false; };
    explicit LazyTreeModel(int rootCount = 1) : m_rootCount(rootCount)
    {
        for (int i = 1; i <= rootCount; ++i)
            m_roots.push_back(new Node{stableId(i), i == 1 ? QStringLiteral("Parent") : QStringLiteral("Row %1").arg(i), nullptr, {}, i == 1, false});
    }
    ~LazyTreeModel() override { for (auto *root : std::as_const(m_roots)) deleteNode(root); }
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override
    {
        if (column != 0 || row < 0) return {};
        const auto siblings = children(parent);
        return row < siblings.size() ? createIndex(row, column, siblings[row]) : QModelIndex{};
    }
    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid()) return {};
        auto *node = static_cast<Node *>(child.internalPointer());
        if (!node || !node->parent) return {};
        const auto siblings = children(node->parent->parent ? indexForNode(node->parent) : QModelIndex{});
        return createIndex(siblings.indexOf(node->parent), 0, node->parent);
    }
    int rowCount(const QModelIndex &parent = {}) const override { return children(parent).size(); }
    int columnCount(const QModelIndex & = {}) const override { return 1; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) return {};
        auto *node = static_cast<Node *>(index.internalPointer());
        if (role == StableIdRole) return node->id;
        if (role == LabelRole || role == Qt::DisplayRole) return node->label;
        if (role == CollisionRole) return QStringLiteral("source collision role");
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }
    bool hasChildren(const QModelIndex &parent = {}) const override
    {
        if (!parent.isValid()) return !m_roots.isEmpty();
        auto *node = static_cast<Node *>(parent.internalPointer());
        return !node->children.isEmpty() || (node->canLoad && !node->loaded);
    }
    bool canFetchMore(const QModelIndex &parent) const override
    {
        if (!parent.isValid()) return false;
        auto *node = static_cast<Node *>(parent.internalPointer());
        return node->canLoad && !node->loaded;
    }
    void fetchMore(const QModelIndex &parent) override
    {
        if (!canFetchMore(parent)) return;
        auto *node = static_cast<Node *>(parent.internalPointer());
        const int row = node->children.size();
        beginInsertRows(parent, row, row);
        node->loaded = true;
        node->children.push_back(new Node{stableId(m_rootCount + 1), QStringLiteral("Lazy child"), node, {}, false, true});
        endInsertRows();
    }
    bool removeRow(int row)
    {
        if (row < 0 || row >= m_roots.size()) return false;
        beginRemoveRows({}, row, row);
        deleteNode(m_roots.takeAt(row));
        endRemoveRows();
        return true;
    }
    QHash<int, QByteArray> roleNames() const override
    {
        auto roles = QAbstractItemModel::roleNames();
        roles.insert(StableIdRole, QByteArrayLiteral("id"));
        roles.insert(LabelRole, QByteArrayLiteral("label"));
        roles.insert(CollisionRole, QByteArrayLiteral("custom"));
        return roles;
    }
private:
    QVector<Node *> m_roots;
    int m_rootCount = 1;
    static void deleteNode(Node *node)
    {
        if (!node) return;
        for (auto *child : std::as_const(node->children)) deleteNode(child);
        delete node;
    }
    QVector<Node *> children(const QModelIndex &parent) const
    {
        return parent.isValid() ? static_cast<Node *>(parent.internalPointer())->children : m_roots;
    }
    QModelIndex indexForNode(Node *node) const
    {
        if (!node) return {};
        const auto siblings = node->parent ? node->parent->children : m_roots;
        return createIndex(siblings.indexOf(node), 0, node);
    }
};

} // namespace

class tst_VirtualCollection : public QObject {
    Q_OBJECT
private slots:
    void hundredThousandRowsStayInModelAndRangesStayBounded()
    {
        constexpr int rowCount = 100000;
        QElapsedTimer timer;
        timer.start();
        GeneratedModel source(rowCount);
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&source);
        VirtualCollectionAdapter adapter;
        adapter.setIdentityModel(&source);
        adapter.setModel(&proxy);
        SelectionModel selection;
        adapter.setSelectionModel(&selection);
        const qint64 setupMs = timer.elapsed();

        QCOMPARE(adapter.count(), rowCount);
        QCOMPARE(adapter.indexedIdCount(), rowCount);
        QVERIFY(adapter.valid());
        QCOMPARE(proxy.rowCount(), rowCount);
        const QString wanted = stableId(98765);
        const int row = adapter.indexOfId(wanted);
        QVERIFY(row >= 0);
        QCOMPARE(adapter.idAt(row), canonicalId(98765));
        QCOMPARE(adapter.dataAt(row, QStringLiteral("label")).toString(),
                 QStringLiteral("Row 001235"));
        QCOMPARE(adapter.rangeIds(row - 20, row + 20).size(), 41);
        QVERIFY(adapter.rangeIds(0, rowCount - 1).isEmpty());
        QVERIFY(selection.select(wanted));

        timer.restart();
        proxy.sort(0, Qt::AscendingOrder);
        const int sortedRow = adapter.indexOfId(wanted);
        QVERIFY(sortedRow >= 0);
        QCOMPARE(adapter.idAt(sortedRow), canonicalId(98765));
        QCOMPARE(selection.currentId(), canonicalId(98765));
        const qint64 sortLookupMs = timer.elapsed();
        qInfo() << "100k model rows / ID-index entries:" << rowCount << adapter.indexedIdCount()
                << "index setup ms:" << setupMs << "sort + stable-ID lookup ms:" << sortLookupMs;
    }

    void filterAndModelResetPreserveStableIdentity()
    {
        GeneratedModel source(100000);
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&source);
        VirtualCollectionAdapter adapter;
        adapter.setIdentityModel(&source);
        adapter.setModel(&proxy);
        SelectionModel selection;
        adapter.setSelectionModel(&selection);

        const QString chosen = stableId(87654);
        QVERIFY(selection.select(chosen));
        proxy.setFilterFixedString(QStringLiteral("never matches"));
        QCOMPARE(proxy.rowCount(), 0);
        QCOMPARE(selection.currentId(), canonicalId(87654));
        QVERIFY(selection.selectedIds().contains(canonicalId(87654)));

        proxy.setFilterRegularExpression(QString());
        QCOMPARE(proxy.rowCount(), 100000);
        QVERIFY(adapter.indexOfId(chosen) >= 0);
        source.resetKeepingIdentity();
        QCOMPARE(selection.currentId(), canonicalId(87654));
        QCOMPARE(adapter.idAt(adapter.indexOfId(chosen)), canonicalId(87654));
    }

    void sourceRemovalPrunesOnlyRemovedIdentities()
    {
        GeneratedModel source(8);
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&source);
        VirtualCollectionAdapter adapter;
        adapter.setIdentityModel(&source);
        adapter.setModel(&proxy);
        SelectionModel selection;
        adapter.setSelectionModel(&selection);
        const QString removed = stableId(3);
        const QString kept = stableId(6);
        QVERIFY(selection.select(removed));
        QVERIFY(selection.toggle(kept));
        QSignalSpy removedSpy(&adapter, &VirtualCollectionAdapter::identityRemoved);
        source.removeRowAt(3);
        QCOMPARE(removedSpy.size(), 1);
        QVERIFY(!selection.selectedIds().contains(canonicalId(3)));
        QVERIFY(selection.selectedIds().contains(canonicalId(6)));
    }

    void destroyedSourceModelsClearTheAdapter()
    {
        VirtualCollectionAdapter adapter;
        auto source = std::make_unique<GeneratedModel>(3);
        adapter.setModel(source.get());
        QCOMPARE(adapter.count(), 3);
        QCOMPARE(adapter.treeCount(), 3);
        source.reset();
        QCOMPARE(adapter.model(), nullptr);
        QCOMPARE(adapter.count(), 0);
        QCOMPARE(adapter.treeCount(), 0);
        QVERIFY(adapter.idAt(0).isEmpty());
    }

    void nestedLazyChildrenIndexAndRemoveWithTheirParent()
    {
        LazyTreeModel source;
        VirtualCollectionAdapter adapter;
        adapter.setModel(&source);
        SelectionModel selection;
        adapter.setSelectionModel(&selection);
        const QString parentId = stableId(1);
        const QString childId = stableId(2);
        QVERIFY(adapter.canFetchMoreForId(parentId));
        QVERIFY(adapter.fetchMoreForId(parentId));
        const QModelIndex childIndex = adapter.indexForId(childId);
        QVERIFY(childIndex.isValid());
        QVERIFY(childIndex.parent().isValid());
        QVERIFY(!adapter.canFetchMoreForId(parentId));
        QVERIFY(selection.select(childId));
        QSignalSpy removedSpy(&adapter, &VirtualCollectionAdapter::identityRemoved);
        QVERIFY(source.removeRow(0));
        QCOMPARE(removedSpy.size(), 1);
        QVERIFY(!selection.selectedIds().contains(canonicalId(2)));
    }

    void roleIdsAvoidSourceRoleCollisionsAndLazyInsertIsMeasured()
    {
        LazyTreeModel source(100000);
        VirtualCollectionAdapter adapter;
        adapter.setModel(&source);
        QCOMPARE(adapter.indexedIdCount(), 100000);
        const auto roles = adapter.roleNames();
        QCOMPARE(roles.value(LazyTreeModel::CollisionRole), QByteArrayLiteral("custom"));
        QVERIFY(roles.key("treeDepth") > LazyTreeModel::CollisionRole);
        QCOMPARE(adapter.dataForId(stableId(1), QStringLiteral("custom")).toString(),
                 QStringLiteral("source collision role"));
        QElapsedTimer timer;
        timer.start();
        QVERIFY(adapter.setExpanded(stableId(1), true));
        const qint64 lazyInsertMs = timer.elapsed();
        QCOMPARE(adapter.indexedIdCount(), 100001);
        QCOMPARE(adapter.treeIndexOfId(stableId(100001)), 1);
        qInfo() << "one lazy child insertion into 100k rows ms:" << lazyInsertMs;
    }

    void qmlTreeViewExpandsRawNestedModelAndLoadsChildren()
    {
        LazyTreeModel source;
        SelectionModel selection;
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(QINDATK_BUILD_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("treeSource"), &source);
        engine.rootContext()->setContextProperty(QStringLiteral("treeSelection"), &selection);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QindaTK as Tk
            Item {
                width: 480
                height: 320
                Tk.VirtualTree {
                    id: tree
                    objectName: "testVirtualTree"
                    anchors.fill: parent
                    model: treeSource
                    selectionModel: treeSelection
                    tooltip: "Hierarchy"
                    property bool expansionResult: false
                    readonly property int visibleRows: tree.treeView ? tree.treeView.count : -1
                    readonly property string secondVisibleId: tree.treeView && tree.treeView.count > 1
                                                               ? tree.treeView.itemAtIndex(1).stableId : ""
                    function expandParent() {
                        expansionResult = requestExpansion("{00000000-0000-4000-8000-000000000001}", 0, true)
                    }
                    function collapseParent() {
                        return requestExpansion("{00000000-0000-4000-8000-000000000001}", 0, false)
                    }
                }
            }
        )", QUrl(QStringLiteral("inline:/RawVirtualTree.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.isReady(), 5000);
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *rootItem = qobject_cast<QQuickItem *>(root.get());
        QVERIFY(rootItem);
        QQuickView window;
        window.resize(480, 320);
        rootItem->setParentItem(window.contentItem());
        window.show();
        QTRY_VERIFY(window.isVisible());
        QObject *tree = root->findChild<QObject *>(QStringLiteral("testVirtualTree"));
        QVERIFY(tree);
        QVERIFY(tree->property("treeView").value<QObject *>());
        QObject *adapter = tree->property("collectionAdapter").value<QObject *>();
        QVERIFY(adapter);
        QCOMPARE(adapter->property("indexedIdCount").toInt(), 1);
        QSignalSpy expansionSpy(tree, SIGNAL(expansionRequested(QString,bool)));
        QVERIFY(QMetaObject::invokeMethod(tree, "expandParent"));
        QVERIFY(tree->property("expansionResult").toBool());
        QCOMPARE(adapter->property("indexedIdCount").toInt(), 2);
        QTRY_COMPARE(tree->property("visibleRows").toInt(), 2);
        QCOMPARE(tree->property("secondVisibleId").toString(), canonicalId(2));
        QCOMPARE(expansionSpy.size(), 1);
        QVERIFY(selection.select(stableId(2)));
        QCOMPARE(selection.currentId(), canonicalId(2));
        QVERIFY(QMetaObject::invokeMethod(tree, "collapseParent"));
        QTRY_COMPARE(tree->property("visibleRows").toInt(), 1);
        QCOMPARE(selection.currentId(), canonicalId(2));
        QVERIFY(QMetaObject::invokeMethod(tree, "expandParent"));
        QTRY_COMPARE(tree->property("visibleRows").toInt(), 2);
        QCOMPARE(tree->property("secondVisibleId").toString(), canonicalId(2));
    }
};

QTEST_MAIN(tst_VirtualCollection)
#include "tst_virtual_collection.moc"
