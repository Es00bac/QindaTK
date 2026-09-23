// SPDX-License-Identifier: LGPL-3.0-or-later
#include "resource_plan_model.h"

#include <QUuid>
#include <cmath>

namespace QindaTK {
namespace {
const QChar Separator(0x1f);
constexpr int MaxIdLength = 256;
}

ResourcePlanModel::ResourcePlanModel(QObject *parent) : QAbstractTableModel(parent) {}

int ResourcePlanModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_resources.size(); }
int ResourcePlanModel::columnCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_buckets.size(); }

QString ResourcePlanModel::cellKey(const QString &resourceId, const QString &bucketId) const
{ return resourceId + Separator + bucketId; }

QVariant ResourcePlanModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= resourceCount()
        || index.column() < 0 || index.column() >= bucketCount()) return {};
    const auto &resource = m_resources.at(index.row());
    const auto &bucket = m_buckets.at(index.column());
    const QString address = cellKey(resource.id, bucket.id);
    const qreal amount = m_allocations.value(address, 0.0);
    switch (role) {
    case Qt::DisplayRole: case Qt::EditRole: case AllocationRole: return amount;
    case ResourceIdRole: return resource.id;
    case ResourceNameRole: return resource.name;
    case BucketIdRole: return bucket.id;
    case BucketNameRole: return bucket.name;
    case UnitRole: return resource.unit;
    case CapacityRole: return resource.capacity;
    case OverAllocatedRole: return amount > resource.capacity;
    case ResourceUsageRole: return resourceUsage(resource.id);
    case ResourceCapacityRole: return resourceCapacity(resource.id);
    case ResourceConflictRole: return resourceConflict(resource.id);
    case BucketUsageRole: {
        const auto summary = bucketSummary(bucket.id).value(resource.unit).toMap();
        return summary.value(QStringLiteral("usage"));
    }
    case BucketCapacityRole: {
        const auto summary = bucketSummary(bucket.id).value(resource.unit).toMap();
        return summary.value(QStringLiteral("capacity"));
    }
    case BucketConflictRole: {
        const auto summary = bucketSummary(bucket.id).value(resource.unit).toMap();
        return summary.value(QStringLiteral("overAllocated"));
    }
    default: return {};
    }
}

QVariant ResourcePlanModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal)
        return section >= 0 && section < bucketCount() ? m_buckets.at(section).name : QVariant{};
    return section >= 0 && section < resourceCount() ? m_resources.at(section).name : QVariant{};
}

QHash<int, QByteArray> ResourcePlanModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {Qt::EditRole, "edit"},
            {ResourceIdRole, "resourceId"}, {ResourceNameRole, "resourceName"},
            {BucketIdRole, "bucketId"}, {BucketNameRole, "bucketName"},
            {UnitRole, "unit"}, {AllocationRole, "allocation"},
            {CapacityRole, "capacity"}, {OverAllocatedRole, "overAllocated"},
            {ResourceUsageRole, "resourceUsage"}, {ResourceCapacityRole, "resourceCapacity"},
            {ResourceConflictRole, "resourceConflict"}, {BucketUsageRole, "bucketUsage"},
            {BucketCapacityRole, "bucketCapacity"}, {BucketConflictRole, "bucketConflict"}};
}

bool ResourcePlanModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= resourceCount()
        || index.column() < 0 || index.column() >= bucketCount()
        || (role != Qt::EditRole && role != AllocationRole)) return false;
    bool ok = false;
    const qreal amount = value.toDouble(&ok);
    if (!ok) return fail(QStringLiteral("Allocation must be a finite number"));
    return requestAllocation(resourceIdAt(index.row()), bucketIdAt(index.column()), amount);
}

Qt::ItemFlags ResourcePlanModel::flags(const QModelIndex &index) const
{ return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                         | (m_readOnly ? Qt::NoItemFlags : Qt::ItemIsEditable) : Qt::NoItemFlags; }

bool ResourcePlanModel::fail(const QString &reason)
{ m_lastError = reason; emit rejected(reason); return false; }

QString ResourcePlanModel::resourceIdAt(int row) const
{ return row >= 0 && row < resourceCount() ? m_resources.at(row).id : QString{}; }
QString ResourcePlanModel::bucketIdAt(int column) const
{ return column >= 0 && column < bucketCount() ? m_buckets.at(column).id : QString{}; }
QString ResourcePlanModel::resourceName(const QString &id) const
{ const int i = resourceIndex(id); return i >= 0 ? m_resources.at(i).name : QString{}; }
QString ResourcePlanModel::bucketName(const QString &id) const
{ const int i = bucketIndex(id); return i >= 0 ? m_buckets.at(i).name : QString{}; }
QString ResourcePlanModel::unit(const QString &id) const
{ const int i = resourceIndex(id); return i >= 0 ? m_resources.at(i).unit : QString{}; }
int ResourcePlanModel::resourceIndex(const QString &id) const
{ return m_resourceIndexes.value(id, -1); }
int ResourcePlanModel::bucketIndex(const QString &id) const
{ return m_bucketIndexes.value(id, -1); }

qreal ResourcePlanModel::allocation(const QString &resourceId, const QString &bucketId) const
{
    if (resourceIndex(resourceId) < 0 || bucketIndex(bucketId) < 0) return 0;
    return m_allocations.value(cellKey(resourceId, bucketId), 0.0);
}
qreal ResourcePlanModel::capacity(const QString &resourceId, const QString &bucketId) const
{ return resourceIndex(resourceId) >= 0 && bucketIndex(bucketId) >= 0
        ? m_resources.at(resourceIndex(resourceId)).capacity : 0; }
bool ResourcePlanModel::overAllocated(const QString &r, const QString &b) const
{ return allocation(r, b) > capacity(r, b); }

qreal ResourcePlanModel::resourceUsage(const QString &id) const
{
    if (resourceIndex(id) < 0) return 0;
    qreal total = 0;
    for (const auto &bucket : m_buckets) total += m_allocations.value(cellKey(id, bucket.id), 0.0);
    return total;
}
qreal ResourcePlanModel::resourceCapacity(const QString &id) const
{ const int i = resourceIndex(id); return i >= 0 ? m_resources.at(i).capacity * bucketCount() : 0; }
bool ResourcePlanModel::resourceConflict(const QString &id) const
{
    if (resourceIndex(id) < 0) return false;
    for (const auto &bucket : m_buckets) if (overAllocated(id, bucket.id)) return true;
    return false;
}

QVariantMap ResourcePlanModel::bucketSummary(const QString &id) const
{
    if (bucketIndex(id) < 0) return {};
    QVariantMap totals;
    for (const auto &resource : m_resources) {
        QVariantMap unitTotal = totals.value(resource.unit).toMap();
        const qreal usage = unitTotal.value(QStringLiteral("usage")).toDouble()
                            + m_allocations.value(cellKey(resource.id, id), 0.0);
        const qreal capacityValue = unitTotal.value(QStringLiteral("capacity")).toDouble()
                                    + resource.capacity;
        unitTotal.insert(QStringLiteral("usage"), usage);
        unitTotal.insert(QStringLiteral("capacity"), capacityValue);
        unitTotal.insert(QStringLiteral("overAllocated"), usage > capacityValue);
        totals.insert(resource.unit, unitTotal);
    }
    return totals;
}

void ResourcePlanModel::setReadOnly(bool value)
{
    if (m_readOnly == value) return;
    m_readOnly = value;
    emit readOnlyChanged(); emit historyChanged();
}

bool ResourcePlanModel::setResources(const QVariantList &records)
{
    if (m_readOnly) return fail(QStringLiteral("Resource plan is read-only"));
    if (records.size() > MaxResources || qint64(records.size()) * bucketCount() > MaxCells)
        return fail(QStringLiteral("Resource plan exceeds its bounded dimensions"));
    QVector<Resource> resources;
    QHash<QString, int> indexes;
    resources.reserve(records.size());
    for (const QVariant &entry : records) {
        const QVariantMap map = entry.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        const QString name = map.value(QStringLiteral("name")).toString();
        const QString unitName = map.value(QStringLiteral("unit")).toString();
        bool validCapacity = false;
        const qreal capacityValue = map.value(QStringLiteral("capacity")).toDouble(&validCapacity);
        if (id.isEmpty() || id.size() > MaxIdLength || id.contains(Separator)
            || name.isEmpty() || name.size() > MaxIdLength || unitName.isEmpty()
            || unitName.size() > MaxIdLength || !validCapacity || !std::isfinite(capacityValue)
            || capacityValue < 0 || indexes.contains(id))
            return fail(QStringLiteral("Resources require unique IDs, names, units and finite nonnegative capacities"));
        indexes.insert(id, resources.size());
        resources.append({id, name, unitName, capacityValue});
    }
    if (m_resources == resources) return true;
    beginResetModel();
    m_resources = std::move(resources); m_resourceIndexes = std::move(indexes);
    for (auto it = m_allocations.begin(); it != m_allocations.end();) {
        const int split = it.key().indexOf(Separator);
        const QString r = split < 0 ? QString{} : it.key().left(split);
        if (!m_resourceIndexes.contains(r)) it = m_allocations.erase(it); else ++it;
    }
    m_history.clear(); m_historyCursor = 0; ++m_revision;
    endResetModel(); emit structureChanged(); emit planChanged(); emit historyChanged();
    return true;
}

bool ResourcePlanModel::setBuckets(const QVariantList &records)
{
    if (m_readOnly) return fail(QStringLiteral("Resource plan is read-only"));
    if (records.size() > MaxBuckets || qint64(resourceCount()) * records.size() > MaxCells)
        return fail(QStringLiteral("Resource plan exceeds its bounded dimensions"));
    QVector<Bucket> buckets;
    QHash<QString, int> indexes;
    buckets.reserve(records.size());
    for (const QVariant &entry : records) {
        const QVariantMap map = entry.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        const QString name = map.value(QStringLiteral("name")).toString();
        if (id.isEmpty() || id.size() > MaxIdLength || id.contains(Separator)
            || name.isEmpty() || name.size() > MaxIdLength || indexes.contains(id))
            return fail(QStringLiteral("Buckets require unique IDs and names"));
        indexes.insert(id, buckets.size()); buckets.append({id, name});
    }
    if (m_buckets == buckets) return true;
    beginResetModel();
    m_buckets = std::move(buckets); m_bucketIndexes = std::move(indexes);
    for (auto it = m_allocations.begin(); it != m_allocations.end();) {
        const int split = it.key().indexOf(Separator);
        const QString b = split < 0 ? QString{} : it.key().mid(split + 1);
        if (!m_bucketIndexes.contains(b)) it = m_allocations.erase(it); else ++it;
    }
    m_history.clear(); m_historyCursor = 0; ++m_revision;
    endResetModel(); emit structureChanged(); emit planChanged(); emit historyChanged();
    return true;
}

bool ResourcePlanModel::apply(const Step &step, bool reverse)
{
    const int row = resourceIndex(step.resourceId), column = bucketIndex(step.bucketId);
    if (row < 0 || column < 0) return fail(QStringLiteral("Allocation history references a removed identity"));
    const qreal expected = reverse ? step.after : step.before;
    if (allocation(step.resourceId, step.bucketId) != expected)
        return fail(QStringLiteral("Allocation history conflicts with the current plan"));
    const qreal value = reverse ? step.before : step.after;
    const QString address = cellKey(step.resourceId, step.bucketId);
    if (value == 0) m_allocations.remove(address); else m_allocations.insert(address, value);
    return true;
}

void ResourcePlanModel::publish(const Step &step)
{
    const int row = resourceIndex(step.resourceId), column = bucketIndex(step.bucketId);
    ++m_revision;
    emit dataChanged(index(row, 0), index(row, bucketCount() - 1));
    emit dataChanged(index(0, column), index(resourceCount() - 1, column));
    emit planChanged();
}

bool ResourcePlanModel::requestAllocation(const QString &r, const QString &b, qreal value)
{
    if (m_readOnly) return fail(QStringLiteral("Resource plan is read-only"));
    if (resourceIndex(r) < 0 || bucketIndex(b) < 0)
        return fail(QStringLiteral("Allocation request uses an unknown resource or bucket ID"));
    if (!std::isfinite(value) || value < 0)
        return fail(QStringLiteral("Allocation must be finite and nonnegative"));
    const qreal before = allocation(r, b);
    if (before == value) return true;
    const Step step{r, b, before, value};
    if (!apply(step, false)) return false;
    m_history.resize(m_historyCursor);
    m_history.append(step);
    if (m_history.size() > MaxHistory) m_history.removeFirst();
    m_historyCursor = m_history.size();
    publish(step);
    emit historyChanged();
    emit allocationCommitted(QUuid::createUuid().toString(QUuid::WithoutBraces), r, b, before, value);
    return true;
}

bool ResourcePlanModel::undo()
{
    if (m_readOnly) return fail(QStringLiteral("Resource plan is read-only"));
    if (m_historyCursor == 0) return fail(QStringLiteral("Resource plan undo unavailable"));
    const Step step = m_history.at(m_historyCursor - 1);
    if (!apply(step, true)) return false;
    --m_historyCursor; publish(step); emit historyChanged(); return true;
}
bool ResourcePlanModel::redo()
{
    if (m_readOnly) return fail(QStringLiteral("Resource plan is read-only"));
    if (m_historyCursor >= m_history.size()) return fail(QStringLiteral("Resource plan redo unavailable"));
    const Step step = m_history.at(m_historyCursor);
    if (!apply(step, false)) return false;
    ++m_historyCursor; publish(step); emit historyChanged(); return true;
}

} // namespace QindaTK
