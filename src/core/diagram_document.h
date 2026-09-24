// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/diagram.md): persistent diagram identity and edits live
// here; DiagramCanvas only presents snapshots and forwards gestures by ID.
class DiagramDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
    Q_PROPERTY(QVariantList edges READ edges NOTIFY changed)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit DiagramDocument(QObject *parent = nullptr);

    QVariantList nodes() const;
    QVariantList edges() const;
    qulonglong revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString lastError() const { return m_error; }

    Q_INVOKABLE QString addNode(const QString &title, double x, double y,
                                const QString &stencilId = QStringLiteral("process"));
    Q_INVOKABLE QString addPort(const QString &nodeId, const QString &label,
                                const QString &direction, const QString &type = QStringLiteral("any"));
    Q_INVOKABLE QString connectPorts(const QString &fromNodeId, const QString &fromPortId,
                                     const QString &toNodeId, const QString &toPortId);
    Q_INVOKABLE bool disconnectEdge(const QString &edgeId);
    Q_INVOKABLE bool removeNode(const QString &nodeId);
    Q_INVOKABLE bool moveNode(const QString &nodeId, double x, double y);
    Q_INVOKABLE bool moveNodes(const QStringList &nodeIds, double dx, double dy);
    Q_INVOKABLE bool setNodeTitle(const QString &nodeId, const QString &title);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);

signals:
    void changed();
    void errorChanged();

private:
    struct Port { QString id; QString label; QString type; };
    struct Node {
        QString id;
        QString title;
        QString stencilId;
        double x = 0;
        double y = 0;
        QVector<Port> inputs;
        QVector<Port> outputs;
    };
    struct Edge {
        QString id;
        QString fromNode;
        QString fromPort;
        QString toNode;
        QString toPort;
    };
    struct State { QVector<Node> nodes; QVector<Edge> edges; };

    int nodeIndex(const QString &id) const;
    int edgeIndex(const QString &id) const;
    QString portType(const QString &nodeId, const QString &portId, bool output) const;
    void rememberEdit();
    void setError(const QString &message);
    bool decode(const QByteArray &bytes, State *result);

    State m_state;
    QVector<State> m_undo;
    QVector<State> m_redo;
    QString m_error;
    quint64 m_revision = 0;
};

} // namespace QindaTK
