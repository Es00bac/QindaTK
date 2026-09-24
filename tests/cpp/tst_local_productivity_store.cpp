// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/local_productivity_store.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class LocalProductivityStoreTest : public QObject {
    Q_OBJECT
private slots:
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
        QindaTK::LocalProductivityStore store;
        store.setFilePath(path);
        QVERIFY(store.upsert(QStringLiteral("messages"), {
            {QStringLiteral("id"), QStringLiteral("m-1")},
            {QStringLiteral("messageId"), QStringLiteral("m-1")},
            {QStringLiteral("bodyPlain"), QStringLiteral("cached offline")}}).value("ok").toBool());
        QVERIFY(store.upsert(QStringLiteral("drafts"), {
            {QStringLiteral("id"), QStringLiteral("d-1")},
            {QStringLiteral("subject"), QStringLiteral("Draft")},
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
};

QTEST_MAIN(LocalProductivityStoreTest)
#include "tst_local_productivity_store.moc"
