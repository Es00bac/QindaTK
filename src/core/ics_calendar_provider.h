// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "local_productivity_store.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace QindaTK {

// iCalendar files and feeds for LocalProductivityStore events, over the
// CalDAV codec (ical_codec.h): a subscribed feed (https:, http:, webcal:) is
// a read-only replica of one calendar; an imported file becomes ordinary
// local events; an export writes one VCALENDAR atomically.
// AGENT-CONTRACT (CalendarView.qml, CalDavProvider, docs/productivity-providers.md):
// feed records carry id "ics:<accountId>:<calendarId>:<uid>", readOnly true,
// remoteAccount = accountId, remoteUid, remoteIcs, icsFeed (the normalized
// feed URL), feedEtag and feedLastModified; CalDavProvider never pushes a
// record with icsFeed. Imported records have no remoteAccount, keep the UID
// in remoteUid and the source text in remoteIcs (the base an export or a
// later CalDAV upload writes over). result.state is "accepted", "rejected",
// "offline" or "conflict" (a store stale-writer rejection).
// AGENT-GUARD: the secret lives only in memory and is redacted from results.
class IcsCalendarProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(LocalProductivityStore *store READ store WRITE setStore NOTIFY configChanged)
    Q_PROPERTY(QString accountId MEMBER m_accountId NOTIFY configChanged)
    Q_PROPERTY(QString calendarId MEMBER m_calendarId NOTIFY configChanged)
    Q_PROPERTY(QString feedUrl MEMBER m_feedUrl NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(QString defaultZone MEMBER m_defaultZone NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(int maximumBytes MEMBER m_maximumBytes NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(bool autoSave MEMBER m_autoSave NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit IcsCalendarProvider(QObject *parent = nullptr);

    LocalProductivityStore *store() const { return m_store; }
    void setStore(LocalProductivityStore *store);
    bool hasSecret() const { return !m_secret.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }

    // Optional credentials of a private feed: "basic" (userName + password)
    // or "bearer". Sent only over https, to loopback, or with allowInsecureAuth.
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("basic"));
    Q_INVOKABLE void clearSecret();
    // Conditional GET of `feedUrl`; replaces this feed's events in `calendarId`.
    // A 304 is accepted with unchanged: true. Emits syncFinished.
    Q_INVOKABLE bool sync(const QString &requestId);
    // Imports a local .ics (path or file: URL) as editable local events in
    // `calendarId` (this provider's when empty); the same UID updates the
    // existing event. Emits importFinished with imported/updated/unchanged/skipped.
    Q_INVOKABLE bool importFile(const QString &requestId, const QString &path, const QString &calendarId = {});
    // Writes the calendar's events (or only `eventIds`) as one VCALENDAR via
    // QSaveFile. Emits exportFinished with exported and bytes.
    Q_INVOKABLE bool exportFile(const QString &requestId, const QString &path, const QString &calendarId = {},
                                const QStringList &eventIds = {});
    // Parses iCalendar text without touching the store:
    // {ok, error, calendarName, events, tasks, warnings}.
    Q_INVOKABLE QVariantMap parseText(const QString &text) const;
    // Serializes event records: {ok, error, text, count, warnings}.
    Q_INVOKABLE QVariantMap serializeEvents(const QVariantList &events) const;

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void syncFinished(const QString &requestId, const QVariantMap &result);
    void importFinished(const QString &requestId, const QVariantMap &result);
    void exportFinished(const QString &requestId, const QVariantMap &result);

private:
    enum class Operation { Sync, Import, Export };
    struct Validators {
        QString etag, lastModified;
        int eventCount = 0;
    };
    void launch(Operation operation, const QString &requestId, std::function<QVariantMap()> work,
                std::function<QVariantMap(QVariantMap)> apply);
    void deliver(Operation operation, const QString &requestId, QVariantMap result);
    QVariantMap persist(QVariantMap result);
    QVariantMap applyFeed(QVariantMap result, const QString &feed, const QString &calendarId);
    QVariantMap applyImport(QVariantMap result, const QString &calendarId);
    QVariantList feedEvents(const QString &feed, const QString &calendarId) const;
    QString zone() const;

    QPointer<LocalProductivityStore> m_store;
    QString m_accountId = QStringLiteral("ics"), m_calendarId = QStringLiteral("ics");
    QString m_feedUrl, m_userName, m_defaultZone;
    int m_timeoutMs = 15000, m_maximumBytes = 16 * 1024 * 1024;
    bool m_allowInsecureAuth = false, m_autoSave = true;
    QString m_secret, m_mechanism = QStringLiteral("basic");
    QHash<QString, Validators> m_validators;     // "<calendarId>\n<feed>" -> last 200 response
    int m_pending = 0;
    QString m_state = QStringLiteral("idle"), m_lastError;
};

} // namespace QindaTK
