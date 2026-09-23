// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QQueue>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Bounded, GUI-thread notification history. Actions are descriptive IDs;
// invoking them is deliberately left to the host's CommandRegistry/context.
class NotificationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY changed)
    Q_PROPERTY(int maxEntries READ maxEntries WRITE setMaxEntries NOTIFY changed)
    Q_PROPERTY(int pendingToastCount READ pendingToastCount NOTIFY toastQueueChanged)
public:
    enum Role { NotificationIdRole = Qt::UserRole + 1, SeverityRole, TitleRole,
                MessageRole, SourceRole, TimestampRole, ReadRole, ActionsRole,
                RepeatCountRole };
    explicit NotificationModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_entries.size()); }
    int unreadCount() const;
    int maxEntries() const { return m_maxEntries; }
    int pendingToastCount() const { return int(m_toastQueue.size()); }
    void setMaxEntries(int maximum);
    Q_INVOKABLE QString post(const QString &title, const QString &message,
                              const QString &severity = QStringLiteral("info"),
                              const QString &source = {}, const QVariantList &actions = {});
    Q_INVOKABLE bool markRead(const QString &notificationId);
    Q_INVOKABLE bool dismiss(const QString &notificationId);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap takeNextToast();
    Q_INVOKABLE void clear();
signals:
    void changed();
    void posted(const QString &notificationId);
    void toastQueueChanged();
private:
    struct Entry {
        QString id;
        QString severity;
        QString title;
        QString message;
        QString source;
        QDateTime timestamp;
        bool read = false;
        QVariantList actions;
        int repeatCount = 1;
    };
    int rowFor(const QString &id) const;
    void enqueueToast(const QString &id);
    static QVariantMap asMap(const Entry &entry);
    QVector<Entry> m_entries;
    QQueue<QString> m_toastQueue;
    int m_maxEntries = 200;
};

} // namespace QindaTK
