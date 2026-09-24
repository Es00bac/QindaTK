// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

struct sqlite3;
struct sqlite3_stmt;

namespace QindaTK {

// A small, local SQLite adapter. Reads are prepared and parameterized;
// schema mutation has a separate preview and explicit execution path.
class LocalDatabaseProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(int rowsAffected READ rowsAffected NOTIFY changed)
    Q_PROPERTY(QVariantList tables READ tables NOTIFY changed)

public:
    explicit LocalDatabaseProvider(QObject *parent = nullptr);
    ~LocalDatabaseProvider() override;

    bool connected() const { return m_db != nullptr; }
    QString filePath() const { return m_filePath; }
    QString errorText() const { return m_error; }
    int rowsAffected() const { return m_rowsAffected; }

    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE bool openMemory();
    Q_INVOKABLE void close();
    Q_INVOKABLE QVariantList tables() const;
    Q_INVOKABLE QVariantMap schema(const QString &tableName) const;
    Q_INVOKABLE QVariantMap query(const QString &sql, const QVariantList &bindings = {});
    Q_INVOKABLE QVariantMap execute(const QString &sql, const QVariantList &bindings = {});
    Q_INVOKABLE QVariantMap previewMigration(const QString &sql) const;
    Q_INVOKABLE QVariantMap executeMigration(const QString &sql);
    Q_INVOKABLE QVariantMap previewMigrationPlan(const QStringList &statements);
    Q_INVOKABLE QVariantMap executeMigrationPlan(const QStringList &statements);

signals:
    void changed();
    void recordsChanged();

private:
    bool prepareOne(const QString &sql, sqlite3_stmt **statement, QString *error) const;
    bool bindAll(sqlite3_stmt *statement, const QVariantList &bindings, QString *error) const;
    void setError(QString error);
    QVariantMap failure(const QString &error);
    static QString firstKeyword(const QString &sql);
    QVariantMap runMigrationPlan(const QStringList &statements, bool commit);

    sqlite3 *m_db = nullptr;
    QString m_filePath;
    QString m_error;
    int m_rowsAffected = 0;
};

} // namespace QindaTK
