// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_database_provider.h"

#include <QByteArray>
#include <QFileInfo>
#include <QMetaType>
#include <QRegularExpression>
#include <sqlite3.h>

namespace QindaTK {
namespace {
QVariant columnValue(sqlite3_stmt *statement, int column)
{
    switch (sqlite3_column_type(statement, column)) {
    case SQLITE_INTEGER: return QVariant::fromValue<qlonglong>(sqlite3_column_int64(statement, column));
    case SQLITE_FLOAT: return sqlite3_column_double(statement, column);
    case SQLITE_TEXT:
        return QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(statement, column)),
                                 sqlite3_column_bytes(statement, column));
    case SQLITE_BLOB:
        return QByteArray(reinterpret_cast<const char *>(sqlite3_column_blob(statement, column)),
                          sqlite3_column_bytes(statement, column));
    default: return {};
    }
}

bool bindValue(sqlite3_stmt *statement, int index, const QVariant &value)
{
    if (!value.isValid() || value.isNull()) return sqlite3_bind_null(statement, index) == SQLITE_OK;
    const int type = value.metaType().id();
    if (type == QMetaType::Bool) return sqlite3_bind_int(statement, index, value.toBool()) == SQLITE_OK;
    if (type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::ULongLong)
        return sqlite3_bind_int64(statement, index, value.toLongLong()) == SQLITE_OK;
    if (type == QMetaType::Double || type == QMetaType::Float)
        return sqlite3_bind_double(statement, index, value.toDouble()) == SQLITE_OK;
    if (type == QMetaType::QByteArray) {
        const QByteArray bytes = value.toByteArray();
        return sqlite3_bind_blob(statement, index, bytes.constData(), bytes.size(), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    const QByteArray utf8 = value.toString().toUtf8();
    return sqlite3_bind_text(statement, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

QVariantMap result(bool ok, const QString &error = {})
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}};
}

// The copy step of a table rebuild (SQLite's documented 12-step ALTER):
// INSERT INTO <table> [(columns)] SELECT ..., into a table the same plan
// created. Other data writes stay out of migration plans.
QString rebuildCopyTarget(const QString &sql)
{
    static const QRegularExpression copy(QStringLiteral("^\\s*INSERT\\s+INTO\\s+\"?(\\w+)\"?\\s*(\\([^)]*\\))?\\s*SELECT\\b"),
                                         QRegularExpression::CaseInsensitiveOption);
    return copy.match(sql).captured(1);
}
QString createdTable(const QString &sql)
{
    static const QRegularExpression create(QStringLiteral("^\\s*CREATE\\s+TABLE\\s+(?:IF\\s+NOT\\s+EXISTS\\s+)?\"?(\\w+)\"?"),
                                           QRegularExpression::CaseInsensitiveOption);
    return create.match(sql).captured(1);
}
} // namespace

LocalDatabaseProvider::LocalDatabaseProvider(QObject *parent) : QObject(parent) {}

LocalDatabaseProvider::~LocalDatabaseProvider()
{
    close();
}

bool LocalDatabaseProvider::open(const QString &path)
{
    close();
    if (path.isEmpty()) { setError(tr("Database path is empty")); return false; }
    const QByteArray native = QFileInfo(path).absoluteFilePath().toUtf8();
    sqlite3 *database = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const int status = sqlite3_open_v2(native.constData(), &database, flags, nullptr);
    if (status != SQLITE_OK) {
        const QString message = database ? QString::fromUtf8(sqlite3_errmsg(database)) : tr("Unable to open SQLite database");
        if (database) sqlite3_close_v2(database);
        setError(message);
        return false;
    }
    m_db = database;
    m_filePath = QString::fromUtf8(native);
    sqlite3_busy_timeout(m_db, 2500);
    sqlite3_exec(m_db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
    setError({});
    emit changed();
    return true;
}

bool LocalDatabaseProvider::openMemory()
{
    close();
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(":memory:", &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        const QString message = database ? QString::fromUtf8(sqlite3_errmsg(database)) : tr("Unable to open in-memory SQLite database");
        if (database) sqlite3_close_v2(database);
        setError(message);
        return false;
    }
    m_db = database;
    m_filePath = QStringLiteral(":memory:");
    sqlite3_busy_timeout(m_db, 2500);
    sqlite3_exec(m_db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
    setError({});
    emit changed();
    return true;
}

void LocalDatabaseProvider::close()
{
    if (m_db) sqlite3_close_v2(m_db);
    m_db = nullptr;
    m_filePath.clear();
    m_rowsAffected = 0;
    emit changed();
}

QVariantList LocalDatabaseProvider::tables() const
{
    QVariantList resultRows;
    if (!m_db) return resultRows;
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT name FROM sqlite_schema WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name",
            -1, &statement, nullptr) != SQLITE_OK) return resultRows;
    while (sqlite3_step(statement) == SQLITE_ROW)
        resultRows.push_back(QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(statement, 0))));
    sqlite3_finalize(statement);
    return resultRows;
}

QVariantMap LocalDatabaseProvider::schema(const QString &tableName) const
{
    if (!m_db) return result(false, tr("Database is closed"));
    if (!tables().contains(tableName)) return result(false, tr("Unknown table"));
    QVariantList fields;
    QVariantList relations;
    auto readPragma = [this, &tableName](const char *sql, QVariantList *output, bool foreignKeys) {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
        const QByteArray name = tableName.toUtf8();
        sqlite3_bind_text(statement, 1, name.constData(), name.size(), SQLITE_TRANSIENT);
        int status = SQLITE_OK;
        while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
            if (foreignKeys) {
                output->push_back(QVariantMap{{"id", sqlite3_column_int(statement, 0)},
                    {"sequence", sqlite3_column_int(statement, 1)},
                    {"table", columnValue(statement, 2)}, {"from", columnValue(statement, 3)},
                    {"to", columnValue(statement, 4)}, {"onUpdate", columnValue(statement, 5)},
                    {"onDelete", columnValue(statement, 6)}});
            } else {
                output->push_back(QVariantMap{{"cid", sqlite3_column_int(statement, 0)},
                    {"name", columnValue(statement, 1)}, {"type", columnValue(statement, 2)},
                    {"notNull", sqlite3_column_int(statement, 3) != 0},
                    {"defaultValue", columnValue(statement, 4)}, {"primaryKeyOrder", sqlite3_column_int(statement, 5)},
                    {"hidden", sqlite3_column_int(statement, 6)}});
            }
        }
        const bool ok = status == SQLITE_DONE;
        sqlite3_finalize(statement);
        return ok;
    };
    if (!readPragma("SELECT cid,name,type,\"notnull\",dflt_value,pk,hidden FROM pragma_table_xinfo(?)", &fields, false)
        || !readPragma("SELECT id,seq,\"table\",\"from\",\"to\",on_update,on_delete FROM pragma_foreign_key_list(?)", &relations, true))
        return result(false, QString::fromUtf8(sqlite3_errmsg(m_db)));
    return {{"ok", true}, {"table", tableName}, {"fields", fields}, {"relationships", relations}};
}

QVariantMap LocalDatabaseProvider::query(const QString &sql, const QVariantList &bindings)
{
    if (!m_db) return failure(tr("Database is closed"));
    sqlite3_stmt *statement = nullptr;
    QString error;
    if (!prepareOne(sql, &statement, &error)) return failure(error);
    if (!sqlite3_stmt_readonly(statement)) { sqlite3_finalize(statement); return failure(tr("Query accepts read-only statements")); }
    if (!bindAll(statement, bindings, &error)) { sqlite3_finalize(statement); return failure(error); }
    QVariantList columns;
    const int columnCount = sqlite3_column_count(statement);
    for (int i = 0; i < columnCount; ++i)
        columns.push_back(QString::fromUtf8(sqlite3_column_name(statement, i)));
    QVariantList rows;
    int status = SQLITE_OK;
    while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
        QVariantMap row;
        for (int i = 0; i < columnCount; ++i)
            row.insert(QString::fromUtf8(sqlite3_column_name(statement, i)), columnValue(statement, i));
        rows.push_back(row);
        if (rows.size() >= 5000) break;
    }
    sqlite3_finalize(statement);
    if (status != SQLITE_DONE && status != SQLITE_ROW) return failure(QString::fromUtf8(sqlite3_errmsg(m_db)));
    setError({});
    return {{"ok", true}, {"columns", columns}, {"rows", rows}, {"truncated", rows.size() >= 5000}};
}

QVariantMap LocalDatabaseProvider::execute(const QString &sql, const QVariantList &bindings)
{
    if (!m_db) return failure(tr("Database is closed"));
    const QString keyword = firstKeyword(sql);
    if (keyword == "CREATE" || keyword == "ALTER" || keyword == "DROP" || keyword == "REINDEX" || keyword == "VACUUM")
        return failure(tr("Use previewMigration and executeMigration for schema changes"));
    sqlite3_stmt *statement = nullptr;
    QString error;
    if (!prepareOne(sql, &statement, &error)) return failure(error);
    if (sqlite3_stmt_readonly(statement)) { sqlite3_finalize(statement); return failure(tr("execute accepts write statements only")); }
    if (!bindAll(statement, bindings, &error)) { sqlite3_finalize(statement); return failure(error); }
    const int status = sqlite3_step(statement);
    if (status != SQLITE_DONE) { error = QString::fromUtf8(sqlite3_errmsg(m_db)); sqlite3_finalize(statement); return failure(error); }
    m_rowsAffected = sqlite3_changes(m_db);
    const qint64 insertId = sqlite3_last_insert_rowid(m_db);
    sqlite3_finalize(statement);
    setError({});
    emit changed();
    emit recordsChanged();
    return {{"ok", true}, {"rowsAffected", m_rowsAffected}, {"lastInsertId", insertId}};
}

QVariantMap LocalDatabaseProvider::previewMigration(const QString &sql) const
{
    const QString keyword = firstKeyword(sql);
    if (keyword != "CREATE" && keyword != "ALTER" && keyword != "DROP")
        return result(false, tr("Only CREATE, ALTER and DROP statements can be previewed"));
    sqlite3_stmt *statement = nullptr;
    QString error;
    if (!prepareOne(sql, &statement, &error)) return result(false, error);
    const bool writable = !sqlite3_stmt_readonly(statement);
    sqlite3_finalize(statement);
    if (!writable) return result(false, tr("Statement does not describe a schema change"));
    return {{"ok", true}, {"operation", keyword.toLower()}, {"sql", sql.trimmed()},
            {"warning", keyword == "DROP" ? tr("This operation removes schema or data") : tr("Preview only; execution requires an explicit host action")}};
}

QVariantMap LocalDatabaseProvider::executeMigration(const QString &sql)
{
    const auto plan = previewMigration(sql);
    if (!plan.value("ok").toBool()) return failure(plan.value("error").toString());
    sqlite3_stmt *statement = nullptr;
    QString error;
    if (!prepareOne(sql, &statement, &error)) return failure(error);
    const int status = sqlite3_step(statement);
    if (status != SQLITE_DONE) { error = QString::fromUtf8(sqlite3_errmsg(m_db)); sqlite3_finalize(statement); return failure(error); }
    sqlite3_finalize(statement);
    m_rowsAffected = sqlite3_changes(m_db);
    setError({});
    emit changed();
    emit recordsChanged();
    return {{"ok", true}, {"rowsAffected", m_rowsAffected}, {"operation", plan.value("operation")}};
}

QVariantMap LocalDatabaseProvider::previewMigrationPlan(const QStringList &statements)
{
    return runMigrationPlan(statements, false);
}

QVariantMap LocalDatabaseProvider::executeMigrationPlan(const QStringList &statements)
{
    return runMigrationPlan(statements, true);
}

QVariantMap LocalDatabaseProvider::runMigrationPlan(const QStringList &statements, bool commit)
{
    if (!m_db) return failure(tr("Database is closed"));
    if (statements.isEmpty() || statements.size() > 64)
        return failure(tr("Migration plan must contain 1–64 statements"));
    char *sqliteError = nullptr;
    if (sqlite3_exec(m_db, "BEGIN IMMEDIATE", nullptr, nullptr, &sqliteError) != SQLITE_OK) {
        const QString message = QString::fromUtf8(sqliteError ? sqliteError : sqlite3_errmsg(m_db));
        sqlite3_free(sqliteError);
        return failure(message);
    }
    const QMap<QString, QVariantMap> relationsBefore = relationshipIndex();
    QStringList operations;
    QStringList created;
    bool destructive = false;
    for (const QString &sql : statements) {
        const QString keyword = firstKeyword(sql);
        if (keyword == QLatin1String("CREATE") && !createdTable(sql).isEmpty()) created.append(createdTable(sql).toLower());
        const bool copy = keyword == QLatin1String("INSERT") && created.contains(rebuildCopyTarget(sql).toLower());
        if (keyword != QLatin1String("CREATE") && keyword != QLatin1String("ALTER")
            && keyword != QLatin1String("DROP") && !copy) {
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return failure(tr("Migration plans accept only CREATE, ALTER, DROP and INSERT ... SELECT into a table the plan creates"));
        }
        sqlite3_stmt *statement = nullptr;
        QString error;
        if (!prepareOne(sql, &statement, &error)) {
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return failure(error);
        }
        if (sqlite3_stmt_readonly(statement)) {
            sqlite3_finalize(statement);
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return failure(tr("Migration plan contains a read-only statement"));
        }
        const int status = sqlite3_step(statement);
        if (status != SQLITE_DONE) error = QString::fromUtf8(sqlite3_errmsg(m_db));
        sqlite3_finalize(statement);
        if (status != SQLITE_DONE) {
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return failure(error);
        }
        operations.append(keyword.toLower());
        destructive |= keyword == QLatin1String("DROP");
    }
    // The plan's relationship diff, read inside the transaction (OFF-34):
    // every foreign key the plan adds or removes, by child table and column.
    const QMap<QString, QVariantMap> relationsAfter = relationshipIndex();
    QVariantList added, removed;
    for (auto it = relationsAfter.cbegin(); it != relationsAfter.cend(); ++it)
        if (!relationsBefore.contains(it.key())) added.append(it.value());
    for (auto it = relationsBefore.cbegin(); it != relationsBefore.cend(); ++it)
        if (!relationsAfter.contains(it.key())) removed.append(it.value());
    // AGENT-GUARD: SQLite DDL is transactional. Preview must roll back the
    // whole executed plan; otherwise dependent statements cannot be checked.
    const char *finish = commit ? "COMMIT" : "ROLLBACK";
    if (sqlite3_exec(m_db, finish, nullptr, nullptr, &sqliteError) != SQLITE_OK) {
        const QString message = QString::fromUtf8(sqliteError ? sqliteError : sqlite3_errmsg(m_db));
        sqlite3_free(sqliteError);
        sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        return failure(message);
    }
    setError({});
    if (commit) {
        m_rowsAffected = sqlite3_changes(m_db);
        emit changed();
        emit recordsChanged();
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("operations"), operations},
            {QStringLiteral("statements"), statements},
            {QStringLiteral("previewOnly"), !commit},
            {QStringLiteral("relationshipChanges"), QVariantMap{{QStringLiteral("added"), added},
                                                                {QStringLiteral("removed"), removed}}},
            {QStringLiteral("warning"), destructive ? tr("Plan includes a DROP statement") : QString()}};
}

QMap<QString, QVariantMap> LocalDatabaseProvider::relationshipIndex() const
{
    QMap<QString, QVariantMap> index;
    for (const QVariant &table : tables()) {
        const QString name = table.toString();
        for (const QVariant &entry : schema(name).value(QStringLiteral("relationships")).toList()) {
            const QVariantMap r = entry.toMap();
            const QVariantMap relation{{QStringLiteral("table"), name}, {QStringLiteral("from"), r.value(QStringLiteral("from"))},
                                       {QStringLiteral("target"), r.value(QStringLiteral("table"))},
                                       {QStringLiteral("to"), r.value(QStringLiteral("to"))},
                                       {QStringLiteral("onUpdate"), r.value(QStringLiteral("onUpdate"))},
                                       {QStringLiteral("onDelete"), r.value(QStringLiteral("onDelete"))}};
            QStringList key;
            for (const QVariant &part : relation) key.append(part.toString());
            index.insert(key.join(QChar(0x1f)), relation);
        }
    }
    return index;
}

bool LocalDatabaseProvider::prepareOne(const QString &sql, sqlite3_stmt **statement, QString *error) const
{
    if (!m_db) { *error = tr("Database is closed"); return false; }
    if (sql.trimmed().isEmpty()) { *error = tr("SQL statement is empty"); return false; }
    const QByteArray utf8 = sql.toUtf8();
    const char *tail = nullptr;
    const int status = sqlite3_prepare_v3(m_db, utf8.constData(), utf8.size(), SQLITE_PREPARE_PERSISTENT, statement, &tail);
    if (status != SQLITE_OK || !*statement) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        if (*statement) sqlite3_finalize(*statement);
        *statement = nullptr;
        return false;
    }
    if (QString::fromUtf8(tail).trimmed().size() > 0) {
        sqlite3_finalize(*statement); *statement = nullptr;
        *error = tr("Only one SQL statement is allowed");
        return false;
    }
    return true;
}

bool LocalDatabaseProvider::bindAll(sqlite3_stmt *statement, const QVariantList &bindings, QString *error) const
{
    const int count = sqlite3_bind_parameter_count(statement);
    if (count != bindings.size()) { *error = tr("Expected %1 bindings, received %2").arg(count).arg(bindings.size()); return false; }
    for (int i = 0; i < count; ++i) {
        if (!bindValue(statement, i + 1, bindings[i])) { *error = QString::fromUtf8(sqlite3_errmsg(m_db)); return false; }
    }
    return true;
}

void LocalDatabaseProvider::setError(QString error)
{
    if (m_error == error) return;
    m_error = std::move(error);
    emit changed();
}

QVariantMap LocalDatabaseProvider::failure(const QString &error)
{
    setError(error);
    return result(false, error);
}

QString LocalDatabaseProvider::firstKeyword(const QString &sql)
{
    static const QRegularExpression word(QStringLiteral("^\\s*([A-Za-z]+)"));
    const auto match = word.match(sql);
    return match.hasMatch() ? match.captured(1).toUpper() : QString();
}

} // namespace QindaTK
