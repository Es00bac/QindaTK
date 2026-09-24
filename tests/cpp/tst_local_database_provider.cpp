// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_database_provider.h"

#include <QTest>

using QindaTK::LocalDatabaseProvider;

class LocalDatabaseProviderTest final : public QObject {
    Q_OBJECT
private slots:
    void preparedReadsAndSchemaMetadata()
    {
        LocalDatabaseProvider provider;
        QVERIFY(provider.openMemory());
        const auto migration = provider.previewMigration(
            "CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT NOT NULL, active INTEGER, manager_id INTEGER REFERENCES people(id))");
        QVERIFY(migration.value("ok").toBool());
        QVERIFY(provider.tables().isEmpty()); // preview never executes DDL
        QVERIFY(provider.executeMigration(migration.value("sql").toString()).value("ok").toBool());
        QVERIFY(provider.tables().contains("people"));

        const auto insert = provider.execute("INSERT INTO people(name, active) VALUES(?, ?)",
                                             {QStringLiteral("O'Reilly"), true});
        QVERIFY(insert.value("ok").toBool());
        QCOMPARE(insert.value("rowsAffected").toInt(), 1);
        QVERIFY(provider.execute("INSERT INTO people(name, active, manager_id) VALUES(?, ?, ?)",
                                 {QStringLiteral("Second"), false, QVariant::fromValue<qlonglong>(1)}).value("ok").toBool());
        QVERIFY(!provider.execute("INSERT INTO people(name, active, manager_id) VALUES(?, ?, ?)",
                                  {QStringLiteral("Invalid manager"), true, QVariant::fromValue<qlonglong>(999)}).value("ok").toBool());

        const auto result = provider.query("SELECT id, name, active FROM people WHERE name = ?", {QStringLiteral("O'Reilly")});
        QVERIFY(result.value("ok").toBool());
        const auto rows = result.value("rows").toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("name").toString(), QStringLiteral("O'Reilly"));
        QCOMPARE(rows.first().toMap().value("active").toInt(), 1);

        const auto injection = provider.query("SELECT id FROM people WHERE name = ?", {QStringLiteral("' OR 1=1 --")});
        QVERIFY(injection.value("ok").toBool());
        QVERIFY(injection.value("rows").toList().isEmpty());
        QVERIFY(!provider.query("SELECT 1; DROP TABLE people").value("ok").toBool());
        QVERIFY(!provider.query("DELETE FROM people").value("ok").toBool());
        QVERIFY(!provider.execute("CREATE TABLE unsafe(id INTEGER)").value("ok").toBool());

        const auto schema = provider.schema("people");
        QVERIFY(schema.value("ok").toBool());
        const auto fields = schema.value("fields").toList();
        QCOMPARE(fields.size(), 4);
        QCOMPARE(fields[1].toMap().value("name").toString(), QStringLiteral("name"));
        QVERIFY(fields[1].toMap().value("notNull").toBool());
        QCOMPARE(schema.value("relationships").toList().size(), 1);
    }
};

QTEST_GUILESS_MAIN(LocalDatabaseProviderTest)
#include "tst_local_database_provider.moc"
