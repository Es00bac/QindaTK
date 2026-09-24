// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_property_sync.h"
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Bounded local checkpoint store for one synchronized PropertyDocument.
// Checkpoints are immutable QSaveFile snapshots; restore emits one new typed
// collaboration operation and first saves the current state as a checkpoint.
class PropertyVersionHistory : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::LocalPropertySyncClient *client READ client WRITE setClient NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath NOTIFY changed)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit PropertyVersionHistory(QObject *parent = nullptr) : QObject(parent) {}
    LocalPropertySyncClient *client() const { return m_client; }
    void setClient(LocalPropertySyncClient *client);
    QString storagePath() const { return m_storagePath; }
    void setStoragePath(const QString &path);
    QVariantList versions() const;
    int count() const { return m_entries.size(); }
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool open();
    Q_INVOKABLE QString checkpoint(const QString &name);
    Q_INVOKABLE QVariantMap inspect(const QString &id) const;
    Q_INVOKABLE QVariantList diff(const QString &fromId, const QString &toId) const;
    Q_INVOKABLE QVariantList diffCurrent(const QString &fromId) const;
    Q_INVOKABLE bool restore(const QString &id);
signals:
    void changed();
private:
    struct Entry {
        QString id;
        QString name;
        QString actorId;
        QString revision;
        QString createdAt;
        int operationCount = 0;
        QByteArray snapshot;
    };
    const Entry *find(const QString &id) const;
    bool fail(const QString &message);
    bool persist(const QVector<Entry> &entries);
    QVariantList diffSnapshots(const QByteArray &from, const QByteArray &to) const;
    QPointer<LocalPropertySyncClient> m_client;
    QString m_storagePath;
    QString m_lastError;
    QVector<Entry> m_entries;
    bool m_opened = false;
};

} // namespace QindaTK
