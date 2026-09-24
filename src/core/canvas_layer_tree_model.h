// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include "canvas_selection_model.h"

#include <QAbstractItemModel>
#include <QPointer>
#include <QSet>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#layer-tree-model, D-094): the LayerTree's
// view of one CanvasDocument. It keeps no hierarchy of its own: rows are
// derived from the document's layers and objects after every change, and
// every request is a document command (one undo step) or a write to the
// shared CanvasSelectionModel. Node keys are "layer:<id>" and "object:<id>"
// (decimal); children list the front-most first, the reverse of paint order.
// Tk.LayerTree projects it through Tk.VirtualCollectionAdapter (idRole "key").
class CanvasLayerTreeModel : public QAbstractItemModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QindaTK::CanvasSelectionModel *selectionModel READ selectionModel WRITE setSelectionModel NOTIFY selectionModelChanged)
    Q_PROPERTY(bool showObjects READ showObjects WRITE setShowObjects NOTIFY showObjectsChanged)
    Q_PROPERTY(int nodeCount READ nodeCount NOTIFY structureChanged)
    Q_PROPERTY(QString activeKey READ activeKey NOTIFY activeKeyChanged)
    Q_PROPERTY(QStringList selectedKeys READ selectedKeys NOTIFY selectedKeysChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Role {
        KeyRole = Qt::UserRole + 1, KindRole, ItemIdRole, LabelRole, DetailRole, IconNameRole,
        ShownRole, LockedRole, EffectiveVisibleRole, EffectiveLockedRole, SelectedRole,
        ActiveRole, ChildCountRole, ParentKeyRole
    };
    Q_ENUM(Role)

    explicit CanvasLayerTreeModel(QObject *parent = nullptr);

    CanvasDocument *document() const { return m_document; }
    void setDocument(CanvasDocument *document);
    CanvasSelectionModel *selectionModel() const { return m_selectionModel; }
    void setSelectionModel(CanvasSelectionModel *model);
    bool showObjects() const { return m_showObjects; }
    void setShowObjects(bool show);
    int nodeCount() const { return int(m_nodes.size()); }
    QString activeKey() const;
    QStringList selectedKeys() const;
    QString lastError() const { return m_lastError; }

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Row requests. `placement` is "above", "below" or "inside"; an object
    // dropped on a content layer goes inside it whatever the placement.
    Q_INVOKABLE bool select(const QString &key, bool toggle = false);
    Q_INVOKABLE bool activate(const QString &key);
    Q_INVOKABLE bool setShown(const QString &key, bool shown);
    Q_INVOKABLE bool setLocked(const QString &key, bool locked);
    Q_INVOKABLE bool rename(const QString &key, const QString &name);
    Q_INVOKABLE bool canMove(const QString &key, const QString &targetKey,
                             const QString &placement) const;
    Q_INVOKABLE bool move(const QString &key, const QString &targetKey, const QString &placement);
    // Moves a row among its siblings; a negative `rows` moves it up (front).
    Q_INVOKABLE bool step(const QString &key, int rows);
    // Into the sibling group shown directly above / out to the parent's level.
    Q_INVOKABLE bool indent(const QString &key);
    Q_INVOKABLE bool outdent(const QString &key);
    // A new layer (or group) just above `nearKey`'s layer, or on top.
    Q_INVOKABLE QString addLayer(const QString &name, const QString &nearKey = {}, bool group = false);
    Q_INVOKABLE QString groupLayers(const QStringList &keys, const QString &name);
    Q_INVOKABLE bool ungroup(const QString &key);
    Q_INVOKABLE bool remove(const QString &key);
    Q_INVOKABLE QStringList ancestorKeys(const QString &key) const;
    Q_INVOKABLE QStringList groupKeys() const;
    Q_INVOKABLE QVariantMap nodeInfo(const QString &key) const;
    Q_INVOKABLE QString keyForObject(const QString &objectId) const;
    Q_INVOKABLE QString keyForLayer(const QString &layerId) const;

signals:
    void documentChanged();
    void selectionModelChanged();
    void showObjectsChanged();
    void structureChanged();
    void activeKeyChanged();
    void selectedKeysChanged();
    void errorChanged();
    // The shared selection gained `key` from elsewhere (a canvas tool, another
    // view): the tree should expand to and show it. Never emitted for this
    // model's own selection writes, so a tree does not chase its own clicks.
    void revealRequested(const QString &key);

private:
    enum class Kind : quint8 { Layer, Group, Object };
    struct Node {
        QString key;
        Kind kind = Kind::Layer;
        quint64 id = 0;
        int parent = -1; // node index; -1 = top level
        int row = 0;     // among the parent's children, front-most first
        QVector<int> children;
        QString label;
        QString detail;
        QString iconName;
        bool shown = true;
        bool locked = false;
        bool effectiveVisible = true;
        bool effectiveLocked = false;
        bool selected = false;
        bool active = false;
    };
    struct Move {
        bool layer = true;
        quint64 id = 0;
        quint64 parent = 0; // parent layer, or the object's destination layer
        int index = 0;      // destination paint-order index
    };
    void rebuild();
    void appendLayers(QVector<Node> &nodes, QVector<int> &siblings, int parent,
                      CanvasLayerId parentLayer,
                      const QHash<CanvasLayerId, QVector<CanvasObjectId>> &objects, int depth) const;
    Node objectNode(const CanvasObject &object, int parent, int row) const;
    void onSelectionChanged();
    const Node *find(const QString &key) const;
    QModelIndex indexOf(int node) const;
    bool resolveMove(const QString &key, const QString &targetKey, const QString &placement,
                     Move *plan) const;
    bool fail(const QString &message);
    bool succeed();

    QPointer<CanvasDocument> m_document;
    QPointer<CanvasSelectionModel> m_selectionModel;
    QVector<Node> m_nodes; // depth-first, parents before children
    QVector<int> m_roots;
    QHash<QString, int> m_byKey;
    QSet<CanvasObjectId> m_selected;
    QString m_activeKey;
    QString m_lastError;
    bool m_showObjects = true;
    bool m_selecting = false; // inside this model's own selection write
};

} // namespace QindaTK
