// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: values are nonnegative finite parts. A parent with a
// declared total must equal the complete sum of its children. Missing values
// have unknown magnitude and are excluded only from explicitly named known
// totals; they never silently become zero.
class PartWholeModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(double knownTotal READ knownTotal NOTIFY changed)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(int zeroCount READ zeroCount NOTIFY changed)
    Q_PROPERTY(bool complete READ isComplete NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    struct Node {
        QString id, parentId, label;
        double value = 0, knownTotal = 0;
        bool declared = false, complete = false;
        int parent = -1, depth = 0;
        QVector<int> children;
    };
    explicit PartWholeModel(QObject *parent = nullptr);
    QString unit() const { return m_unit; }
    void setUnit(const QString &v);
    int count() const { return m_nodes.size(); }
    double knownTotal() const { return m_knownTotal; }
    int missingCount() const { return m_missingCount; }
    int zeroCount() const { return m_zeroCount; }
    bool isComplete() const { return m_missingCount == 0; }
    QString lastError() const { return m_lastError; }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    Q_INVOKABLE bool replaceNodes(const QVariantList &rows);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap node(const QString &id) const;
    Q_INVOKABLE QStringList visibleRootIds() const;
    const QVector<Node> &nodes() const { return m_nodes; }
    const QVector<int> &roots() const { return m_roots; }
    int indexOf(const QString &id) const { return m_ids.value(id,-1); }
    QVector<int> visibleRoots() const;
signals:
    void changed();
    void errorChanged();
private:
    QVariantMap record(int index) const;
    void error(const QString &message);
    enum Role { IdRole=Qt::UserRole+1, ParentIdRole, LabelRole, ValueRole,
                KnownTotalRole, StatusRole, DepthRole, ShareRole };
    QVector<Node> m_nodes;
    QVector<int> m_roots;
    QHash<QString,int> m_ids;
    QString m_unit, m_lastError;
    double m_knownTotal = 0;
    int m_missingCount = 0, m_zeroCount = 0;
};
} // namespace QindaTK
