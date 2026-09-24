// SPDX-License-Identifier: LGPL-3.0-or-later
#include "undo_service.h"
#include "workbook_session.h"
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>

using namespace QindaTK;

namespace {
// Holds each save on the worker until the test releases it.
class BarrierStore final : public DocumentStore {
public:
    QSemaphore entered, release;
    std::atomic_bool failNext = false;
    SaveReceipt save(const SaveRequest &request) override
    {
        entered.release();
        if (!release.tryAcquire(1, 10000)) return {{}, request.path, {}, {ErrorCode::Cancelled, QStringLiteral("barrier"), {}, {}}};
        if (failNext.exchange(false))
            return {{}, request.path, {}, {ErrorCode::BackendFailure, QStringLiteral("Disk full"), request.path, {}}};
        return real.save(request);
    }
    LoadReceipt load(const QString &path) override { return real.load(path); }
    WorkbookDocumentStore real;
};
QString savedInput(const QString &path, int row, int column)
{
    Workbook reader;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !reader.restore(file.readAll())) return QStringLiteral("<unreadable>");
    return reader.cellInput(reader.sheetIdAt(0), row, column);
}
}

class WorkbookSessionTest : public QObject {
    Q_OBJECT
private slots:
    void editsDuringAsyncSaveStayDirty();
    void failedSaveBlocksClose();
    void recoveryReopensAUsableRevision();
    void undoServiceRoutesToTheWorkbook();
};

void WorkbookSessionTest::editsDuringAsyncSaveStayDirty()
{
    QTemporaryDir dir;
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(wb.setCellInput(s, 0, 0, "before"));
    WorkbookSession session;
    auto store = std::make_shared<BarrierStore>();
    session.setStore(store);
    session.setWorkbook(&wb);
    QVERIFY(session.isDirty());
    const QUrl url = QUrl::fromLocalFile(dir.filePath("book.qtkwb"));
    QSignalSpy finished(&session, &WorkbookSession::saveFinished);
    QVERIFY(session.requestSaveAs(url));
    QVERIFY(session.isSaving());
    QVERIFY(!session.requestSave()); // one save at a time
    QVERIFY(store->entered.tryAcquire(1, 10000));
    QVERIFY(wb.setCellInput(s, 0, 0, "during")); // lands while the worker writes
    store->release.release();
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.last().at(0).toBool(), true);
    QCOMPARE(savedInput(url.toLocalFile(), 0, 0), QStringLiteral("before")); // the captured state
    QVERIFY(session.isDirty()); // the edit during the save is not saved
    QCOMPARE(session.title(), QStringLiteral("book.qtkwb"));
    store->release.release();
    QVERIFY(session.requestSave());
    QVERIFY(finished.wait(10000));
    QVERIFY(!session.isDirty());
    QCOMPARE(savedInput(url.toLocalFile(), 0, 0), QStringLiteral("during"));
    // Save As refuses an existing file unless asked to overwrite.
    const QUrl other = QUrl::fromLocalFile(dir.filePath("copy.qtkwb"));
    QFile existing(other.toLocalFile());
    QVERIFY(existing.open(QIODevice::WriteOnly) && existing.write("x") == 1);
    existing.close();
    store->release.release();
    QVERIFY(session.requestSaveAs(other));
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.last().at(0).toBool(), false);
    store->release.release();
    QVERIFY(session.requestSaveAs(other, true));
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.last().at(0).toBool(), true);
}

void WorkbookSessionTest::failedSaveBlocksClose()
{
    QTemporaryDir dir;
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    WorkbookSession session;
    auto store = std::make_shared<BarrierStore>();
    session.setStore(store);
    session.setWorkbook(&wb);
    const QUrl url = QUrl::fromLocalFile(dir.filePath("book.qtkwb"));
    QSignalSpy finished(&session, &WorkbookSession::saveFinished);
    QSignalSpy requested(&session, &WorkbookSession::closeRequested);
    QSignalSpy blocked(&session, &WorkbookSession::closeBlocked);
    QSignalSpy closed(&session, &WorkbookSession::closed);
    store->release.release();
    QVERIFY(session.requestSaveAs(url));
    QVERIFY(finished.wait(10000));
    QVERIFY(wb.setCellInput(s, 1, 1, "unsaved"));
    QVERIFY(!session.requestClose());
    QCOMPARE(requested.size(), 1);
    store->failNext = true;
    store->release.release();
    QVERIFY(session.respondToClose("save"));
    QVERIFY(session.isClosing());
    QVERIFY(finished.wait(10000));
    QCOMPARE(blocked.size(), 1);
    QCOMPARE(blocked.first().first().toString(), QStringLiteral("Disk full"));
    QCOMPARE(closed.size(), 0); // a failed save never closes
    QVERIFY(session.isDirty());
    QVERIFY(!session.isClosing());

    // A file changed behind our back is a conflict: the save fails, still dirty.
    QFile file(url.toLocalFile());
    QVERIFY(file.open(QIODevice::Append) && file.write(" ") == 1);
    file.close();
    store->release.release();
    QVERIFY(session.requestSave());
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.last().at(0).toBool(), false);
    QVERIFY(session.isDirty());
    QVERIFY(!session.respondToClose("cancel"));
    QCOMPARE(closed.size(), 0);
    QVERIFY(session.respondToClose("discard"));
    QCOMPARE(closed.size(), 1);
}

void WorkbookSessionTest::recoveryReopensAUsableRevision()
{
    QTemporaryDir dir;
    RecoveryManager recovery;
    recovery.setDirectory(dir.filePath("recovery"));
    const QUrl url = QUrl::fromLocalFile(dir.filePath("book.qtkwb"));
    QString documentId;
    {
        Workbook wb;
        WorkbookSession session;
        session.setWorkbook(&wb);
        session.setRecovery(&recovery);
        QSignalSpy finished(&session, &WorkbookSession::saveFinished);
        QVERIFY(wb.setCellInput(wb.sheetIdAt(0), 0, 0, "saved"));
        QVERIFY(session.requestSaveAs(url));
        QVERIFY(finished.wait(10000));
        QVERIFY(session.inspectRecovery().isEmpty()); // a successful save discards frames
        QVERIFY(wb.setCellInput(wb.sheetIdAt(0), 0, 0, "edited"));
        QVERIFY(wb.applyStyle(wb.sheetIdAt(0), 0, 0, 0, 0, {{"bold", true}}));
        QVERIFY(session.recordRecovery());
        documentId = wb.documentId();
        QCOMPARE(session.inspectRecovery().value("revision").toString(), wb.revision());
    } // the process "stops" without saving

    // A torn write at the end of the journal is ignored, not fatal.
    QFile journal(QDir(dir.filePath("recovery")).filePath(documentId + ".journal"));
    QVERIFY(journal.open(QIODevice::Append) && journal.write("\x51\x54\x4b\x52garbage") > 0);
    journal.close();

    Workbook reopened;
    WorkbookSession session;
    session.setWorkbook(&reopened);
    session.setRecovery(&recovery);
    QVERIFY(session.open(url));
    QCOMPARE(reopened.cellInput(reopened.sheetIdAt(0), 0, 0), QStringLiteral("saved"));
    const QVariantMap found = session.inspectRecovery();
    QVERIFY(found.value("truncatedTail").toBool());
    QVERIFY(session.restoreRecovery());
    QCOMPARE(reopened.cellInput(reopened.sheetIdAt(0), 0, 0), QStringLiteral("edited"));
    QCOMPARE(reopened.cellFormat(reopened.sheetIdAt(0), 0, 0).value("bold").toBool(), true);
    QVERIFY(session.isDirty()); // recovered, not saved
    QCOMPARE(session.url(), url);
    QVERIFY(reopened.setCellInput(reopened.sheetIdAt(0), 1, 0, "usable")); // a working document
    QVERIFY(session.discardRecovery());
    QVERIFY(session.inspectRecovery().isEmpty());
    QVERIFY(!session.restoreRecovery());

    // Autosave records frames while dirty.
    session.setAutosaveInterval(10);
    QVERIFY(reopened.setCellInput(reopened.sheetIdAt(0), 2, 0, "autosaved"));
    QTRY_VERIFY_WITH_TIMEOUT(!session.inspectRecovery().isEmpty(), 5000);
}

void WorkbookSessionTest::undoServiceRoutesToTheWorkbook()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    UndoService service;
    QSignalSpy changed(&service, &UndoService::changed);
    service.setActiveParticipant(&wb);
    QCOMPARE(service.activeParticipant(), &wb);
    QVERIFY(!service.canUndo());
    QVERIFY(wb.setCellInput(s, 0, 0, "1"));
    QVERIFY(changed.size() >= 2); // the workbook's historyChanged reaches the service
    QVERIFY(service.canUndo());
    QCOMPARE(service.undoText(), QStringLiteral("Edit A1"));
    QVERIFY(!service.isClean());
    QVERIFY(service.undo());
    QCOMPARE(wb.cellInput(s, 0, 0), QString());
    QVERIFY(service.canRedo());
    QVERIFY(service.redo());
    service.markClean();
    QVERIFY(service.isClean());
    QVERIFY(!wb.isModified());
    QVERIFY(service.historyEntries().isEmpty()); // property-document history is not shown for a participant
    QVERIFY(!service.beginMacro("macro"));
    service.setActiveParticipant(&service); // not a participant
    QCOMPARE(service.lastError(), QStringLiteral("Not an undo participant"));
    QCOMPARE(service.activeParticipant(), &wb);
    service.setActiveParticipant(nullptr);
    QVERIFY(!service.canUndo());
    QVERIFY(!service.undo());
}

QTEST_MAIN(WorkbookSessionTest)
#include "tst_workbook_session.moc"
