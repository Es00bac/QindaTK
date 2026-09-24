// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A bounded, typed image graph. Source pixels belong to the host; topology,
// parameters, groups and edit history belong to the toolkit. Saved graphs
// retain source IDs but never embed a decoder, codec or application project.
// AGENT-CONTRACT (docs/node-graph.md): NodeGraph.qml draws the nodes/edges/
// groups snapshots and emits requests; only this model validates and edits.
class NodeGraphModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
    Q_PROPERTY(QVariantList edges READ edges NOTIFY changed)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QVariantList nodeTypes READ nodeTypes CONSTANT)
    Q_PROPERTY(QString imageDataUrl READ imageDataUrl NOTIFY frameChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
public:
    explicit NodeGraphModel(QObject *parent = nullptr);
    QVariantList nodes() const;
    QVariantList edges() const;
    QVariantList groups() const;
    static QVariantList nodeTypes();
    QString imageDataUrl() const { return m_imageDataUrl; }
    QImage frame() const { return m_frame; }
    QString lastError() const { return m_error; }
    qulonglong revision() const { return m_revision; }
    bool canUndo() const { return !m_editing && !m_undo.isEmpty(); }
    bool canRedo() const { return !m_editing && !m_redo.isEmpty(); }
    bool editing() const { return m_editing; }

    Q_INVOKABLE QString addNode(const QString &type, const QString &title,
                                double x, double y, const QVariantMap &parameters = {});
    Q_INVOKABLE bool removeNode(const QString &nodeId);
    Q_INVOKABLE bool moveNode(const QString &nodeId, double x, double y);
    Q_INVOKABLE bool setParameters(const QString &nodeId, const QVariantMap &parameters);
    Q_INVOKABLE bool setParameter(const QString &nodeId, const QString &parameterId,
                                  const QVariant &value);
    Q_INVOKABLE QString connectPorts(const QString &fromNodeId, const QString &fromPortId,
                                     const QString &toNodeId, const QString &toPortId);
    Q_INVOKABLE bool disconnectEdge(const QString &edgeId);
    Q_INVOKABLE QString groupNodes(const QStringList &nodeIds, const QString &title = {});
    Q_INVOKABLE bool ungroup(const QString &groupId);
    Q_INVOKABLE bool moveGroup(const QString &groupId, double dx, double dy);
    // One gesture = one undo step: edits between beginEdit and commitEdit
    // coalesce; cancelEdit restores the graph captured by beginEdit.
    Q_INVOKABLE bool beginEdit();
    Q_INVOKABLE bool commitEdit();
    Q_INVOKABLE bool cancelEdit();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    bool setSourceImage(const QString &nodeId, const QImage &image);
    Q_INVOKABLE QVariantMap render(const QString &outputNodeId, int width, int height);
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);
signals:
    void changed();
    void frameChanged();
    void errorChanged();
private:
    struct Node {
        QString id;
        QString type;
        QString title;
        double x = 0;
        double y = 0;
        QVariantMap parameters;
        bool operator==(const Node &) const = default;
    };
    struct Edge {
        QString id;
        QString fromNode;
        QString fromPort;
        QString toNode;
        QString toPort;
        bool operator==(const Edge &) const = default;
    };
    struct Group {
        QString id;
        QString title;
        QStringList nodeIds;
        bool operator==(const Group &) const = default;
    };
    struct GraphState {
        QVector<Node> nodes;
        QVector<Edge> edges;
        QVector<Group> groups;
        bool operator==(const GraphState &) const = default;
    };

    int nodeIndex(const QString &id) const;
    int edgeIndex(const QString &id) const;
    int groupIndex(const QString &id) const;
    int groupOf(const QString &nodeId) const;
    // Type table (node_graph_types.cpp): ports, parameter identity and ranges.
    static bool validType(const QString &type);
    static bool validParameters(const QString &type, const QVariantMap &parameters);
    static QVariant parameter(const QString &type, const QVariantMap &parameters,
                              const QString &parameterId);
    static QVariantList portList(const QString &type, bool inputs);
    static QString portType(const QString &type, const QString &port, bool input);
    static bool hasPort(const QString &type, const QString &port, bool input);
    bool reaches(const QString &start, const QString &target) const;
    GraphState state() const { return {m_nodes, m_edges, m_groups}; }
    void restore(GraphState state);
    void rememberEdit();
    void invalidateFrom(const QString &nodeId);
    void invalidateAll();
    void setError(const QString &message);
    QImage evaluate(const QString &nodeId, const QSize &size, QSet<QString> *visiting);
    QString inputNode(const QString &nodeId, const QString &portId) const;

    QVector<Node> m_nodes;
    QVector<Edge> m_edges;
    QVector<Group> m_groups;
    QVector<GraphState> m_undo;
    QVector<GraphState> m_redo;
    QVector<GraphState> m_editRedo;
    QHash<QString, QImage> m_sources;
    QHash<QString, QImage> m_cache;
    QStringList m_cacheOrder;
    QStringList m_evaluated;
    qint64 m_sourceBytes = 0;
    qint64 m_cacheBytes = 0;
    QSize m_cacheSize;
    QImage m_frame;
    QString m_imageDataUrl;
    QString m_error;
    quint64 m_revision = 0;
    bool m_editing = false;
};

} // namespace QindaTK
