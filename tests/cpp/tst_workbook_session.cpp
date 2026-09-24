// SPDX-License-Identifier: LGPL-3.0-or-later
// The workbook in the shared document framework (docs/workbook.md#sessions):
// DocumentManager sessions, the shared UndoService, recovery and the codec.
#include "canvas_document_format.h"
#include "document_manager.h"
#include "format_registry.h"
#include "job_model.h"
#include "workbook.h"
#include "workbook_delimited.h"
#include "workbook_format.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QWaitCondition>
#include <QtTest>
#include <atomic>

using namespace QindaTK;

namespace {
const QString workbookSchema = QString::fromLatin1(WorkbookFormat::FormatId);

// Real files; one save can be held inside the worker, the next can fail.
class HeldStore final : public DocumentStore {
public:
    SaveReceipt save(const SaveRequest &request) override
    {
        {
            QMutexLocker lock(&m_mutex);
            if (m_holdNext) {
                m_holdNext = false;
                m_held = true;
                m_changed.wakeAll();
                while (m_held && m_changed.wait(&m_mutex, 20000)) {}
                m_held = false;
            }
        }
        if (failNext.exchange(false))
            return {request.snapshot->header, request.path, {}, {ErrorCode::BackendFailure, QStringLiteral("Disk full"), request.path, {}}};
        return m_disk.save(request);
    }
    LoadReceipt load(const QString &path) override { return m_disk.load(path); }
    void holdNextSave()
    {
        QMutexLocker lock(&m_mutex);
        m_holdNext = true;
    }
    bool waitUntilHeld()
    {
        QMutexLocker lock(&m_mutex);
        return m_held || (m_changed.wait(&m_mutex, 5000) && m_held);
    }
    void release()
    {
        QMutexLocker lock(&m_mutex);
        m_held = false;
        m_changed.wakeAll();
    }
    std::atomic_bool failNext = false;

private:
    AtomicDocumentStore m_disk;
    QMutex m_mutex;
    QWaitCondition m_changed;
    bool m_holdNext = false;
    bool m_held = false;
};

Workbook *workbookOf(DocumentSession *session)
{
    return session ? qobject_cast<Workbook *>(session->modelObject()) : nullptr;
}
bool saved(DocumentSession *session, const std::function<bool()> &start)
{
    QSignalSpy finished(session, &DocumentSession::saveFinished);
    if (!start() || !finished.wait(10000)) return false;
    return finished.constFirst().at(0).toBool();
}
QString savedInput(const QString &path, int row, int column)
{
    QFile file(path);
    Workbook reader;
    if (!file.open(QIODevice::ReadOnly) || !reader.restore(file.readAll())) return QStringLiteral("<unreadable>");
    return reader.cellInput(reader.sheetIdAt(0), row, column);
}
QVariantMap waitForJob(FormatRegistry &registry, const QString &id)
{
    if (id.isEmpty()) return {};
    const bool done = QTest::qWaitFor([&] {
        const QString state = registry.jobs()->job(id).value("state").toString();
        return state == QStringLiteral("succeeded") || state == QStringLiteral("failed");
    }, 10000);
    return done ? registry.jobs()->job(id) : QVariantMap();
}
} // namespace

class WorkbookSessionTest : public QObject {
    Q_OBJECT
private slots:
    void createdThroughTheManager();
    void editsDuringAsyncSaveStayDirty();
    void failedSaveBlocksClose();
    void recoveryReopensAUsableRevision();
    void sharedHistoryRecordsWorkbookSteps();
    void codecRoundTripsHeaderAndExtensions();
    void formatRegistryUsesTheWorkbookSchema();
};

void WorkbookSessionTest::createdThroughTheManager()
{
    DocumentManager manager;
    bool listed = false;
    for (const QVariant &type : manager.documentTypes())
        listed = listed || type.toMap().value("schemaId").toString() == workbookSchema;
    QVERIFY(listed);
    DocumentSession *session = manager.createDocument(workbookSchema);
    QVERIFY2(session, qPrintable(manager.lastError()));
    Workbook *wb = workbookOf(session);
    QVERIFY(wb);
    QCOMPARE(session->kind(), QStringLiteral("workbook"));
    QCOMPARE(session->documentId(), wb->documentId());
    QCOMPARE(wb->undoService(), manager.undoService()); // the session adopted its history
    QVERIFY(!wb->canUndo());
    QVERIFY(wb->setCellInput(wb->sheetIdAt(0), 0, 0, "=1+1"));
    QVERIFY(wb->canUndo() && manager.undoService()->canUndo(wb));
    QCOMPARE(session->revision(), wb->revision());
    // A session keeps its document identity: restore of another file and reset are refused.
    Workbook other;
    QVERIFY(!wb->restore(other.serialize()));
    wb->reset();
    QVERIFY(!wb->lastError().isEmpty());
    QCOMPARE(wb->cellDisplay(wb->sheetIdAt(0), 0, 0), QStringLiteral("2"));
}

void WorkbookSessionTest::editsDuringAsyncSaveStayDirty()
{
    QTemporaryDir dir;
    DocumentManager manager;
    auto store = std::make_shared<HeldStore>();
    manager.setStore(store);
    DocumentSession *session = manager.createDocument(workbookSchema);
    Workbook *wb = workbookOf(session);
    const QString s = wb->sheetIdAt(0);
    QVERIFY(wb->setCellInput(s, 0, 0, "before"));
    QVERIFY(session->isDirty());
    const QString path = dir.filePath(QStringLiteral("book.qtkwb"));
    QSignalSpy finished(session, &DocumentSession::saveFinished);
    store->holdNextSave();
    QVERIFY(session->requestSaveAs(path));
    QVERIFY(session->isSaving());
    QVERIFY(store->waitUntilHeld());
    QVERIFY(wb->setCellInput(s, 0, 0, "during")); // lands while the worker writes
    store->release();
    QVERIFY(finished.wait(10000));
    QVERIFY(finished.constFirst().at(0).toBool());
    QCOMPARE(savedInput(path, 0, 0), QStringLiteral("before")); // the captured state
    QVERIFY(session->isDirty());                                 // the later edit is not saved
    QVERIFY(wb->undo());
    QVERIFY(!session->isDirty()); // back at exactly the saved position
    QVERIFY(wb->redo());
    QVERIFY(saved(session, [&] { return session->requestSave(); }));
    QVERIFY(!session->isDirty() && !wb->isModified());
    QCOMPARE(savedInput(path, 0, 0), QStringLiteral("during"));
}

void WorkbookSessionTest::failedSaveBlocksClose()
{
    QTemporaryDir dir;
    DocumentManager manager;
    auto store = std::make_shared<HeldStore>();
    manager.setStore(store);
    DocumentSession *session = manager.createDocument(workbookSchema);
    Workbook *wb = workbookOf(session);
    const QString id = session->documentId();
    QVERIFY(saved(session, [&] { return session->requestSaveAs(dir.filePath(QStringLiteral("book.qtkwb"))); }));
    QVERIFY(wb->setCellInput(wb->sheetIdAt(0), 1, 1, "unsaved"));
    QSignalSpy blocked(&manager, &DocumentManager::closeBlocked);
    QVERIFY(!manager.closeDocument(id, DocumentManager::ClosePolicy::Cancel));
    QCOMPARE(blocked.size(), 1);
    store->failNext = true;
    QVERIFY(manager.closeDocument(id, DocumentManager::ClosePolicy::Save));
    QVERIFY(blocked.wait(10000));
    QCOMPARE(blocked.last().at(1).toString(), QStringLiteral("Disk full"));
    QCOMPARE(manager.findSession(id), session); // a failed save never closes
    QVERIFY(session->isDirty());
    QCOMPARE(wb->cellInput(wb->sheetIdAt(0), 1, 1), QStringLiteral("unsaved"));
    QVERIFY(manager.closeDocument(id, DocumentManager::ClosePolicy::Discard));
    QVERIFY(!manager.findSession(id));
}

void WorkbookSessionTest::recoveryReopensAUsableRevision()
{
    QTemporaryDir dir;
    const QString recoveryDir = dir.filePath(QStringLiteral("recovery"));
    const QString path = dir.filePath(QStringLiteral("book.qtkwb"));
    QString id, revision;
    {
        DocumentManager manager;
        manager.recoveryManager()->setDirectory(recoveryDir);
        DocumentSession *session = manager.createDocument(workbookSchema);
        session->setAutosaveDelayMs(600000); // frames only from the explicit calls here
        Workbook *wb = workbookOf(session);
        const QString s = wb->sheetIdAt(0);
        QVERIFY(wb->setCellInput(s, 0, 0, "saved"));
        QVERIFY(saved(session, [&] { return session->requestSaveAs(path); }));
        QVERIFY(manager.recoveryManager()->inspectId(session->documentId()).isEmpty()); // discarded by the save
        QVERIFY(wb->setCellInput(s, 0, 0, "edited"));
        QVERIFY(wb->applyStyle(s, 0, 0, 0, 0, {{"bold", true}}));
        QVERIFY(session->autosaveNow());
        id = session->documentId();
        revision = session->revision();
        const QVariantMap offered = manager.recoveryManager()->inspectId(id);
        QCOMPARE(offered.value("schemaId").toString(), workbookSchema);
        QCOMPARE(offered.value("revision").toString(), revision);
        QVERIFY(offered.value("codecAvailable").toBool());
    } // the process "stops": the manager and its sessions go away unsaved
    // A torn write at the end of the journal is ignored, not fatal.
    QFile journal(QDir(recoveryDir).filePath(id + QStringLiteral(".journal")));
    QVERIFY(journal.open(QIODevice::Append) && journal.write("QTKRgarbage") > 0);
    journal.close();

    DocumentManager restarted;
    restarted.recoveryManager()->setDirectory(recoveryDir);
    QVERIFY(restarted.recoveryManager()->inspectId(id).value("truncatedTail").toBool());
    DocumentSession *restored = restarted.restoreRecovered(id, path);
    QVERIFY2(restored, qPrintable(restarted.lastError()));
    QVERIFY(restored->recovered() && restored->isDirty());
    QCOMPARE(restored->revision(), revision);
    Workbook *wb = workbookOf(restored);
    const QString s = wb->sheetIdAt(0);
    QCOMPARE(wb->cellInput(s, 0, 0), QStringLiteral("edited"));
    QCOMPARE(wb->cellFormat(s, 0, 0).value("bold").toBool(), true);
    QVERIFY(wb->setCellInput(s, 1, 0, "usable")); // a working document with its own history
    QVERIFY(wb->undo());
    QVERIFY(saved(restored, [&] { return restored->requestSave(); })); // the original file is the baseline
    QVERIFY(!restored->isDirty());
    QCOMPARE(savedInput(path, 0, 0), QStringLiteral("edited"));
    QVERIFY(restarted.recoveryManager()->inspectId(id).isEmpty());
}

void WorkbookSessionTest::sharedHistoryRecordsWorkbookSteps()
{
    DocumentManager manager;
    UndoService *undo = manager.undoService();
    DocumentSession *session = manager.createDocument(workbookSchema);
    Workbook *wb = workbookOf(session);
    const QString s = wb->sheetIdAt(0);
    undo->setActiveModelObject(wb);
    QVERIFY(wb->setCellInput(s, 0, 0, "1"));
    QVERIFY(wb->pasteInputs(s, 1, 0, {QVariant(QVariantList{"2"}), QVariant(QVariantList{"=A1+A2"})}));
    QCOMPARE(undo->undoText(), QStringLiteral("Paste cells"));
    QCOMPARE(wb->undoText(), undo->undoText());
    QCOMPARE(undo->historyEntries().size(), 2);
    QVERIFY(undo->undo());
    QVERIFY(wb->cellInput(s, 2, 0).isEmpty());
    QVERIFY(undo->redo());
    QCOMPARE(wb->cellDisplay(s, 2, 0), QStringLiteral("3"));

    // A macro groups workbook operations into one step; its undo restores both.
    QVERIFY(undo->beginMacro(QStringLiteral("Seed")));
    QVERIFY(wb->setCellInput(s, 0, 1, "x"));
    QVERIFY(wb->insertRows(s, 0, 1));
    QVERIFY(undo->endMacro());
    QCOMPARE(undo->undoText(), QStringLiteral("Seed"));
    QCOMPARE(wb->cellInput(s, 3, 0), QStringLiteral("=A2+A3"));
    QVERIFY(wb->undo());
    QCOMPARE(wb->cellInput(s, 2, 0), QStringLiteral("=A1+A2"));
    QVERIFY(wb->cellInput(s, 0, 1).isEmpty());

    // An edit whose undo data exceeds the budget is refused and changes nothing.
    undo->setMemoryLimit(4096);
    QVariantList rows;
    for (int r = 0; r < 200; ++r) rows.append(QVariant(QVariantList{QString::number(r), QStringLiteral("text %1").arg(r)}));
    const QString before = wb->serializedText();
    QVERIFY(!wb->pasteInputs(s, 10, 0, rows));
    QCOMPARE(wb->lastError(), QStringLiteral("Edit exceeds undo memory limit"));
    QCOMPARE(wb->serializedText(), before);
    QVERIFY(!wb->transactionActive());

    // An explicit transaction is one step; while it is open, history waits.
    undo->setMemoryLimit(8 * 1024 * 1024);
    QVERIFY(wb->beginTransaction(QStringLiteral("Two cells")));
    QVERIFY(wb->setCellInput(s, 5, 5, "a"));
    QVERIFY(!wb->canUndo() && !wb->undo());
    QVERIFY(wb->setCellInput(s, 5, 6, "b"));
    QVERIFY(wb->commitTransaction());
    QCOMPARE(undo->undoText(), QStringLiteral("Two cells"));
    QVERIFY(undo->undo());
    QVERIFY(wb->cellInput(s, 5, 5).isEmpty() && wb->cellInput(s, 5, 6).isEmpty());
}

void WorkbookSessionTest::codecRoundTripsHeaderAndExtensions()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("book.qtkwb"));
    QString id, revision;
    {
        DocumentManager manager;
        DocumentSession *session = manager.createDocument(workbookSchema);
        Workbook *wb = workbookOf(session);
        QVERIFY(wb->setCellInput(wb->sheetIdAt(0), 0, 0, "kept"));
        session->setExtensions({{"org.example.view", QJsonObject{{"zoom", 1.5}}}});
        QVERIFY(saved(session, [&] { return session->requestSaveAs(path); }));
        id = session->documentId();
        revision = session->revision();
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    const DocumentCodecPtr codec = workbookDocumentCodec();
    QVERIFY(codec->recognizes(bytes));
    QVERIFY(!codec->recognizes("\x89PNG"));
    const auto header = codec->inspect(bytes);
    QVERIFY(header);
    QCOMPARE(header->revision.toDecimal(), revision);
    QCOMPARE(QJsonDocument::fromJson(bytes).object().value("revision").toString(), revision);

    DocumentManager reopened;
    DocumentSession *session = reopened.openDocument(path);
    QVERIFY2(session, qPrintable(reopened.lastError()));
    QVERIFY(!session->isDirty());
    QCOMPARE(session->documentId(), id);
    QCOMPARE(session->revision(), revision);
    QCOMPARE(session->extensions().value("org.example.view").toObject().value("zoom").toDouble(), 1.5);
    QCOMPARE(workbookOf(session)->cellInput(workbookOf(session)->sheetIdAt(0), 0, 0), QStringLiteral("kept"));

    // A plain serialize() file (no header) opens as revision 1.
    Workbook plain;
    QVERIFY(plain.setCellInput(plain.sheetIdAt(0), 0, 0, "plain"));
    const QString plainPath = dir.filePath(QStringLiteral("plain.qtkwb"));
    QVERIFY(plain.save(QUrl::fromLocalFile(plainPath)));
    DocumentSession *plainSession = reopened.openDocument(plainPath, workbookSchema);
    QVERIFY2(plainSession, qPrintable(reopened.lastError()));
    QCOMPARE(plainSession->revision(), QStringLiteral("1"));
    QVERIFY(!codec->inspect(QByteArray("{\"format\":\"qindatk.workbook\",\"version\":2,\"revision\":\"x\"}")));
}

void WorkbookSessionTest::formatRegistryUsesTheWorkbookSchema()
{
    QTemporaryDir dir;
    DocumentManager manager;
    FormatRegistry registry;
    Workbook scratch;
    QVERIFY(scratch.registerFormat(&registry));
    DocumentSession *session = manager.createDocument(workbookSchema);
    Workbook *wb = workbookOf(session);
    QVERIFY(wb->pasteInputs(wb->sheetIdAt(0), 0, 0, {QVariant(QVariantList{"a", "1"}), QVariant(QVariantList{"b", "=B1*2"})}));
    const QUrl csv = QUrl::fromLocalFile(dir.filePath(QStringLiteral("out.csv")));
    const QVariantMap job = waitForJob(registry, registry.startExportSession(session, csv, QString::fromLatin1(WorkbookDelimitedFormat::CsvId)));
    QCOMPARE(job.value("state").toString(), QStringLiteral("succeeded"));
    QFile out(csv.toLocalFile());
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), QByteArray("a,1\r\nb,2\r\n"));
    // The workbook providers refuse another document kind before any job runs.
    DocumentSession *canvas = manager.createDocument(QString::fromLatin1(CanvasDocumentFormat::SchemaId));
    QVERIFY(canvas);
    QVERIFY(!registry.preflightExportSession(canvas, csv, QString::fromLatin1(WorkbookDelimitedFormat::CsvId))
                 .value("ok").toBool());
    // An import becomes a new, unsaved workbook session.
    const QVariantMap imported = waitForJob(registry, registry.startImport(csv));
    QCOMPARE(imported.value("state").toString(), QStringLiteral("succeeded"));
    DocumentSession *adopted = registry.adoptImport(&manager, imported.value("id").toString());
    QVERIFY2(adopted, qPrintable(registry.lastError()));
    QCOMPARE(adopted->kind(), QStringLiteral("workbook"));
    QVERIFY(adopted->isDirty());
    QCOMPARE(workbookOf(adopted)->cellDisplay(workbookOf(adopted)->sheetIdAt(0), 1, 1), QStringLiteral("2"));
}

QTEST_MAIN(WorkbookSessionTest)
#include "tst_workbook_session.moc"
