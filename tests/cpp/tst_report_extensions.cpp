// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_database_provider.h"
#include "local_data_design_document.h"
#include "local_report_document_model.h"
#include "local_report_spec.h"
#include "local_report_groups.h"
#include "rich_text_codec.h"
#include "rich_text_document.h"
#include "rich_text_layout.h"
#include "sqlite_query_source.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
using namespace QindaTK;

class ReportExtensionsTest : public QObject {
    Q_OBJECT
private slots:
    void repeatedGroupsUnknownFieldsAndRunningSums()
    {
        LocalDatabaseProvider db;
        QVERIFY(db.openMemory());
        QVERIFY(db.executeMigration("CREATE TABLE entries(id INTEGER PRIMARY KEY, name TEXT, amount REAL, day TEXT)").value("ok").toBool());
        for (const QVariantList &row : QList<QVariantList>{{1,"Alpha",5,"2026-01-02"}, {2,"Alps",8,"2026-02-03"},
                                                         {3,"Beta",-3,"2026-04-04"}})
            QVERIFY(db.execute("INSERT INTO entries VALUES(?,?,?,?)",row).value("ok").toBool());
        LocalDataDesignDocument design;
        QVERIFY(design.bindTable(&db, "entries"));
        QVariantList groups{
            QVariantMap{{"field","name"},{"interval",QVariantMap{{"by","prefix"},{"size",1}}},{"futureProperty","retained"}},
            QVariantMap{{"field","name"},{"interval",QVariantMap{{"by","prefix"},{"size",2}}}}
        };
        QVERIFY(design.setReportGroups(groups));
        QCOMPARE(design.report().value("groups").toList(), groups);
        QVariantMap report = design.report();
        report.insert("columns", QVariantList{"name","amount"});
        report.insert("runningSums", QVariantList{QVariantMap{{"field","amount"},{"over","group"}},
                                                  QVariantMap{{"field","amount"},{"over","all"}}});
        LocalReportDocumentModel output;
        auto result = output.build(&db,report);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(output.groupCount(),2);
        QVariantList details;
        for (const QVariant &band : output.bands()) if (band.toMap().value("kind") == "detail") details.append(band);
        QCOMPARE(details.size(),3);
        QCOMPARE(details[1].toMap().value("cells").toStringList(), QStringList({"Alps","8","13","13"}));
        QCOMPARE(details[2].toMap().value("cells").toStringList(), QStringList({"Beta","-3","-3","10"}));
        while (groups.size() < 10) groups.append(QVariantMap{{"field","day"}});
        QVERIFY(design.setReportGroups(groups));
        result = output.build(&db,design.report());
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        groups.append(QVariantMap{{"field","day"}});
        QVERIFY(!design.setReportGroups(groups));
        QVERIFY(design.setReportGroups({QVariantMap{{"field","unknown"},{"custom",42}}}));
        QVERIFY(design.bindTable(&db,"entries"));
        QCOMPARE(design.report().value("groups").toList().first().toMap().value("custom").toInt(),42);
        QVERIFY(!output.build(&db,design.report()).value("ok").toBool());
    }
    void intervalKeys()
    {
        LocalDatabaseProvider db;
        QVERIFY(db.openMemory());
        QVERIFY(db.executeMigration("CREATE TABLE samples(id INTEGER, amount REAL, day TEXT)").value("ok").toBool());
        QVERIFY(db.execute("INSERT INTO samples VALUES(1,-0.1,'2026-01-01')").value("ok").toBool());
        QString error;
        auto source = SqliteTableSource::read(&db,"samples",&error);
        QVERIFY2(source,qPrintable(error));
        const QList<QPair<QString,QString>> expected{{"year","2026"},{"quarter","2026-Q1"},{"month","2026-01"},
                                                   {"week","2026-W01"},{"day","2026-01-01"},{"range","-10"}};
        for (const auto &[by,key] : expected) {
            LocalReport::Group group;
            group.field = by == "range" ? "amount" : "day";
            group.intervalBy = by; group.intervalSize = 10;
            LocalReport::GroupSource projected(*source,{group});
            QCOMPARE(projected.rowAt(0,projected.keys()).values.value(projected.keys().first()).data.toString(), key);
        }
    }
    void paginationPoliciesUseDocumentLayout()
    {
        LocalReport::Spec spec;
        spec.title = "Report";
        spec.paperId = "a5";
        LocalReport::Output output;
        output.columnLabels = {"Value"};
        // The long prelude places a short whole group across the next page.
        output.bands.append({LocalReport::Band::Kind::ReportHeader,-1,"Report",{},false,false});
        for (int i=0;i<19;++i) output.bands.append({LocalReport::Band::Kind::ReportInfo,-1,"Prelude line",{},false,false});
        const int groupIndex = output.bands.size();
        output.bands.append({LocalReport::Band::Kind::GroupHeader,0,"Kept group",{},false,false});
        for (int i=0;i<5;++i) output.bands.append({LocalReport::Band::Kind::Detail,0,{}, {QString::number(i)},false,false});
        output.bands.append({LocalReport::Band::Kind::GroupFooter,0,"Kept total",{},false,true});
        output.bands[groupIndex].keepWith = 6;
        const auto json = LocalReport::documentJson(output,spec,QUuid::createUuid().toString(QUuid::WithoutBraces));
        RichTextDocument doc;
        QVERIFY(RichTextCodec::fromJsonObject(json,doc));
        RichTextLayout layout;
        layout.setDocument(&doc); layout.paginateAll();
        QHash<QUuid,int> pages;
        for(int page=0;page<layout.pageCount();++page)
            for(const auto &item:layout.page(page).items) pages.insert(item.block,page);
        int headerPage=-1,footerPage=-1;
        for(const auto &value:json.value("blocks").toArray()) {
            const auto paragraph=value.toObject().value("paragraph").toObject();
            if(paragraph.value("text")=="Kept group") headerPage=pages.value(QUuid(paragraph.value("id").toString()),-1);
            if(paragraph.value("text")=="Kept total") footerPage=pages.value(QUuid(paragraph.value("id").toString()),-1);
        }
        QVERIFY(headerPage>=0); QCOMPARE(headerPage,footerPage);
        // Force-after a section is encoded on the following section, including
        // a detail table, so all output surfaces share the same break.
        output.bands[0].pageBreakAfter=true;
        const auto forced=LocalReport::documentJson(output,spec,QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto second=forced.value("blocks").toArray()[1].toObject().value("paragraph").toObject();
        QVERIFY(second.value("format").toObject().value("pageBreakBefore").toBool());
    }
};
QTEST_MAIN(ReportExtensionsTest)
#include "tst_report_extensions.moc"
