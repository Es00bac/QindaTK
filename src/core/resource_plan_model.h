// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Bounded allocation matrix. Allocation and capacity are values in the
// resource's declared unit; each bucket is an independent capacity period.
class ResourcePlanModel : public QAbstractTableModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY structureChanged)
    Q_PROPERTY(int bucketCount READ bucketCount NOTIFY structureChanged)
    Q_PROPERTY(QString revision READ revision NOTIFY planChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY rejected)
public:
    enum Role {
        ResourceIdRole = Qt::UserRole + 1, ResourceNameRole, BucketIdRole,
        BucketNameRole, UnitRole, AllocationRole, CapacityRole,
        OverAllocatedRole, ResourceUsageRole, ResourceCapacityRole,
        ResourceConflictRole, BucketUsageRole, BucketCapacityRole,
        BucketConflictRole
    };
    Q_ENUM(Role)

    explicit ResourcePlanModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    int resourceCount() const { return m_resources.size(); }
    int bucketCount() const { return m_buckets.size(); }
    QString revision() const { return QString::number(m_revision); }
    bool readOnly() const { return m_readOnly; }
    bool canUndo() const { return !m_readOnly && m_historyCursor > 0; }
    bool canRedo() const { return !m_readOnly && m_historyCursor < m_history.size(); }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QString resourceIdAt(int row) const;
    Q_INVOKABLE QString bucketIdAt(int column) const;
    Q_INVOKABLE QString resourceName(const QString &id) const;
    Q_INVOKABLE QString bucketName(const QString &id) const;
    Q_INVOKABLE QString unit(const QString &resourceId) const;
    Q_INVOKABLE int resourceIndex(const QString &id) const;
    Q_INVOKABLE int bucketIndex(const QString &id) const;
    Q_INVOKABLE qreal allocation(const QString &resourceId, const QString &bucketId) const;
    Q_INVOKABLE qreal capacity(const QString &resourceId, const QString &bucketId) const;
    Q_INVOKABLE bool overAllocated(const QString &resourceId, const QString &bucketId) const;
    Q_INVOKABLE qreal resourceUsage(const QString &resourceId) const;
    Q_INVOKABLE qreal resourceCapacity(const QString &resourceId) const;
    Q_INVOKABLE bool resourceConflict(const QString &resourceId) const;
    // Bucket totals are per resource unit. Mixed units are never combined.
    Q_INVOKABLE QVariantMap bucketSummary(const QString &bucketId) const;
    Q_INVOKABLE bool setResources(const QVariantList &resources);
    Q_INVOKABLE bool setBuckets(const QVariantList &buckets);
    Q_INVOKABLE bool requestAllocation(const QString &resourceId, const QString &bucketId,
                                       qreal value);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    void setReadOnly(bool value);

signals:
    void structureChanged();
    void planChanged();
    void historyChanged();
    void readOnlyChanged();
    void allocationCommitted(const QString &transactionId, const QString &resourceId,
                             const QString &bucketId, qreal before, qreal after);
    void rejected(const QString &reason);

private:
    struct Resource { QString id, name, unit; qreal capacity = 0; friend bool operator==(const Resource &, const Resource &) = default; };
    struct Bucket { QString id, name; friend bool operator==(const Bucket &, const Bucket &) = default; };
    struct Step { QString resourceId, bucketId; qreal before = 0, after = 0; };
    static constexpr int MaxResources = 100000;
    static constexpr int MaxBuckets = 256;
    static constexpr qsizetype MaxCells = 1000000;
    static constexpr int MaxHistory = 256;
    bool fail(const QString &reason);
    bool apply(const Step &step, bool reverse);
    void publish(const Step &step);
    QString cellKey(const QString &resourceId, const QString &bucketId) const;

    QVector<Resource> m_resources;
    QVector<Bucket> m_buckets;
    QHash<QString, int> m_resourceIndexes, m_bucketIndexes;
    QHash<QString, qreal> m_allocations;
    QVector<Step> m_history;
    qsizetype m_historyCursor = 0;
    quint64 m_revision = 0;
    bool m_readOnly = false;
    QString m_lastError;
};

} // namespace QindaTK
