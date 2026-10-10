// SPDX-License-Identifier: LGPL-3.0-or-later
// LocalProductivityStore: batch upsert and the stable-ID index (D-324,
// docs/productivity-store.md#batch-upsert). A mail or calendar sync hands
// thousands of records at once; one change and one signal keep QML from
// re-evaluating every binding per record.
#include "local_productivity_store.h"

namespace QindaTK {
namespace {
constexpr qsizetype MaximumBatch = 100000;
}

const QHash<QString, int> &LocalProductivityStore::idIndex(const QString &name) const
{
    const auto found = m_idIndex.constFind(name);
    if (found != m_idIndex.constEnd()) return *found;
    QHash<QString, int> index;
    if (const QVariantList *rows = collection(name)) {
        index.reserve(rows->size());
        for (int i = 0; i < rows->size(); ++i) index.insert(idOf(rows->at(i).toMap()), i);
    }
    return *m_idIndex.insert(name, index);
}

int LocalProductivityStore::indexOf(const QString &name, const QString &recordId) const
{
    if (!collection(name) || recordId.trimmed().isEmpty()) return -1;
    return idIndex(name).value(recordId.trimmed(), -1);
}

QVariantMap LocalProductivityStore::record(const QString &name, const QString &recordId) const
{
    const int i = indexOf(name, recordId);
    return i >= 0 ? collection(name)->at(i).toMap() : QVariantMap{};
}

QVariantMap LocalProductivityStore::upsertMany(const QString &name, const QVariantList &records)
{
    QVariantList *rows = collection(name);
    QString error;
    if (!rows) error = QStringLiteral("Unknown collection %1").arg(name);
    else if (records.size() > MaximumBatch) error = QStringLiteral("A batch holds at most %1 records").arg(MaximumBatch);
    for (qsizetype i = 0; error.isEmpty() && i < records.size(); ++i)
        if (idOf(records.at(i).toMap()).isEmpty()) error = QStringLiteral("Record %1 of the batch has no stable ID").arg(i + 1);
    if (!error.isEmpty()) {
        setStatus(QStringLiteral("error"), error);
        return result(false, error);
    }
    QVariantMap out = result(true);
    out.insert(QStringLiteral("inserted"), 0);
    out.insert(QStringLiteral("updated"), 0);
    if (records.isEmpty()) return out;
    QHash<QString, int> index = idIndex(name);
    int inserted = 0;
    int updated = 0;
    for (const QVariant &value : records) {
        const QVariantMap entry = value.toMap();
        const QString id = idOf(entry);
        const auto found = index.constFind(id);
        if (found != index.constEnd()) {
            (*rows)[*found] = entry;
            ++updated;
        } else {
            index.insert(id, int(rows->size()));
            rows->append(entry);
            ++inserted;
        }
    }
    if (name == QLatin1String("events")) {
        m_eventUndo.clear();
        emit eventHistoryChanged();
    }
    ++m_revision;
    emit dataChanged();
    m_idIndex.insert(name, index); // still exact after the batch
    setStatus(QStringLiteral("modified"));
    out = result(true);
    out.insert(QStringLiteral("inserted"), inserted);
    out.insert(QStringLiteral("updated"), updated);
    return out;
}

} // namespace QindaTK
