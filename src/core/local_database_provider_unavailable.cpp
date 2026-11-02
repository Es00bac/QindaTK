// SPDX-License-Identifier: LGPL-3.0-or-later
// AGENT-NOTE (D-354, docs/android.md): LocalDatabaseProvider for builds
// without SQLite (QINDATK_ENABLE_SQLITE=OFF, the Android default; the NDK has
// no public SQLite). The QML-visible API of local_database_provider.cpp is
// kept. The provider is never `connected`, so the report, data-design and
// sqlite_query_source consumers take their existing "SQLite database is not
// connected" failure path unchanged.
// AGENT-GUARD: keep this file free of <sqlite3.h>. m_db stays null; the
// private sqlite helpers declared in the header (prepareOne, bindAll,
// runMigrationPlan, relationshipIndex, firstKeyword) are intentionally not
// defined here and nothing in this file may call them.
#include "local_database_provider.h"

namespace QindaTK {

namespace {
QString unavailableText()
{
    return QObject::tr("SQLite is not built into this QindaTK");
}
QVariantMap fail(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}
} // namespace

LocalDatabaseProvider::LocalDatabaseProvider(QObject *parent) : QObject(parent) {}
LocalDatabaseProvider::~LocalDatabaseProvider() = default;

bool LocalDatabaseProvider::open(const QString &) { setError(unavailableText()); return false; }
bool LocalDatabaseProvider::openMemory() { setError(unavailableText()); return false; }
void LocalDatabaseProvider::close() {}
QVariantList LocalDatabaseProvider::tables() const { return {}; }
QVariantMap LocalDatabaseProvider::schema(const QString &) const { return fail(QObject::tr("Database is closed")); }
QVariantMap LocalDatabaseProvider::query(const QString &, const QVariantList &) { return failure(unavailableText()); }
QVariantMap LocalDatabaseProvider::execute(const QString &, const QVariantList &) { return failure(unavailableText()); }
QVariantMap LocalDatabaseProvider::previewMigration(const QString &) const { return fail(unavailableText()); }
QVariantMap LocalDatabaseProvider::executeMigration(const QString &) { return failure(unavailableText()); }
QVariantMap LocalDatabaseProvider::previewMigrationPlan(const QStringList &) { return failure(unavailableText()); }
QVariantMap LocalDatabaseProvider::executeMigrationPlan(const QStringList &) { return failure(unavailableText()); }

void LocalDatabaseProvider::setError(QString error)
{
    if (m_error == error) return;
    m_error = std::move(error);
    emit changed();
}
QVariantMap LocalDatabaseProvider::failure(const QString &error)
{
    setError(error);
    return fail(error);
}

} // namespace QindaTK
