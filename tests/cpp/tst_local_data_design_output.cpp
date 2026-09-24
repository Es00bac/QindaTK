// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_data_design_store.h"
#include "local_database_provider.h"
#include "local_report_document_model.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaTK;

class LocalDataDesignOutputTest final : public QObject {
    Q_OBJECT
private slots:
    void atomicDesignReopenAndPaginatedPdf()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalDatabaseProvider database;
        QVERIFY(database.openMemory());
        QVERIFY(database.executeMigration("CREATE TABLE contacts(id INTEGER PRIMARY KEY, name TEXT NOT NULL, team TEXT NOT NULL)").value("ok").toBool());
        for (int i = 0; i < 100; ++i) {
            const auto inserted = database.execute("INSERT INTO contacts(name, team) VALUES(?, ?)",
                {QStringLiteral("Person %1").arg(i, 3, 10, QLatin1Char('0')),
                 i % 2 ? QStringLiteral("Studio") : QStringLiteral("Research")});
            QVERIFY(inserted.value("ok").toBool());
        }

        const QVariantMap form{{"title", "Contact card"}, {"tableName", "contacts"},
            {"fields", QVariantList{QVariantMap{{"fieldName", "name"}, {"label", "Name"}}}}};
        const QVariantMap report{{"tableName", "contacts"}, {"headerText", "People"},
            {"footerText", "Internal"}, {"filterField", ""}, {"filterValue", ""}, {"groupField", ""}};
        const QVariantMap design{{"format", "qindatk.local-data-design"}, {"version", 1},
            {"designId", "people-directory"},
            {"binding", QVariantMap{{"provider", "sqlite"}, {"schema", "main"},
                {"table", "contacts"}, {"fields", QVariantList{"id", "name", "team"}}}},
            {"form", form}, {"report", report}};

        LocalDataDesignStore store;
        const QString designPath = directory.filePath("contacts.qtkdata.json");
        QVERIFY2(store.saveToFile(designPath, design, &database).value("ok").toBool(), "design should save atomically");
        const QVariantMap reopened = store.loadFromFile(designPath, &database);
        QVERIFY2(reopened.value("ok").toBool(), qPrintable(reopened.value("error").toString()));
        QCOMPARE(reopened.value("design").toMap().value("designId").toString(), QStringLiteral("people-directory"));
        QVERIFY(!store.loadFromFile(designPath, nullptr).value("design").toMap().isEmpty());

        QVariantMap stale = design;
        auto binding = stale.value("binding").toMap();
        binding.insert("fields", QVariantList{"id", "missing_column"});
        stale.insert("binding", binding);
        QVERIFY(!store.validate(stale, &database).value("ok").toBool());

        LocalReportDocumentModel output;
        const QVariantMap built = output.build(&database, report);
        QVERIFY2(built.value("ok").toBool(), qPrintable(built.value("error").toString()));
        QCOMPARE(output.rowCount(), 100);
        QVERIFY(output.document()->text().contains(QStringLiteral("Person 099")));
        const QString pdfPath = directory.filePath("contacts-report.pdf");
        const QVariantMap exported = output.exportPdf(pdfPath);
        QVERIFY2(exported.value("ok").toBool(), qPrintable(exported.value("error").toString()));
        QFile pdf(pdfPath);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        const QByteArray bytes = pdf.readAll();
        QVERIFY(bytes.startsWith("%PDF-"));
        const int pageObjects = bytes.count("/Type /Page");
        QVERIFY2(pageObjects >= 2, qPrintable(QStringLiteral("expected multiple pages, found %1").arg(pageObjects)));
    }
};

QTEST_MAIN(LocalDataDesignOutputTest)
#include "tst_local_data_design_output.moc"
