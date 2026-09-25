// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_productivity_store.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Two-way CalDAV sync of one calendar collection with LocalProductivityStore:
// VEVENT <-> events, VTODO <-> tasks. Remote changes arrive by ETag; local
// edits are detected by a digest of the mapped fields and written back with
// If-Match, so a record changed on both sides becomes a conflict that the
// host resolves with resolveConflict(..., "local" | "remote").
// AGENT-CONTRACT (CalendarView.qml, TaskBoard.qml, docs/productivity-providers.md):
// synced records carry remoteAccount/remoteHref/remoteEtag/remoteUid/remoteIcs/
// syncDigest/syncState; a local record is pushed as new when its remoteAccount
// equals `accountId` and it has no remoteHref. result.state is "accepted",
// "rejected", "offline" or "conflict".
// AGENT-GUARD: the secret lives only in memory and is redacted from results.
class CalDavProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString collectionUrl MEMBER m_collectionUrl NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(QString accountId MEMBER m_accountId NOTIFY configChanged)
    Q_PROPERTY(QString calendarId MEMBER m_calendarId NOTIFY configChanged)
    Q_PROPERTY(QString defaultZone MEMBER m_defaultZone NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(bool autoSave MEMBER m_autoSave NOTIFY configChanged)
    Q_PROPERTY(LocalProductivityStore *store READ store WRITE setStore NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList conflicts READ conflicts NOTIFY conflictsChanged)
public:
    explicit CalDavProvider(QObject *parent = nullptr);

    LocalProductivityStore *store() const { return m_store; }
    void setStore(LocalProductivityStore *store);
    bool hasSecret() const { return !m_secret.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    QVariantList conflicts() const;

    // mechanism: "basic" (user name + password) or "bearer" (host-obtained token).
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("basic"));
    Q_INVOKABLE void clearSecret();
    // Pulls remote changes, then pushes local edits and new records.
    Q_INVOKABLE bool sync(const QString &requestId);
    // Deletes a synced record on the server (If-Match) and then locally.
    Q_INVOKABLE bool removeRemote(const QString &requestId, const QString &collection, const QString &recordId);
    // choice "remote" adopts the server version; "local" overwrites it with If-Match.
    Q_INVOKABLE bool resolveConflict(const QString &requestId, const QString &collection,
                                     const QString &recordId, const QString &choice);
    // Finds the account's calendars from a server root, principal, home set
    // or collection URL (`collectionUrl` when empty): RFC 6764 well-known
    // redirect, current-user-principal, calendar-home-set, PROPFIND Depth 1.
    // Emits discoveryFinished with principalUrl, homeSetUrl and calendars
    // [{url, displayName, color, description, components, readOnly}]; needs
    // no store. Defined in caldav_discovery.cpp.
    Q_INVOKABLE bool discoverCalendars(const QString &requestId, const QString &url = {});

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void conflictsChanged();
    void syncFinished(const QString &requestId, const QVariantMap &result);
    void changeFinished(const QString &requestId, const QVariantMap &result);
    void discoveryFinished(const QString &requestId, const QVariantMap &result);

private:
    struct Prepared;
    bool prepare(Prepared *prepared, QString *error) const;
    QVariantMap findRecord(const QString &collection, const QString &id) const;
    QVariantMap remoteMerged(QVariantMap row, const QString &collection, const QString &id,
                             const QVariantMap &fields, const QVariantMap &remote) const;
    void applyResult(const QVariantMap &result);
    QVariantMap persist(QVariantMap result);
    void launch(bool syncSignal, const QString &requestId, const Prepared &prepared);
    void deliver(bool syncSignal, const QString &requestId, QVariantMap result);

    QString m_collectionUrl, m_userName, m_accountId = QStringLiteral("caldav");
    QString m_calendarId = QStringLiteral("caldav"), m_defaultZone = QStringLiteral("UTC");
    int m_timeoutMs = 15000;
    bool m_allowInsecureAuth = false, m_autoSave = true;
    QPointer<LocalProductivityStore> m_store;
    QString m_secret, m_mechanism = QStringLiteral("basic");
    QMap<QString, QVariantMap> m_conflicts;      // "<collection>/<id>" -> conflict entry
    int m_pending = 0;
    QString m_state = QStringLiteral("idle"), m_lastError;
};

} // namespace QindaTK
