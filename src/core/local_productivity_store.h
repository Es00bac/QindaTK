// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
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

    Q_INVOKABLE bool load();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool loadFrom(const QString &path);
    Q_INVOKABLE bool saveTo(const QString &path);
    Q_INVOKABLE QVariantMap upsert(const QString &collection, const QVariantMap &record);
    Q_INVOKABLE QVariantMap remove(const QString &collection, const QString &recordId);
    Q_INVOKABLE QVariantMap moveTask(const QString &taskId, const QString &toStatus,
                                     int targetOrder = -1);
    Q_INVOKABLE QVariantMap undoTaskMove();
    Q_INVOKABLE QVariantMap exportSnapshot() const;

signals:
    void filePathChanged();
    void dataChanged();
    void statusChanged();
    void taskHistoryChanged();

private:
    QVariantList *collection(const QString &name);
    const QVariantList *collection(const QString &name) const;
    static QString idOf(const QVariantMap &record);
    static bool validateSnapshot(const QVariantMap &snapshot, QString *error);
    static QVariantMap readSnapshot(const QString &path, QString *error);
    static bool writeAtomic(const QString &path, const QByteArray &bytes, QString *error);
    QVariantMap result(bool ok, const QString &error = {}) const;
    void setStatus(const QString &status, const QString &error = {});
    void pushTaskUndo();

    QString m_filePath;
    QVariantList m_messages, m_drafts, m_events, m_tasks;
    QVector<QVariantList> m_taskUndo;
    quint64 m_revision = 0;
    QString m_lastError, m_status = QStringLiteral("empty");
    bool m_recoveryAvailable = false;
};

} // namespace QindaTK
