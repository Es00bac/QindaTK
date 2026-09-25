// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_database_provider.h"

#include <QTest>

using QindaTK::LocalDatabaseProvider;

class LocalDatabaseProviderTest final : public QObject {
    Q_OBJECT
private slots:
    void previewsAndCommitsAtomicMigrationPlan()
    {
        LocalDatabaseProvider provider;
        QVERIFY(provider.openMemory());
        QVERIFY(provider.executeMigration("CREATE TABLE contacts(id INTEGER PRIMARY KEY)").value("ok").toBool());
        const QStringList plan{
            QStringLiteral("ALTER TABLE contacts ADD COLUMN nickname TEXT"),
            QStringLiteral("CREATE INDEX idx_contacts_nickname ON contacts(nickname)")};
        QVERIFY(provider.previewMigrationPlan(plan).value("ok").toBool());
        QCOMPARE(provider.schema(QStringLiteral("contacts")).value("fields").toList().size(), 1);
        const QStringList broken{
            QStringLiteral("ALTER TABLE contacts ADD COLUMN skipped TEXT"),
            QStringLiteral("CREATE INDEX invalid_index ON contacts(missing_field)")};
        QVERIFY(!provider.previewMigrationPlan(broken).value("ok").toBool());
        QCOMPARE(provider.schema(QStringLiteral("contacts")).value("fields").toList().size(), 1);
        QVERIFY(provider.executeMigrationPlan(plan).value("ok").toBool());
        QCOMPARE(provider.schema(QStringLiteral("contacts")).value("fields").toList().size(), 2);
        QVERIFY(!provider.executeMigrationPlan(broken).value("ok").toBool());
        QCOMPARE(provider.schema(QStringLiteral("contacts")).value("fields").toList().size(), 2);
    }

    // OFF-34: a relationship change through a table rebuild previews as a
    // plan that names the foreign keys it adds or removes, touches nothing,
    // and a stale plan fails at commit leaving schema and rows as they were.
    void relationshipRebuildPlans()
    {
        LocalDatabaseProvider provider;
        QVERIFY(provider.openMemory());
        QVERIFY(provider.executeMigration("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT)").value("ok").toBool());
        QVERIFY(provider.executeMigration("CREATE TABLE orders(id INTEGER PRIMARY KEY, person_id INTEGER)").value("ok").toBool());
        QVERIFY(provider.execute("INSERT INTO people(id, name) VALUES(1, 'Mira')").value("ok").toBool());
        QVERIFY(provider.execute("INSERT INTO orders(id, person_id) VALUES(7, 1)").value("ok").toBool());
        auto relationships = [&provider] {
            return provider.schema(QStringLiteral("orders")).value("relationships").toList();
        };
        const QStringList addKey{
            QStringLiteral("CREATE TABLE orders_new(id INTEGER PRIMARY KEY, person_id INTEGER REFERENCES people(id) ON DELETE CASCADE)"),
            QStringLiteral("INSERT INTO orders_new(id, person_id) SELECT id, person_id FROM orders"),
            QStringLiteral("DROP TABLE orders"),
            QStringLiteral("ALTER TABLE orders_new RENAME TO orders")};
        const QVariantMap preview = provider.previewMigrationPlan(addKey);
        QVERIFY2(preview.value("ok").toBool(), qPrintable(preview.value("error").toString()));
        QCOMPARE(preview.value("operations").toStringList(), (QStringList{"create", "insert", "drop", "alter"}));
        QVERIFY(!preview.value("warning").toString().isEmpty()); // the DROP is called out
        const QVariantMap changes = preview.value("relationshipChanges").toMap();
        QCOMPARE(changes.value("added").toList().size(), 1);
        QCOMPARE(changes.value("removed").toList().size(), 0);
        const QVariantMap added = changes.value("added").toList().first().toMap();
        QCOMPARE(added.value("table").toString(), QStringLiteral("orders"));
        QCOMPARE(added.value("from").toString(), QStringLiteral("person_id"));
        QCOMPARE(added.value("target").toString(), QStringLiteral("people"));
        QCOMPARE(added.value("to").toString(), QStringLiteral("id"));
        QCOMPARE(added.value("onDelete").toString(), QStringLiteral("CASCADE"));
        QVERIFY(relationships().isEmpty()); // preview rolled everything back
        QVERIFY(!provider.tables().contains(QStringLiteral("orders_new")));

        // A plain data write is not a migration step; only a rebuild copy is.
        QVERIFY(!provider.previewMigrationPlan({QStringLiteral("INSERT INTO orders(id) SELECT 8")}).value("ok").toBool());

        // The database moves on after the preview; the stale plan fails whole.
        QVERIFY(provider.executeMigration("CREATE TABLE orders_new(x INTEGER)").value("ok").toBool());
        const QVariantMap failed = provider.executeMigrationPlan(addKey);
        QVERIFY(!failed.value("ok").toBool());
        QVERIFY(!provider.errorText().isEmpty());
        QVERIFY(relationships().isEmpty());
        QCOMPARE(provider.schema(QStringLiteral("orders_new")).value("fields").toList().size(), 1);
        QCOMPARE(provider.query("SELECT COUNT(*) AS n FROM orders").value("rows").toList().first().toMap().value("n").toInt(), 1);

        QVERIFY(provider.executeMigration("DROP TABLE orders_new").value("ok").toBool());
        QVERIFY(provider.executeMigrationPlan(addKey).value("ok").toBool());
        QCOMPARE(relationships().size(), 1);
        QCOMPARE(provider.query("SELECT person_id FROM orders WHERE id = 7").value("rows").toList().first().toMap()
                     .value("person_id").toInt(), 1);

        // The inverse rebuild removes the key, and says so.
        const QStringList dropKey{
            QStringLiteral("CREATE TABLE orders_new(id INTEGER PRIMARY KEY, person_id INTEGER)"),
            QStringLiteral("INSERT INTO orders_new SELECT id, person_id FROM orders"),
            QStringLiteral("DROP TABLE orders"),
            QStringLiteral("ALTER TABLE orders_new RENAME TO orders")};
        const QVariantMap inverse = provider.previewMigrationPlan(dropKey);
        QVERIFY(inverse.value("ok").toBool());
        QCOMPARE(inverse.value("relationshipChanges").toMap().value("removed").toList().size(), 1);
        QCOMPARE(inverse.value("relationshipChanges").toMap().value("added").toList().size(), 0);
        QCOMPARE(relationships().size(), 1);
    }

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
