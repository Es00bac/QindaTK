// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notification_model.h"

#include <QThread>
#include <QUuid>
#include <QSet>
#include <limits>

namespace QindaTK {

int NotificationModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant NotificationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const auto &entry = m_entries[index.row()];
    switch (role) {
    case NotificationIdRole: return entry.id;
    case SeverityRole: return entry.severity;
    case TitleRole: return entry.title;
    case MessageRole: return entry.message;
    case SourceRole: return entry.source;
    case TimestampRole: return entry.timestamp.toString(Qt::ISODateWithMs);
    case ReadRole: return entry.read;
    case ActionsRole: return entry.actions;
    case RepeatCountRole: return entry.repeatCount;
    default: return {};
    }
}

QHash<int, QByteArray> NotificationModel::roleNames() const
{
    return {{NotificationIdRole, "notificationId"}, {SeverityRole, "severity"},
            {TitleRole, "title"}, {MessageRole, "message"}, {SourceRole, "source"},
            {TimestampRole, "timestamp"}, {ReadRole, "read"}, {ActionsRole, "actions"},
            {RepeatCountRole, "repeatCount"}};
}

int NotificationModel::unreadCount() const
{
    int unread = 0;
    for (const auto &entry : m_entries) if (!entry.read) ++unread;
    return unread;
}

void NotificationModel::setMaxEntries(int maximum)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (maximum < 1 || maximum > 1000 || maximum == m_maxEntries) return;
    m_maxEntries = maximum;
    if (m_entries.size() > m_maxEntries) {
        const int excess = int(m_entries.size()) - m_maxEntries;
        bool queueChanged = false;
        for (int row = 0; row < excess; ++row)
            queueChanged = m_toastQueue.removeAll(m_entries[row].id) > 0 || queueChanged;
        beginRemoveRows({}, 0, excess - 1);
        m_entries.remove(0, excess);
        endRemoveRows();
        if (queueChanged) emit toastQueueChanged();
    }
    emit changed();
}

QString NotificationModel::post(const QString &title, const QString &message,
                                const QString &severity, const QString &source,
                                const QVariantList &actions)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if ((title.trimmed().isEmpty() && message.trimmed().isEmpty())
        || title.size() > 256 || message.size() > 4096 || source.size() > 256
        || actions.size() > 8) return {};
    if (severity != QStringLiteral("info") && severity != QStringLiteral("success")
        && severity != QStringLiteral("warning") && severity != QStringLiteral("error")) return {};
    QSet<QString> actionIds;
    for (const QVariant &value : actions) {
        const QVariantMap action = value.toMap();
        const QString id = action.value(QStringLiteral("id")).toString();
        const QString label = action.value(QStringLiteral("label")).toString();
        if (id.isEmpty() || label.isEmpty() || id.size() > 128 || label.size() > 128
            || actionIds.contains(id)) return {};
        actionIds.insert(id);
    }
    if (severity == QStringLiteral("warning") || severity == QStringLiteral("error")) {
        for (int row = int(m_entries.size()) - 1; row >= 0; --row) {
            auto &entry = m_entries[row];
            if (entry.severity != severity || entry.title != title || entry.message != message
                || entry.source != source || entry.actions != actions) continue;
            // AGENT-GUARD: repeated failures retain one stable notice identity,
            // become unread again, and move to the most recent position.
            entry.timestamp = QDateTime::currentDateTimeUtc();
            entry.read = false;
            if (entry.repeatCount < std::numeric_limits<int>::max()) ++entry.repeatCount;
            const QString id = entry.id;
            if (row != m_entries.size() - 1) {
                beginMoveRows({}, row, row, {}, int(m_entries.size()));
                m_entries.move(row, int(m_entries.size()) - 1);
                endMoveRows();
                row = int(m_entries.size()) - 1;
            }
            emit dataChanged(index(row), index(row), {TimestampRole, ReadRole, RepeatCountRole});
            emit changed();
            enqueueToast(id);
            emit posted(id);
            return id;
        }
    }
    if (m_entries.size() >= m_maxEntries) {
        const bool queueChanged = m_toastQueue.removeAll(m_entries.first().id) > 0;
        beginRemoveRows({}, 0, 0);
        m_entries.removeFirst();
        endRemoveRows();
        if (queueChanged) emit toastQueueChanged();
    }
    Entry entry{QUuid::createUuid().toString(QUuid::WithoutBraces), severity,
                title, message, source, QDateTime::currentDateTimeUtc(), false, actions};
    const int row = int(m_entries.size());
    beginInsertRows({}, row, row);
    m_entries.append(entry);
    endInsertRows();
    emit changed();
    enqueueToast(entry.id);
    emit posted(entry.id);
    return entry.id;
}

void NotificationModel::enqueueToast(const QString &id)
{
    if (m_toastQueue.contains(id)) return;
    constexpr int maxPendingToasts = 32;
    if (m_toastQueue.size() >= maxPendingToasts) m_toastQueue.dequeue();
    m_toastQueue.enqueue(id);
    emit toastQueueChanged();
}

int NotificationModel::rowFor(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == id) return i;
    return -1;
}

bool NotificationModel::markRead(const QString &notificationId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const int row = rowFor(notificationId);
    if (row < 0) return false;
    if (m_entries[row].read) return true;
    m_entries[row].read = true;
    emit dataChanged(index(row), index(row), {ReadRole});
    emit changed();
    return true;
}

bool NotificationModel::dismiss(const QString &notificationId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const int row = rowFor(notificationId);
    if (row < 0) return false;
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    if (m_toastQueue.removeAll(notificationId) > 0) emit toastQueueChanged();
    emit changed();
    return true;
}

QVariantMap NotificationModel::asMap(const Entry &entry)
{
    return {{QStringLiteral("notificationId"), entry.id},
            {QStringLiteral("severity"), entry.severity},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("message"), entry.message},
            {QStringLiteral("source"), entry.source},
            {QStringLiteral("timestamp"), entry.timestamp.toString(Qt::ISODateWithMs)},
            {QStringLiteral("read"), entry.read},
            {QStringLiteral("actions"), entry.actions},
            {QStringLiteral("repeatCount"), entry.repeatCount}};
}

QVariantMap NotificationModel::get(int row) const
{
    return row < 0 || row >= m_entries.size() ? QVariantMap{} : asMap(m_entries[row]);
}

QVariantMap NotificationModel::takeNextToast()
{
    Q_ASSERT(thread() == QThread::currentThread());
    while (!m_toastQueue.isEmpty()) {
        const QString id = m_toastQueue.dequeue();
        emit toastQueueChanged();
        const int row = rowFor(id);
        if (row >= 0) return asMap(m_entries[row]);
    }
    return {};
}

void NotificationModel::clear()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_entries.isEmpty()) return;
    beginRemoveRows({}, 0, int(m_entries.size()) - 1);
    m_entries.clear();
    endRemoveRows();
    if (!m_toastQueue.isEmpty()) {
        m_toastQueue.clear();
        emit toastQueueChanged();
    }
    emit changed();
}

} // namespace QindaTK
