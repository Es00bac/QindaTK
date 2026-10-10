// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/local_productivity_store.h"
#include "../../src/core/rich_text_document.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

class LocalProductivityStoreTest : public QObject {
    Q_OBJECT
private slots:
    void staleWriterGetsConflictAndCanReloadThenRetry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("shared.json"));
        QindaTK::LocalProductivityStore seed;
        seed.setFilePath(path);
        QVERIFY(seed.upsert(QStringLiteral("messages"), {
            {QStringLiteral("id"), QStringLiteral("base")},
            {QStringLiteral("subject"), QStringLiteral("Shared start")}}).value("ok").toBool());
        QVERIFY(seed.save());

        QindaTK::LocalProductivityStore first;
        QindaTK::LocalProductivityStore stale;
        QVERIFY(first.loadFrom(path));
        QVERIFY(stale.loadFrom(path));
        QVERIFY(first.upsert(QStringLiteral("messages"), {
            {QStringLiteral("id"), QStringLiteral("first")},
            {QStringLiteral("subject"), QStringLiteral("First writer")}}).value("ok").toBool());
        QVERIFY(first.save());
        QFile recoveryBeforeFile(path + QStringLiteral(".recovery"));
        QVERIFY(recoveryBeforeFile.open(QIODevice::ReadOnly));
        const QByteArray recoveryBefore = recoveryBeforeFile.readAll();
        recoveryBeforeFile.close();
        QVERIFY(stale.upsert(QStringLiteral("messages"), {
            {QStringLiteral("id"), QStringLiteral("stale")},
            {QStringLiteral("subject"), QStringLiteral("Stale writer")}}).value("ok").toBool());
        QVERIFY(!stale.save());
        QCOMPARE(stale.status(), QStringLiteral("conflict"));
        QVERIFY(stale.lastError().contains(QStringLiteral("reload")));

        QindaTK::LocalProductivityStore disk;
        QVERIFY(disk.loadFrom(path));
        const QVariantList diskMessages = disk.messages();
        QCOMPARE(diskMessages.size(), 2);
        QVERIFY(std::none_of(diskMessages.cbegin(), diskMessages.cend(), [](const QVariant &row) {
            return row.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("stale");
        }));
        QFile recoveryAfterFile(path + QStringLiteral(".recovery"));
        QVERIFY(recoveryAfterFile.open(QIODevice::ReadOnly));
        QCOMPARE(recoveryAfterFile.readAll(), recoveryBefore);

        QVERIFY(stale.load());
        QVERIFY(stale.upsert(QStringLiteral("messages"), {
            {QStringLiteral("id"), QStringLiteral("stale")},
            {QStringLiteral("subject"), QStringLiteral("Reapplied after reload")}}).value("ok").toBool());
        QVERIFY(stale.save());
        QindaTK::LocalProductivityStore merged;
        QVERIFY(merged.loadFrom(path));
        QCOMPARE(merged.messages().size(), 3);
    }

    void shiftsAndUndoesCalendarEventInLocalWallTime()
    {
        QindaTK::LocalProductivityStore store;
        QVERIFY(store.upsert(QStringLiteral("events"), {{QStringLiteral("id"), QStringLiteral("e-1")},
            {QStringLiteral("startLocal"), QStringLiteral("2026-09-24 10:00")},
            {QStringLiteral("endLocal"), QStringLiteral("2026-09-24 10:45")},
            {QStringLiteral("zone"), QStringLiteral("America/Denver")}}).value("ok").toBool());
        QVERIFY(store.shiftEventLocal(QStringLiteral("e-1"), 60).value("ok").toBool());
        QCOMPARE(store.events().first().toMap().value(QStringLiteral("startLocal")).toString(),
                 QStringLiteral("2026-09-24 11:00"));
        QVERIFY(store.canUndoEventChange());
        QVERIFY(store.undoEventChange().value("ok").toBool());
        QCOMPARE(store.events().first().toMap().value(QStringLiteral("startLocal")).toString(),
                 QStringLiteral("2026-09-24 10:00"));
        QVERIFY(store.upsert(QStringLiteral("events"), {{QStringLiteral("id"), QStringLiteral("e-1")},
            {QStringLiteral("startLocal"), QStringLiteral("2026-03-08 01:30")},
            {QStringLiteral("endLocal"), QStringLiteral("2026-03-08 01:45")},
            {QStringLiteral("zone"), QStringLiteral("America/Denver")}}).value("ok").toBool());
        QVERIFY(!store.shiftEventLocal(QStringLiteral("e-1"), 60).value("ok").toBool());
        QCOMPARE(store.events().first().toMap().value(QStringLiteral("startLocal")).toString(),
                 QStringLiteral("2026-03-08 01:30"));
    }

    void persistsCollectionsRecoversAndUndoesTaskMove()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("productivity.json"));
        const QString richDocumentJson = QStringLiteral(R"({"schema":"qindatk.richtext","version":1,"documentId":"00000000-0000-4000-8000-000000000001","page":{"widthPt":612,"heightPt":792,"marginsPt":[72,72,72,72]},"defaults":{"family":"Sans Serif","sizePt":11},"formats":[{}],"lists":[],"objects":[],"blocks":[{"paragraph":{"id":"00000000-0000-4000-8000-000000000002","text":"Formatted body","spans":[[14,0]],"mark":0}}]})");
        QindaTK::LocalProductivityStore store;
        store.setFilePath(path);
        QVERIFY(store.upsert(QStringLiteral("messages"), {
            {QStringLiteral("id"), QStringLiteral("m-1")},
            {QStringLiteral("messageId"), QStringLiteral("m-1")},
            {QStringLiteral("bodyPlain"), QStringLiteral("cached offline")}}).value("ok").toBool());
        QVERIFY(store.upsert(QStringLiteral("drafts"), {
            {QStringLiteral("id"), QStringLiteral("d-1")},
            {QStringLiteral("subject"), QStringLiteral("Draft")},
            {QStringLiteral("body"), QStringLiteral("Legacy plain body")},
            {QStringLiteral("richDocumentJson"), richDocumentJson},
            {QStringLiteral("attachments"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("a-1")}, {QStringLiteral("name"), QStringLiteral("map.pdf")}}}}}).value("ok").toBool());
        QVERIFY(store.upsert(QStringLiteral("events"), {
            {QStringLiteral("id"), QStringLiteral("e-1")},
            {QStringLiteral("recurrenceRule"), QVariantMap{{QStringLiteral("frequency"), QStringLiteral("weekly")}, {QStringLiteral("interval"), 2}}}}).value("ok").toBool());
        QVERIFY(store.upsert(QStringLiteral("tasks"), {
            {QStringLiteral("id"), QStringLiteral("t-1")},
            {QStringLiteral("status"), QStringLiteral("todo")}, {QStringLiteral("order"), 0}}).value("ok").toBool());
        QVERIFY(store.upsert(QStringLiteral("tasks"), {
            {QStringLiteral("id"), QStringLiteral("t-2")},
            {QStringLiteral("status"), QStringLiteral("todo")}, {QStringLiteral("order"), 1}}).value("ok").toBool());
        QVERIFY(store.save());

        QVERIFY(store.moveTask(QStringLiteral("t-1"), QStringLiteral("doing")).value("ok").toBool());
        QCOMPARE(store.tasks().first().toMap().value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
        QCOMPARE(store.tasks().last().toMap().value(QStringLiteral("order")).toInt(), 0);
        QVERIFY(store.undoTaskMove().value("ok").toBool());
        QCOMPARE(store.tasks().first().toMap().value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
        QVERIFY(store.moveTask(QStringLiteral("t-1"), QStringLiteral("doing")).value("ok").toBool());
        QVERIFY(store.save());
        QVERIFY(store.recoveryAvailable());

        QFile corrupt(path);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(corrupt.write("broken"), qint64(6));
        corrupt.close();

        QindaTK::LocalProductivityStore reopened;
        QVERIFY(reopened.loadFrom(path));
        QCOMPARE(reopened.status(), QStringLiteral("recovered"));
        QCOMPARE(reopened.messages().size(), 1);
        const QVariantMap reopenedDraft = reopened.drafts().first().toMap();
        QCOMPARE(reopenedDraft.value(QStringLiteral("id")).toString(), QStringLiteral("d-1"));
        QCOMPARE(reopenedDraft.value(QStringLiteral("body")).toString(), QStringLiteral("Legacy plain body"));
        QVERIFY(reopenedDraft.value(QStringLiteral("richDocumentJson")).toString().contains(QStringLiteral("Formatted body")));
        QindaTK::RichTextDocument restoredBody;
        QVERIFY(restoredBody.loadJson(reopenedDraft.value(QStringLiteral("richDocumentJson")).toString()));
        QCOMPARE(restoredBody.text(), QStringLiteral("Formatted body"));
        QCOMPARE(reopened.drafts().first().toMap().value(QStringLiteral("attachments")).toList().size(), 1);
        QCOMPARE(reopened.events().first().toMap().value(QStringLiteral("recurrenceRule")).toMap().value(QStringLiteral("frequency")).toString(), QStringLiteral("weekly"));
        QCOMPARE(reopened.tasks().first().toMap().value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
        QVERIFY(reopened.upsert(QStringLiteral("messages"), {
            {QStringLiteral("id"), QStringLiteral("m-2")},
            {QStringLiteral("messageId"), QStringLiteral("m-2")}}).value("ok").toBool());
        QVERIFY(reopened.save());
        QindaTK::LocalProductivityStore repaired;
        QVERIFY(repaired.loadFrom(path));
        QCOMPARE(repaired.messages().size(), 2);
    }

    // D-324: a sync hands thousands of records at once — one change, one
    // signal, matched through the ID index; a bad batch changes nothing.
    void upsertManyIsOneChangeThroughTheIdIndex()
    {
        QindaTK::LocalProductivityStore store;
        QSignalSpy changed(&store, &QindaTK::LocalProductivityStore::dataChanged);
        QVariantList batch;
        for (int i = 0; i < 5000; ++i)
            batch.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("m-%1").arg(i)},
                                     {QStringLiteral("subject"), QStringLiteral("Message %1").arg(i)}});
        QVariantMap outcome = store.upsertMany(QStringLiteral("messages"), batch);
        QVERIFY2(outcome.value(QStringLiteral("ok")).toBool(), qPrintable(outcome.value(QStringLiteral("error")).toString()));
        QCOMPARE(outcome.value(QStringLiteral("inserted")).toInt(), 5000);
        QCOMPARE(outcome.value(QStringLiteral("updated")).toInt(), 0);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(store.messages().size(), 5000);
        QCOMPARE(store.indexOf(QStringLiteral("messages"), QStringLiteral("m-4321")), 4321);
        QCOMPARE(store.record(QStringLiteral("messages"), QStringLiteral("m-7")).value(QStringLiteral("subject")).toString(),
                 QStringLiteral("Message 7"));

        // Updates and inserts in one batch; a repeated ID in the batch: the later one wins.
        const QString revision = store.revisionText();
        outcome = store.upsertMany(QStringLiteral("messages"), {
            QVariantMap{{QStringLiteral("id"), QStringLiteral("m-7")}, {QStringLiteral("subject"), QStringLiteral("Edited")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("new-1")}, {QStringLiteral("subject"), QStringLiteral("First")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("new-1")}, {QStringLiteral("subject"), QStringLiteral("Second")}}});
        QVERIFY(outcome.value(QStringLiteral("ok")).toBool());
        QCOMPARE(outcome.value(QStringLiteral("inserted")).toInt(), 1);
        QCOMPARE(outcome.value(QStringLiteral("updated")).toInt(), 2);
        QCOMPARE(changed.count(), 2);
        QVERIFY(store.revisionText() != revision);
        QCOMPARE(store.messages().size(), 5001);
        QCOMPARE(store.indexOf(QStringLiteral("messages"), QStringLiteral("m-7")), 7);
        QCOMPARE(store.record(QStringLiteral("messages"), QStringLiteral("new-1")).value(QStringLiteral("subject")).toString(),
                 QStringLiteral("Second"));

        // A record without an ID refuses the whole batch.
        const QString before = store.revisionText();
        outcome = store.upsertMany(QStringLiteral("messages"), {
            QVariantMap{{QStringLiteral("id"), QStringLiteral("m-1")}, {QStringLiteral("subject"), QStringLiteral("Lost")}},
            QVariantMap{{QStringLiteral("subject"), QStringLiteral("No id")}}});
        QVERIFY(!outcome.value(QStringLiteral("ok")).toBool());
        QVERIFY(outcome.value(QStringLiteral("error")).toString().contains(QStringLiteral("Record 2")));
        QCOMPARE(store.revisionText(), before);
        QCOMPARE(store.record(QStringLiteral("messages"), QStringLiteral("m-1")).value(QStringLiteral("subject")).toString(),
                 QStringLiteral("Message 1"));
        QVERIFY(!store.upsertMany(QStringLiteral("folders"), {}).value(QStringLiteral("ok")).toBool());
        QCOMPARE(changed.count(), 2);

        // The index follows other edits: remove shifts the rows after it.
        QVERIFY(store.remove(QStringLiteral("messages"), QStringLiteral("m-0")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(store.indexOf(QStringLiteral("messages"), QStringLiteral("m-7")), 6);
        QCOMPARE(store.indexOf(QStringLiteral("messages"), QStringLiteral("m-0")), -1);
        QCOMPARE(store.indexOf(QStringLiteral("tasks"), QStringLiteral("m-7")), -1);
        QVERIFY(store.record(QStringLiteral("messages"), QStringLiteral("missing")).isEmpty());
    }
};

QTEST_MAIN(LocalProductivityStoreTest)
#include "tst_local_productivity_store.moc"
