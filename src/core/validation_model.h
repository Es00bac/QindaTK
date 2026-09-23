// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct ValidationEntry final {
    QString code;
    int severity = 0;
    EntityId target;
    QString fieldPath;
    QString message;
    QString remedy;
};

// AGENT-CONTRACT: validators return immutable, snapshot-tagged results to
// ValidationModel; the model accepts them only while both the run and the
// document snapshot are current. Worker code carries IDs and values only.
class ValidationModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList issues READ issues NOTIFY issuesChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged)
    Q_PROPERTY(bool invalid READ invalid NOTIFY stateChanged)
    Q_PROPERTY(QString runId READ runId NOTIFY stateChanged)
    Q_PROPERTY(QString documentId READ documentId NOTIFY stateChanged)
    Q_PROPERTY(QString revision READ revision NOTIFY stateChanged)
    Q_PROPERTY(QString resetGeneration READ resetGeneration NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap firstIssue READ firstIssue NOTIFY issuesChanged)
    Q_PROPERTY(QString firstIssueTargetId READ firstIssueTargetId NOTIFY issuesChanged)
    Q_PROPERTY(QString firstIssuePath READ firstIssuePath NOTIFY issuesChanged)

public:
    enum Severity { Error = 0, Warning, Info };
    Q_ENUM(Severity)

    explicit ValidationModel(QObject *parent = nullptr);

    [[nodiscard]] QVariantList issues() const;
    [[nodiscard]] bool pending() const { return m_pending; }
    [[nodiscard]] bool invalid() const;
    [[nodiscard]] QString runId() const;
    [[nodiscard]] QString documentId() const { return m_snapshot.documentId.toString(); }
    [[nodiscard]] QString revision() const { return m_snapshot.revision.toDecimal(); }
    [[nodiscard]] QString resetGeneration() const;
    [[nodiscard]] QVariantMap firstIssue() const;
    [[nodiscard]] QString firstIssueTargetId() const;
    [[nodiscard]] QString firstIssuePath() const;

    // Owner updates the authoritative snapshot whenever the document changes.
    // A change invalidates an in-flight run and removes feedback for old data.
    Q_INVOKABLE bool setSnapshot(const QString &documentId, const QString &revision,
                                 const QString &resetGeneration);
    bool setSnapshot(const SnapshotHeader &snapshot);

    // Start a run for the current snapshot. The decimal result is safe to send
    // through QML/JavaScript and is also the completion correlation token.
    Q_INVOKABLE QString beginRun();
    [[nodiscard]] quint64 beginRun(const SnapshotHeader &snapshot);

    // Typed worker completion. snapshot is the one actually validated.
    bool completeRun(quint64 runId, const SnapshotHeader &snapshot,
                     const QVector<ValidationEntry> &entries);
    // QML completion carries the snapshot actually checked, as exact strings,
    // plus issue maps with code, severity, targetId, fieldPath, message and remedy.
    Q_INVOKABLE bool completeRun(const QString &runId, const QString &documentId,
                                 const QString &revision, const QString &resetGeneration,
                                 const QVariantList &entries);
    Q_INVOKABLE void clear();

signals:
    void issuesChanged();
    void stateChanged();
    void runCompleted(const QString &runId, bool accepted);

private:
    [[nodiscard]] bool matchesCurrent(const SnapshotHeader &snapshot) const;
    [[nodiscard]] QVariantMap entryMap(const ValidationEntry &entry) const;
    static bool parseEntry(const QVariantMap &map, ValidationEntry *entry);
    void notifyAll();

    SnapshotHeader m_snapshot;
    QVector<ValidationEntry> m_entries;
    quint64 m_runId = 0;
    bool m_pending = false;
};

} // namespace QindaTK

Q_DECLARE_METATYPE(QindaTK::ValidationEntry)
