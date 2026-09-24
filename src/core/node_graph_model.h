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
// parameters and edit history belong to the toolkit. Saved graphs retain
// source IDs but never embed a decoder, codec or application project.
class NodeGraphModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
    Q_PROPERTY(QVariantList edges READ edges NOTIFY changed)
    Q_PROPERTY(QString imageDataUrl READ imageDataUrl NOTIFY frameChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
public:
    explicit NodeGraphModel(QObject *parent = nullptr);
    QVariantList nodes() const;
    QVariantList edges() const;
    QString imageDataUrl() const { return m_imageDataUrl; }
    QImage frame() const { return m_frame; }
    QString lastError() const { return m_error; }
    qulonglong revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    Q_INVOKABLE QString addNode(const QString &type, const QString &title,
                                double x, double y, const QVariantMap &parameters = {});
    Q_INVOKABLE bool removeNode(const QString &nodeId);
    Q_INVOKABLE bool moveNode(const QString &nodeId, double x, double y);
    Q_INVOKABLE bool setParameters(const QString &nodeId, const QVariantMap &parameters);
    Q_INVOKABLE QString connectPorts(const QString &fromNodeId, const QString &fromPortId,
                                     const QString &toNodeId, const QString &toPortId);
    Q_INVOKABLE bool disconnectEdge(const QString &edgeId);
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
    };
    struct Edge {
        QString id;
        QString fromNode;
        QString fromPort;
        QString toNode;
        QString toPort;
    };
    struct GraphState { QVector<Node> nodes; QVector<Edge> edges; };

    int nodeIndex(const QString &id) const;
    int edgeIndex(const QString &id) const;
    static bool validType(const QString &type);
    static bool validParameters(const QString &type, const QVariantMap &parameters);
    static QVariantList portList(const QString &type, bool inputs);
    static bool hasPort(const QString &type, const QString &port, bool input);
    bool reaches(const QString &start, const QString &target) const;
    void rememberEdit();
    void invalidateFrom(const QString &nodeId);
    void invalidateAll();
    void setError(const QString &message);
    QImage evaluate(const QString &nodeId, const QSize &size, QSet<QString> *visiting);
    QString inputNode(const QString &nodeId, const QString &portId) const;

    QVector<Node> m_nodes;
    QVector<Edge> m_edges;
    QVector<GraphState> m_undo;
    QVector<GraphState> m_redo;
    QHash<QString, QImage> m_sources;
    QHash<QString, QImage> m_cache;
    QStringList m_cacheOrder;
    qint64 m_sourceBytes = 0;
    qint64 m_cacheBytes = 0;
    QSize m_cacheSize;
    QImage m_frame;
    QString m_imageDataUrl;
    QString m_error;
    quint64 m_revision = 0;
};

} // namespace QindaTK
