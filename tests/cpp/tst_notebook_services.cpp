// SPDX-License-Identifier: LGPL-3.0-or-later
// NotebookDocument with the common services: DocumentSession save, reopen and
// recovery, the resource resolver, the script runtime, chart data, native
// format validation and a 10,000-block notebook (WP-057, docs/notebooks.md).
#include "command_registry.h"
#include "document_codec.h"
#include "document_manager.h"
#include "notebook_document.h"
#include "resource_resolver.h"
#include "undo_service.h"
#include "script_runtime.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace QindaTK;

namespace {
QString textOf(const NotebookDocument &doc, const QString &id)
{
    return doc.block(id).value("data").toMap().value("text").toString();
}

// 200 pages x 50 blocks; every fifth block nests under the one before it and
// every block carries a bold run, so the file exercises tree and run checks.
QByteArray largeNotebook(QStringList *pageIds)
{
    QJsonArray pages;
    for (int p = 0; p < 200; ++p) {
        QJsonArray blocks;
        QString previous;
        for (int b = 0; b < 50; ++b) {
            const QString id = QStringLiteral("b-%1-%2").arg(p).arg(b);
            const QString text = QStringLiteral("Page %1 block %2").arg(p).arg(b);
            QJsonObject block{{"id", id}, {"type", "text"},
                              {"data", QJsonObject{{"text", text},
                                                   {"runs", QJsonArray{QJsonObject{{"length", 4}, {"bold", true}},
                                                                       QJsonObject{{"length", int(text.size()) - 4}}}}}}};
            if (b % 5 == 4) block.insert("parentId", previous);
            else previous = id;
            blocks.append(block);
        }
        const QString pageId = QStringLiteral("page-%1").arg(p);
        pageIds->append(pageId);
        pages.append(QJsonObject{{"id", pageId}, {"sectionId", p < 100 ? "s-a" : "s-b"},
                                 {"title", QStringLiteral("Page %1").arg(p)}, {"tags", QJsonArray{}}, {"blocks", blocks}});
    }
    return QJsonDocument(QJsonObject{{"format", "QindaTK.Notebook"}, {"version", 1},
        {"documentId", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"revision", "1"},
        {"resetGeneration", "0"}, {"title", "Large"},
        {"sections", QJsonArray{QJsonObject{{"id", "s-a"}, {"title", "A"}}, QJsonObject{{"id", "s-b"}, {"title", "B"}}}},
        {"pages", pages}}).toJson(QJsonDocument::Compact);
}
} // namespace

class TstNotebookServices final : public QObject {
    Q_OBJECT
private slots:
    void sessionSaveReopenAndRecover()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("field.qtknb");
        const QString recovery = dir.filePath("recovery");
        QString documentId, a, b;
        {
            DocumentManager manager;
            manager.recoveryManager()->setDirectory(recovery);
            auto *session = manager.createDocument(QString::fromLatin1(NotebookDocument::SchemaId));
            QVERIFY2(session, qPrintable(manager.lastError()));
            session->setAutosaveDelayMs(600000);
            auto *doc = qobject_cast<NotebookDocument *>(session->modelObject());
            QVERIFY(doc);
            QCOMPARE(doc->undoService(), manager.undoService());
            const QString page = doc->addPage(doc->addSection("Field"), {}, "Day");
            a = doc->insertBlock(page, {}, "text", {{"text", "Saved text"}});
            QVERIFY(doc->undo());
            QVERIFY(doc->block(a).isEmpty());
            QVERIFY(doc->redo());
            QCOMPARE(textOf(*doc, a), QStringLiteral("Saved text"));
            QSignalSpy finished(session, &DocumentSession::saveFinished);
            QVERIFY(session->requestSaveAs(path));
            QVERIFY(finished.wait(10000));
            QVERIFY(finished.first().first().toBool());
            QVERIFY(!session->isDirty());
            documentId = session->documentId();
            b = doc->insertBlock(page, a, "text", {{"text", "Unsaved but recovered"}});
            QVERIFY(session->isDirty());
            QVERIFY(session->autosaveNow());
            QCOMPARE(manager.recoveryManager()->inspectId(documentId).value("schemaId").toString(),
                     QString::fromLatin1(NotebookDocument::SchemaId));
        }
        DocumentManager restarted;
        restarted.recoveryManager()->setDirectory(recovery);
        auto *restored = restarted.restoreRecovered(documentId, path);
        QVERIFY2(restored, qPrintable(restarted.lastError()));
        auto *doc = qobject_cast<NotebookDocument *>(restored->modelObject());
        QVERIFY(doc);
        QCOMPARE(textOf(*doc, b), QStringLiteral("Unsaved but recovered"));
        QVERIFY(restored->recovered() && restored->isDirty());
        QSignalSpy saved(restored, &DocumentSession::saveFinished);
        QVERIFY(restored->requestSave());
        QVERIFY(saved.wait(10000));
        QVERIFY(saved.first().first().toBool());
        QVERIFY(restarted.closeDocument(documentId, DocumentManager::ClosePolicy::Discard));
        auto *reopened = restarted.openDocument(path);
        QVERIFY2(reopened, qPrintable(restarted.lastError()));
        QCOMPARE(reopened->documentId(), documentId);
        QCOMPARE(reopened->schemaId(), QString::fromLatin1(NotebookDocument::SchemaId));
        auto *opened = qobject_cast<NotebookDocument *>(reopened->modelObject());
        QVERIFY(opened);
        QCOMPARE(textOf(*opened, a), QStringLiteral("Saved text"));
        QCOMPARE(textOf(*opened, b), QStringLiteral("Unsaved but recovered"));
        QVERIFY(!opened->canUndo());
    }

    void largeNotebookReopensIdentically()
    {
        QStringList pageIds;
        const QByteArray bytes = largeNotebook(&pageIds);
        NotebookDocument doc;
        QVERIFY2(doc.loadJson(QString::fromUtf8(bytes)), qPrintable(doc.lastError()));
        QCOMPARE(doc.blockCount(), 10000);
        QCOMPARE(doc.block("b-7-4").value("parentId").toString(), QStringLiteral("b-7-3"));

        // One edit retains only the pages it touched (implicit sharing).
        UndoService *undo = doc.undoService();
        const quint64 retained = undo->retainedBytes(&doc);
        QVERIFY(doc.setBlockText("b-150-10", "Changed"));
        QVERIFY(undo->retainedBytes(&doc) - retained < 64 * 1024);

        // Structural edits across a large notebook, then undo them all.
        const QVariantList firstPage = doc.blocks(pageIds.at(0));
        QVERIFY(doc.moveBlock("b-0-0", pageIds.at(199), {}, 0)); // to another page
        const QVariantMap caret = doc.deleteSelection({{"anchorBlockId", "b-0-10"}, {"anchorOffset", 4},
                                                       {"focusBlockId", "b-0-40"}, {"focusOffset", 4}});
        QCOMPARE(caret.value("blockId").toString(), QStringLiteral("b-0-10"));
        QCOMPARE(textOf(doc, "b-0-10"), QStringLiteral("Page 0 block 40"));
        QVERIFY(doc.undo());
        QVERIFY(doc.undo());
        QCOMPARE(doc.blocks(pageIds.at(0)), firstPage);

        // Session codec round trip: capture, decode, adopt.
        const DocumentCodecPtr codec = DocumentCodecRegistry::shared()->codec(QString::fromLatin1(NotebookDocument::SchemaId));
        QVERIFY(codec);
        OperationError error;
        const DocumentCapturePtr capture = codec->capture(doc, {}, &error);
        QVERIFY2(capture, qPrintable(error.message));
        const QByteArray encoded = capture->encode(&error);
        QVERIFY(codec->recognizes(encoded.left(256)));
        const DecodedDocumentPtr decoded = codec->decode(encoded, &error);
        QVERIFY2(decoded, qPrintable(error.message));
        const std::unique_ptr<DocumentModel> adopted = codec->adopt(*decoded, &error);
        QVERIFY(adopted);
        const auto *copy = dynamic_cast<const NotebookDocument *>(adopted.get());
        QVERIFY(copy);
        QVERIFY(copy->state() == doc.state());
        QCOMPARE(copy->documentId(), doc.documentId());
        QCOMPARE(copy->revision(), doc.revision());
    }

    void nativeFormatRejectsInvalidTreesAndKeepsUnknownBlocks()
    {
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const QString a = doc.insertBlock(page, {}, "text", {{"text", "Parent"}});
        const QString b = doc.insertBlock(page, a, "text", {{"text", "Child"}});
        QVERIFY(doc.indentBlock(b));
        QJsonObject root = QJsonDocument::fromJson(doc.toJson().toUtf8()).object();
        auto withBlocks = [&](const QJsonArray &blocks) {
            QJsonObject copy = root;
            QJsonArray pages = copy.value("pages").toArray();
            QJsonObject first = pages.at(0).toObject();
            first.insert("blocks", blocks);
            pages[0] = first;
            copy.insert("pages", pages);
            return QString::fromUtf8(QJsonDocument(copy).toJson(QJsonDocument::Compact));
        };
        const QJsonArray blocks = root.value("pages").toArray().at(0).toObject().value("blocks").toArray();
        NotebookDocument target;
        QVERIFY(!target.loadJson(withBlocks(QJsonArray{blocks.at(1), blocks.at(0)})));
        QVERIFY(target.lastError().contains(QStringLiteral("does not follow")));
        QVERIFY(!target.loadJson(withBlocks(QJsonArray{blocks.at(0), blocks.at(0)})));
        QJsonObject badRuns = blocks.at(0).toObject();
        badRuns.insert("data", QJsonObject{{"text", "abc"}, {"runs", QJsonArray{QJsonObject{{"length", 5}}}}});
        QVERIFY(!target.loadJson(withBlocks(QJsonArray{badRuns})));

        QJsonObject unknown{{"id", "future-1"}, {"type", "sketch-3d"},
                            {"data", QJsonObject{{"mesh", "cube"}, {"version", 7}}}};
        QVERIFY2(target.loadJson(withBlocks(QJsonArray{blocks.at(0), blocks.at(1), unknown})), qPrintable(target.lastError()));
        QCOMPARE(target.block("future-1").value("supported").toBool(), false);
        QCOMPARE(target.block("future-1").value("data").toMap().value("mesh").toString(), QStringLiteral("cube"));
        NotebookDocument again;
        QVERIFY(again.loadJson(target.toJson()));
        QCOMPARE(again.block("future-1"), target.block("future-1"));
    }

    void missingAssetsArePreservedAndReported()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile present(dir.filePath("present.png"));
        QVERIFY(present.open(QIODevice::WriteOnly));
        present.write("not really a png");
        present.close();
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const QString missing = doc.insertBlock(page, {}, "image", {{"resourceId", "photo-1"}, {"caption", "East inlet"}});
        const QString ready = doc.insertBlock(page, missing, "image", {{"resourceId", "photo-2"}});
        const QString loose = doc.insertBlock(page, ready, "image", {{"resourceId", "photo-3"}});
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource("photo-1", QUrl::fromLocalFile(dir.filePath("missing.png")).toString()));
        QVERIFY(resolver.registerResource("photo-2", QUrl::fromLocalFile(dir.filePath("present.png")).toString()));
        auto status = [&](const NotebookDocument &notebook) {
            QStringList result;
            for (const QVariant &row : notebook.assetStatus(&resolver)) {
                const QVariantMap map = row.toMap();
                result.append(map.value("blockId").toString() + u'=' + map.value("availability").toString()
                              + (map.value("missing").toBool() ? QStringLiteral("!") : QString()));
            }
            return result;
        };
        const QStringList expected{missing + "=missing!", ready + "=ready", loose + "=unregistered!"};
        QCOMPARE(status(doc), expected);
        QCOMPARE(doc.resourceIds(), QStringList({"photo-1", "photo-2", "photo-3"}));
        const QString path = dir.filePath("assets.qtknb");
        QVERIFY(doc.saveToFile(path));
        NotebookDocument reopened;
        QVERIFY(reopened.loadFromFile(path));
        QCOMPARE(reopened.block(missing).value("data").toMap().value("caption").toString(), QStringLiteral("East inlet"));
        QCOMPARE(status(reopened), expected);
    }

    void codeBlocksEvaluateThroughTheScriptRuntime()
    {
        NotebookDocument doc;
        const QString page = doc.addPage(doc.addSection("S"), {}, "P");
        const QString table = doc.insertBlock(page, {}, "table", {{"columns", QVariantList{"x", "y"}},
            {"rows", QVariantList{QVariantList{"1", "2"}, QVariantList{"3", "4"}}}});
        const QString code = doc.insertBlock(page, table, "code", {{"text",
            "const rows = app.document.blocks[0].rows; app.print(rows.length);"
            " return rows.map(r => Number(r[1])).reduce((a, b) => a + b, 0)"}});
        ScriptRuntime runtime;
        QSignalSpy evaluated(&doc, &NotebookDocument::blockEvaluated);
        QVERIFY2(doc.evaluateBlock(code, &runtime), qPrintable(doc.lastError()));
        QVERIFY(evaluated.wait(5000));
        QCOMPARE(evaluated.first().at(0).toString(), code);
        QCOMPARE(evaluated.first().at(1).toBool(), true);
        const QVariantMap data = doc.block(code).value("data").toMap();
        QCOMPARE(data.value("status").toString(), QStringLiteral("completed"));
        QCOMPARE(data.value("output").toString(), QStringLiteral("2"));
        QCOMPARE(data.value("result").toDouble(), 6.0);
        QVERIFY(doc.undo());
        QVERIFY(!doc.block(code).value("data").toMap().contains("status"));

        const QString python = doc.insertBlock(page, code, "code", {{"text", "print(1)"}, {"language", "python"}});
        QVERIFY(!doc.evaluateBlock(python, &runtime));
    }

    void chartSeriesReadsTableRowsByStableId()
    {
        NotebookDocument doc;
        const QString section = doc.addSection("S");
        const QString page = doc.addPage(section, {}, "P");
        const QString table = doc.insertBlock(page, {}, "table", {{"columns", QVariantList{"Time", "Level"}},
            {"rows", QVariantList{QVariantList{"0", "1.5"}, QVariantList{"1", "2.5"}, QVariantList{"2", "x"}}}});
        const QString chart = doc.insertBlock(page, table, "chart", {{"sourceBlockId", table}, {"kind", "bar"},
                                                                     {"xColumn", 0}, {"yColumn", 1}});
        const QStringList rowIds = doc.block(table).value("data").toMap().value("rowIds").toStringList();
        QCOMPARE(rowIds.size(), 3);
        QVariantMap series = doc.chartSeries(chart);
        QCOMPARE(series.value("valid").toBool(), true);
        QCOMPARE(series.value("categorical").toBool(), false);
        QCOMPARE(series.value("name").toString(), QStringLiteral("Level"));
        QCOMPARE(series.value("kind").toString(), QStringLiteral("bar"));
        QVariantList points = series.value("points").toList();
        QCOMPARE(points.size(), 3);
        QCOMPARE(points.at(0).toMap().value("pointId").toString(), rowIds.at(0));
        QCOMPARE(points.at(1).toMap().value("y").toDouble(), 2.5);
        QCOMPARE(points.at(2).toMap().value("gap").toBool(), true);
        QCOMPARE(series.value("yMin").toDouble(), 0.0);
        QCOMPARE(series.value("yMax").toDouble(), 2.5);
        QVERIFY(doc.setTableCell(table, 2, 1, "4"));
        points = doc.chartSeries(chart).value("points").toList();
        QCOMPARE(points.at(2).toMap().value("y").toDouble(), 4.0);
        QCOMPARE(points.at(2).toMap().value("pointId").toString(), rowIds.at(2));

        // Copying the table with its chart rewires the copy to the new table.
        const QString other = doc.addPage(section, page, "Q");
        doc.insertRange(other, {}, 0, doc.selectionRange({{"anchorBlockId", table}, {"focusBlockId", chart}}));
        const QVariantList copied = doc.blocks(other);
        QCOMPARE(copied.size(), 2);
        const QString newTable = copied.at(0).toMap().value("id").toString();
        QVERIFY(newTable != table);
        QCOMPARE(copied.at(1).toMap().value("data").toMap().value("sourceBlockId").toString(), newTable);
        QVERIFY(doc.removeBlock(table));
        QCOMPARE(doc.chartSeries(chart).value("valid").toBool(), false);
    }
};

QTEST_MAIN(TstNotebookServices)
#include "tst_notebook_services.moc"
