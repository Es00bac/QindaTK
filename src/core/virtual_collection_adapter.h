// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "selection_model.h"
#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A narrow QML bridge for virtual views. The view model remains authoritative;
// only stable IDs and the requested role values cross the QML boundary.
class VirtualCollectionAdapter : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QAbstractItemModel *identityModel READ identityModel WRITE setIdentityModel NOTIFY identityModelChanged)
    Q_PROPERTY(QindaTK::SelectionModel *selectionModel READ selectionModel WRITE setSelectionModel NOTIFY selectionModelChanged)
    Q_PROPERTY(QString idRole READ idRole WRITE setIdRole NOTIFY idRoleChanged)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int treeCount READ treeCount NOTIFY changed)
    Q_PROPERTY(int indexedIdCount READ indexedIdCount NOTIFY changed)
    Q_PROPERTY(quint64 generation READ generation NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
public:
    explicit VirtualCollectionAdapter(QObject *parent = nullptr);
    QAbstractItemModel *model() const { return m_model; }
    void setModel(QAbstractItemModel *model);
    QAbstractItemModel *identityModel() const { return m_identityModel; }
    void setIdentityModel(QAbstractItemModel *model);
    SelectionModel *selectionModel() const { return m_selection; }
    void setSelectionModel(SelectionModel *selection);
    QString idRole() const { return m_idRoleName; }
    void setIdRole(const QString &name);
    int count() const;
    int treeCount() const { return m_visibleIndexes.size(); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int indexedIdCount() const { return m_indexes.size(); }
    quint64 generation() const { return m_generation; }
    bool valid() const { return m_valid; }

    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE QString treeIdAt(int row) const;
    Q_INVOKABLE int treeIndexOfId(const QString &id) const;
    Q_INVOKABLE QVariant treeDataAt(int row, const QString &roleName) const;
    Q_INVOKABLE QString idForIndex(const QModelIndex &index) const;
    Q_INVOKABLE QVariant dataForId(const QString &id, const QString &roleName) const;
    Q_INVOKABLE QModelIndex indexForId(const QString &id) const;
    Q_INVOKABLE QVariant dataAt(int row, const QString &roleName) const;
    Q_INVOKABLE int indexOfId(const QString &id) const;
    // Returns IDs only for the requested visible interval; deliberately bounded.
    Q_INVOKABLE QStringList rangeIds(int firstRow, int lastRow) const;
    Q_INVOKABLE bool canFetchMoreForId(const QString &parentId) const;
    Q_INVOKABLE bool fetchMoreForId(const QString &parentId);
    Q_INVOKABLE bool canFetchMore() const;
    Q_INVOKABLE bool fetchMore();
    Q_INVOKABLE bool setExpanded(const QString &id, bool expanded);

signals:
    void modelChanged();
    void identityModelChanged();
    void selectionModelChanged();
    void idRoleChanged();
    void changed();
    void identityRemoved(const QStringList &ids);
    void invalidModel(const QString &reason);

private:
    void reconnect();
    void rebuild(bool advanceGeneration);
    void rebuildProjection();
    bool indexInsertedSubtree(const QModelIndex &parent, int first, int last);
    void insertVisibleSubtree(const QModelIndex &parent, int first, int last);
    void removeVisibleDescendants(const QModelIndex &parent);
    void captureRemoval(QAbstractItemModel *source, int first, int last, const QModelIndex &parent);
    int roleForName(const QAbstractItemModel *model, const QString &name) const;
    QPointer<QAbstractItemModel> m_model;
    QPointer<QAbstractItemModel> m_identityModel;
    SelectionModel *m_selection = nullptr;
    QString m_idRoleName = QStringLiteral("id");
    QHash<QString, QPersistentModelIndex> m_indexes;
    QStringList m_rootIds;
    QVector<QPersistentModelIndex> m_visibleIndexes;
    QVector<int> m_visibleDepths;
    QHash<QString, int> m_visibleRows;
    QSet<QString> m_expandedIds;
    int m_treeDepthRole = Qt::UserRole + 500;
    int m_treeHasChildrenRole = Qt::UserRole + 501;
    int m_treeExpandedRole = Qt::UserRole + 502;
    int m_treeParentIdRole = Qt::UserRole + 503;
    int m_treeLoadingRole = Qt::UserRole + 504;
    QStringList m_removing;
    QVector<QMetaObject::Connection> m_modelConnections;
    QVector<QMetaObject::Connection> m_identityConnections;
    quint64 m_generation = 0;
    bool m_valid = true;
    bool m_identityModelExplicit = false;
};

} // namespace QindaTK
