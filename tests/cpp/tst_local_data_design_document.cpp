// SPDX-License-Identifier: LGPL-3.0-or-later
// The form/report design as a session document (WP-059,
// docs/forms-reports.md#design-document): commands and undo, macros,
// DocumentManager save/reopen and crash recovery, version 1 files, and a
// saved design qualified across reversible schema changes.
#include "document_manager.h"
#include "local_data_design_document.h"
#include "local_data_design_store.h"
#include "local_database_provider.h"
#include "local_report_document_model.h"
#include "recovery_manager.h"
#include "rich_text_document.h"
#include "undo_service.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
bool seedSales(LocalDatabaseProvider &db)
{
    if (!db.openMemory()) return false;
    if (!db.executeMigration(QStringLiteral("CREATE TABLE sales(id INTEGER PRIMARY KEY, region TEXT NOT NULL, "
                                            "rep TEXT, amount REAL, units INTEGER)")).value("ok").toBool())
        return false;
    const QList<QVariantList> rows{{1, QStringLiteral("North"), QStringLiteral("Ann"), 10.5, 1},
                                   {2, QStringLiteral("South"), QStringLiteral("Cy"), 7.25, 4},
                                   {3, QStringLiteral("South"), QVariant(), 1.0, QVariant()}};
    for (const QVariantList &row : rows)
        if (!db.execute(QStringLiteral("INSERT INTO sales(id, region, rep, amount, units) VALUES(?, ?, ?, ?, ?)"), row)
                 .value("ok").toBool())
            return false;
    return true;
}

bool saved(DocumentSession *session, const std::function<bool()> &start)
{
    QSignalSpy finished(session, &DocumentSession::saveFinished);
    if (!start() || !finished.wait(10000)) return false;
    return finished.constFirst().at(0).toBool();
}

QStringList formOrder(const LocalDataDesignDocument &doc)
{
    QStringList out;
    for (const QVariant &field : doc.form().value("fields").toList())
        out.append(field.toMap().value("fieldName").toString());
    return out;
}

QString fieldStatus(const QVariantMap &check, const QString &name)
{
    for (const QVariant &field : check.value("fields").toList())
        if (field.toMap().value("name").toString() == name) return field.toMap().value("status").toString();
    return {};
}
} // namespace

class LocalDataDesignDocumentTest final : public QObject {
    Q_OBJECT
private slots:
    void editsAreUndoableCommands()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        LocalDataDesignDocument doc;
        QVERIFY(!doc.canUndo());
        QVERIFY2(doc.bindTable(&db, QStringLiteral("sales")), qPrintable(doc.lastError()));
        QCOMPARE(doc.boundFields(), (QStringList{"id", "region", "rep", "amount", "units"}));
        const QVariantMap types = doc.design().value("binding").toMap().value("types").toMap();
        QCOMPARE(types.value("amount").toString(), QStringLiteral("number"));
        QCOMPARE(types.value("units").toString(), QStringLiteral("integer"));
        QCOMPARE(doc.undoText(), QStringLiteral("Bind to sales"));

        // A refused edit changes nothing, not even the revision.
        const QString revision = doc.revision();
        QVERIFY(!doc.setFormFieldLabel(QStringLiteral("territory"), QStringLiteral("Territory")));
        QVERIFY(!doc.lastError().isEmpty());
        QCOMPARE(doc.revision(), revision);
        QVERIFY(!doc.setReportGroups({"id", "region", "rep", "amount"}));
        QVERIFY(!doc.setReportTotal({}, QStringLiteral("rep"), QStringLiteral("median"), true));
        QVERIFY(!doc.setReportFilter({{"kind", "compare"}, {"field", "units"}}, {}));
        QVERIFY(!doc.setReportPage(QStringLiteral("a3"), QStringLiteral("portrait")));
        QCOMPARE(doc.revision(), revision);

        QVERIFY(doc.setFormFieldLabel(QStringLiteral("rep"), QStringLiteral("Sales rep")));
        QVERIFY(doc.moveFormField(QStringLiteral("amount"), 0));
        QVERIFY(doc.setFormFieldHidden(QStringLiteral("id"), true));
        QCOMPARE(formOrder(doc), (QStringList{"amount", "id", "region", "rep", "units"}));
        QVERIFY(doc.form().value("fields").toList().at(1).toMap().value("hidden").toBool());
        QVERIFY(doc.setReportGroups({QVariantMap{{"field", "region"}}}));
        QVERIFY(doc.setReportTotal(QStringLiteral("region"), QStringLiteral("amount"), QStringLiteral("sum"), true));
        QVERIFY(doc.setReportTotal({}, QStringLiteral("units"), QStringLiteral("countValues"), true));
        QVERIFY(doc.setGroupPageBreak(QStringLiteral("region"), true));
        QVERIFY(doc.setReportFilter({{"kind", "compare"}, {"field", "units"}, {"op", "ge"}, {"parameter", "minUnits"}},
                                    {{"minUnits", QVariantMap{{"type", "integer"}, {"value", "1"}}}}));
        QCOMPARE(doc.undoText(), QStringLiteral("Set report filter"));
        QVERIFY(doc.validate(&db).value("ok").toBool());

        QVERIFY(doc.undo()); // filter
        QVERIFY(doc.undo()); // page break
        QVERIFY(!doc.report().value("groups").toList().first().toMap().value("pageBreakBefore").toBool());
        QVERIFY(doc.redo());
        QVERIFY(doc.report().value("groups").toList().first().toMap().value("pageBreakBefore").toBool());
        for (int i = 0; i < 7; ++i) QVERIFY(doc.undo()); // back to just after binding
        QCOMPARE(formOrder(doc), (QStringList{"id", "region", "rep", "amount", "units"}));
        QCOMPARE(doc.form().value("fields").toList().at(2).toMap().value("label").toString(), QStringLiteral("rep"));
        QVERIFY(doc.report().value("groups").toList().isEmpty());
        QCOMPARE(doc.undoText(), QStringLiteral("Bind to sales"));

        // A macro is one step: undo reverts both edits.
        UndoService *history = doc.undoService();
        QVERIFY(history->beginMacro(QStringLiteral("Seed report")));
        QVERIFY(doc.setReportText(QStringLiteral("headerText"), QStringLiteral("Sales by region")));
        QVERIFY(doc.setReportGroups({"region"}));
        QVERIFY(history->endMacro());
        QCOMPARE(doc.undoText(), QStringLiteral("Seed report"));
        QVERIFY(doc.undo());
        QCOMPARE(doc.report().value("headerText").toString(), QStringLiteral("Data report"));
        QVERIFY(doc.report().value("groups").toList().isEmpty());

        // The report the document hands out runs as is.
        QVERIFY(doc.redo());
        LocalReportDocumentModel output;
        QVERIFY(output.build(&db, doc.report()).value("ok").toBool());
        QCOMPARE(output.groupCount(), 2);
        QCOMPARE(output.columns(), (QStringList{"id", "region", "rep", "amount", "units"}));
    }

    void sessionSaveReopenAndRecovery()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sales.qtkdata"));
        const QString recoveryDir = dir.filePath(QStringLiteral("recovery"));
        QString id, revision;
        {
            DocumentManager manager;
            manager.recoveryManager()->setDirectory(recoveryDir);
            bool listed = false;
            for (const QVariant &type : manager.documentTypes())
                listed = listed || type.toMap().value("schemaId").toString() == LocalDataDesignDocument::schemaId();
            QVERIFY(listed);
            DocumentSession *session = manager.createDocument(LocalDataDesignDocument::schemaId());
            QVERIFY2(session, qPrintable(manager.lastError()));
            session->setAutosaveDelayMs(600000); // frames only from the explicit call below
            auto *doc = qobject_cast<LocalDataDesignDocument *>(session->modelObject());
            QVERIFY(doc);
            QCOMPARE(session->kind(), QStringLiteral("data-design"));
            QCOMPARE(doc->undoService(), manager.undoService()); // the session adopted the history
            QVERIFY(doc->bindTable(&db, QStringLiteral("sales")));
            QVERIFY(doc->setReportGroups({"region"}));
            QVERIFY(saved(session, [&] { return session->requestSaveAs(path); }));
            QVERIFY(!session->isDirty());

            // The store reads the session file and qualifies it against SQLite.
            LocalDataDesignStore store;
            const QVariantMap loaded = store.loadFromFile(path, &db);
            QVERIFY2(loaded.value("ok").toBool(), qPrintable(loaded.value("error").toString()));
            QVERIFY(!loaded.value("design").toMap().contains("session"));

            QVERIFY(doc->setFormTitle(QStringLiteral("Sales entry")));
            QVERIFY(session->isDirty());
            QVERIFY(session->autosaveNow());
            id = session->documentId();
            revision = session->revision();
            QCOMPARE(manager.recoveryManager()->inspectId(id).value("schemaId").toString(),
                     LocalDataDesignDocument::schemaId());
        } // the process "stops" with the title edit unsaved

        DocumentManager restarted;
        restarted.recoveryManager()->setDirectory(recoveryDir);
        DocumentSession *restored = restarted.restoreRecovered(id, path);
        QVERIFY2(restored, qPrintable(restarted.lastError()));
        QVERIFY(restored->recovered() && restored->isDirty());
        QCOMPARE(restored->revision(), revision);
        auto *recoveredDoc = qobject_cast<LocalDataDesignDocument *>(restored->modelObject());
        QVERIFY(recoveredDoc);
        QCOMPARE(recoveredDoc->form().value("title").toString(), QStringLiteral("Sales entry"));
        QVERIFY(recoveredDoc->setFormTitle(QStringLiteral("Sales entry v2"))); // a working document
        QVERIFY(recoveredDoc->undo());

        // The saved file reopens with the same identity and the saved state.
        DocumentManager other;
        DocumentSession *opened = other.openDocument(path);
        QVERIFY2(opened, qPrintable(other.lastError()));
        QCOMPARE(opened->documentId(), id);
        auto *openedDoc = qobject_cast<LocalDataDesignDocument *>(opened->modelObject());
        QVERIFY(openedDoc);
        QCOMPARE(openedDoc->form().value("title").toString(), QStringLiteral("Untitled form"));
        QCOMPARE(openedDoc->report().value("groups").toList().size(), 1);
        QVERIFY(!openedDoc->canUndo());
    }

    void versionOneFilesOpenWithAStableIdentity()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("first-cut.json"));
        const QVariantMap design{{"format", "qindatk.local-data-design"}, {"version", 1}, {"designId", "sales-v1"},
            {"binding", QVariantMap{{"provider", "sqlite"}, {"schema", "main"}, {"table", "sales"},
                                    {"fields", QVariantList{"id", "region", "units"}}}},
            {"form", QVariantMap{{"title", "Sales"}, {"tableName", "sales"},
                                 {"fields", QVariantList{QVariantMap{{"fieldName", "region"}, {"label", "Region"}}}}}},
            {"report", QVariantMap{{"tableName", "sales"}, {"groupField", "region"}, {"sumField", "units"}}}};
        LocalDataDesignStore store;
        QVERIFY(store.saveToFile(path, design, &db).value("ok").toBool());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QJsonObject first;
        SnapshotHeader a, b;
        QString problem;
        QVERIFY2(LocalDataDesignDocument::decodeState(bytes, &first, &a, nullptr, &problem), qPrintable(problem));
        QVERIFY(LocalDataDesignDocument::decodeState(bytes, nullptr, &b, nullptr, &problem));
        QCOMPARE(a.documentId.toString(), b.documentId.toString());
        QCOMPARE(first.value("version").toInt(), 2);

        DocumentManager manager;
        DocumentSession *session = manager.openDocument(path);
        QVERIFY2(session, qPrintable(manager.lastError()));
        QCOMPARE(session->documentId(), a.documentId.toString());
        auto *doc = qobject_cast<LocalDataDesignDocument *>(session->modelObject());
        QVERIFY(doc);
        // The first-cut report keys still run, and the first edit migrates them.
        LocalReportDocumentModel output;
        QVERIFY(output.build(&db, doc->report()).value("ok").toBool());
        QCOMPARE(output.groupCount(), 2);
        QVERIFY(doc->setReportTotal(QStringLiteral("region"), QStringLiteral("units"), QStringLiteral("maximum"), true));
        const QVariantMap report = doc->report();
        QVERIFY(!report.contains("groupField") && !report.contains("sumField"));
        QCOMPARE(report.value("groups").toList().first().toMap().value("totals").toList().size(), 2);
        QVERIFY(output.build(&db, doc->report()).value("ok").toBool());
    }

    void savedDesignFollowsReversibleSchemaChanges()
    {
        LocalDatabaseProvider db;
        QVERIFY(seedSales(db));
        LocalDataDesignDocument doc;
        QVERIFY(doc.bindTable(&db, QStringLiteral("sales")));
        QVERIFY(doc.setReportGroups({QVariantMap{{"field", "rep"},
            {"totals", QVariantList{QVariantMap{{"field", "units"}, {"function", "average"}}}}}}));
        LocalReportDocumentModel output;
        QVERIFY(output.build(&db, doc.report()).value("ok").toBool());
        const QString original = output.document()->text();

        // Rename a bound column: the preview rolls back, the explicit plan applies.
        const QStringList rename{QStringLiteral("ALTER TABLE sales RENAME COLUMN rep TO seller")};
        QVERIFY(db.previewMigrationPlan(rename).value("ok").toBool());
        QVERIFY(doc.validate(&db).value("ok").toBool());
        QVERIFY(db.executeMigrationPlan(rename).value("ok").toBool());
        QVariantMap check = doc.validate(&db);
        QVERIFY(!check.value("ok").toBool());
        QCOMPARE(fieldStatus(check, QStringLiteral("rep")), QStringLiteral("missing"));
        QVariantMap built = output.build(&db, doc.report());
        QCOMPARE(built.value("stage").toString(), QStringLiteral("definition"));
        QCOMPARE(output.document()->text(), original);
        QVERIFY(db.executeMigrationPlan({QStringLiteral("ALTER TABLE sales RENAME COLUMN seller TO rep")})
                    .value("ok").toBool());
        QVERIFY(doc.validate(&db).value("ok").toBool());
        QVERIFY(output.build(&db, doc.report()).value("ok").toBool());
        QCOMPARE(output.document()->text(), original);

        // Change a bound type (SQLite rebuilds the table), then change it back.
        const QStringList toText{
            QStringLiteral("CREATE TABLE sales_v2 AS SELECT id, region, rep, amount, CAST(units AS TEXT) AS units FROM sales"),
            QStringLiteral("DROP TABLE sales"), QStringLiteral("ALTER TABLE sales_v2 RENAME TO sales")};
        QVERIFY(db.previewMigrationPlan(toText).value("ok").toBool());
        QCOMPARE(fieldStatus(doc.validate(&db), QStringLiteral("units")), QStringLiteral("ok"));
        QVERIFY(db.executeMigrationPlan(toText).value("ok").toBool());
        check = doc.validate(&db);
        QVERIFY(!check.value("ok").toBool()); // AVG needs a number
        QCOMPARE(fieldStatus(check, QStringLiteral("units")), QStringLiteral("typeChanged"));
        QCOMPARE(output.build(&db, doc.report()).value("stage").toString(), QStringLiteral("definition"));
        QCOMPARE(output.document()->text(), original);
        const QStringList back{
            QStringLiteral("CREATE TABLE sales_v3 AS SELECT id, region, rep, amount, CAST(units AS INTEGER) AS units FROM sales"),
            QStringLiteral("DROP TABLE sales"), QStringLiteral("ALTER TABLE sales_v3 RENAME TO sales")};
        QVERIFY(db.executeMigrationPlan(back).value("ok").toBool());
        check = doc.validate(&db);
        QVERIFY2(check.value("ok").toBool(), qPrintable(check.value("error").toString()));
        QCOMPARE(fieldStatus(check, QStringLiteral("units")), QStringLiteral("ok"));
        QVERIFY(output.build(&db, doc.report()).value("ok").toBool());
        QCOMPARE(output.document()->text(), original);
    }
};

QTEST_MAIN(LocalDataDesignDocumentTest)
#include "tst_local_data_design_document.moc"
