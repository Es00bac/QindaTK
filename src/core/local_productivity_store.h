// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QByteArray>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Local offline-first persistence for mail, calendar and task snapshots.
// Remote transport/delivery is deliberately outside this store.
class LocalProductivityStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath NOTIFY filePathChanged)
    Q_PROPERTY(QString defaultPath READ defaultPath CONSTANT)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY dataChanged)
    Q_PROPERTY(QVariantList drafts READ drafts NOTIFY dataChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY dataChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY dataChanged)
    Q_PROPERTY(QString revision READ revisionText NOTIFY dataChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool canUndoTaskMove READ canUndoTaskMove NOTIFY taskHistoryChanged)
    Q_PROPERTY(bool canUndoEventChange READ canUndoEventChange NOTIFY eventHistoryChanged)
public:
    explicit LocalProductivityStore(QObject *parent = nullptr);
    QString filePath() const { return m_filePath; }
    QString defaultPath() const;
    void setFilePath(const QString &path);
    QVariantList messages() const { return m_messages; }
    QVariantList drafts() const { return m_drafts; }
    QVariantList events() const { return m_events; }
    QVariantList tasks() const { return m_tasks; }
    QString revisionText() const;
    QString lastError() const { return m_lastError; }
    QString status() const { return m_status; }
    bool recoveryAvailable() const { return m_recoveryAvailable; }
    bool canUndoTaskMove() const { return !m_taskUndo.isEmpty(); }
    bool canUndoEventChange() const { return !m_eventUndo.isEmpty(); }

    Q_INVOKABLE bool load();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool loadFrom(const QString &path);
    Q_INVOKABLE bool saveTo(const QString &path);
    Q_INVOKABLE QVariantMap upsert(const QString &collection, const QVariantMap &record);
    // AGENT-CONTRACT (D-324, docs/productivity-store.md#batch-upsert): inserts
    // or replaces many records of one collection as ONE change — one
    // revision, one dataChanged — matching stable IDs through the ID index
    // (O(n + k), not O(n·k)). A later record with an ID earlier in the batch
    // replaces it. A record without an ID refuses the whole batch and
    // changes nothing. Returns {ok, error, revision, inserted, updated}.
    Q_INVOKABLE QVariantMap upsertMany(const QString &collection, const QVariantList &records);
    // The stable-ID index: a record's position in its collection (-1 when
    // absent) and the record itself ({} when absent). Built lazily per
    // collection and dropped on every dataChanged.
    Q_INVOKABLE int indexOf(const QString &collection, const QString &recordId) const;
    Q_INVOKABLE QVariantMap record(const QString &collection, const QString &recordId) const;
    Q_INVOKABLE QVariantMap remove(const QString &collection, const QString &recordId);
    Q_INVOKABLE QVariantMap moveTask(const QString &taskId, const QString &toStatus,
                                     int targetOrder = -1);
    Q_INVOKABLE QVariantMap undoTaskMove();
    // Moves an event by local wall minutes. scope "series" moves the whole
    // event (its exception and override keys follow); "occurrence" moves one
    // instance of a recurring event as an override; "following" splits the
    // series at `occurrenceId` and moves that part (the result names the new
    // event in `eventId`). All-day events move by whole days (minutes % 1440).
    Q_INVOKABLE QVariantMap shiftEventLocal(const QString &eventId, int minutes,
                                            const QString &scope = QStringLiteral("series"),
                                            const QString &occurrenceId = {});
    // Replaces the recurrence rule (RecurrenceService shape); an empty map
    // makes the event single. Exceptions/overrides the new rule no longer
    // produces are dropped.
    Q_INVOKABLE QVariantMap setEventRecurrence(const QString &eventId, const QVariantMap &rule);
    // Adds or removes an EXDATE-style exclusion for one occurrence.
    Q_INVOKABLE QVariantMap setOccurrenceExcluded(const QString &eventId, const QString &occurrenceId,
                                                  bool excluded);
    // Overrides title/description/location/startLocal/endLocal of one
    // occurrence (RECURRENCE-ID); an empty map restores the series values.
    Q_INVOKABLE QVariantMap setOccurrenceOverride(const QString &eventId, const QString &occurrenceId,
                                                  const QVariantMap &fields);
    Q_INVOKABLE QVariantMap undoEventChange();
    Q_INVOKABLE QVariantMap exportSnapshot() const;

signals:
    void filePathChanged();
    void dataChanged();
    void statusChanged();
    void taskHistoryChanged();
    void eventHistoryChanged();

private:
    const QHash<QString, int> &idIndex(const QString &name) const;
    QVariantList *collection(const QString &name);
    const QVariantList *collection(const QString &name) const;
    static QString idOf(const QVariantMap &record);
    static bool validateSnapshot(const QVariantMap &snapshot, QString *error);
    static QVariantMap readSnapshot(const QString &path, QString *error,
                                    QByteArray *sourceBytes = nullptr);
    static bool writeAtomic(const QString &path, const QByteArray &bytes, QString *error);
    QVariantMap result(bool ok, const QString &error = {}) const;
    void setStatus(const QString &status, const QString &error = {});
    void pushTaskUndo();
    int eventIndex(const QString &eventId) const;
    QVariantMap eventError(const QString &error);
    void commitEvents(const QVariantList &previous);

    QString m_filePath;
    QString m_expectedFilePath;
    QByteArray m_expectedFileDigest;
    QString m_expectedDiskRevision;
    QVariantList m_messages, m_drafts, m_events, m_tasks;
    mutable QHash<QString, QHash<QString, int>> m_idIndex; // collection -> id -> row
    QVector<QVariantList> m_taskUndo;
    QVector<QVariantList> m_eventUndo;
    quint64 m_revision = 0;
    QString m_lastError, m_status = QStringLiteral("empty");
    bool m_recoveryAvailable = false;
    bool m_expectedFileExists = false;
};

} // namespace QindaTK
