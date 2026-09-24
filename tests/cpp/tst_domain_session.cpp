// SPDX-License-Identifier: LGPL-3.0-or-later
// One application with a PropertyDocument session and a CanvasDocument
// session over the shared session, history, recovery, workspace and format
// boundaries (WP-033 domain sessions).
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "dock_model.h"
#include "document_manager.h"
#include "edit_transaction.h"
#include "format_registry.h"
#include "workspace_session.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QWaitCondition>
#include <QtTest>

using namespace QindaTK;

namespace {
const QString canvasSchema = QString::fromLatin1(CanvasDocumentFormat::SchemaId);

// Real files, with one save that can be held inside the worker.
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
                // Bounded, so a failed test cannot leave the worker parked.
                while (m_held && m_changed.wait(&m_mutex, 20000)) {}
                m_held = false;
            }
        }
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
private:
    AtomicDocumentStore m_disk;
    QMutex m_mutex;
    QWaitCondition m_changed;
    bool m_holdNext = false;
    bool m_held = false;
};

bool setGain(DocumentManager &manager, DocumentSession *session, EntityId target, double value)
{
    EditTransaction edit;
    edit.setDocument(session->document());
    edit.setUndoService(manager.undoService());
    return edit.begin({target}, QStringLiteral("gain"), QStringLiteral("Set gain"))
        && !edit.updateValue(value) && !edit.commitEdit();
}

double gain(DocumentSession *session, EntityId target)
{
    return session->document()->value(target, QStringLiteral("gain")).toDouble();
}

QByteArray content(DocumentSession *session)
{
    return CanvasDocumentFormat::encode(qobject_cast<CanvasDocument *>(session->modelObject())->state());
}

CanvasDocument *canvasOf(DocumentSession *session)
{
    return qobject_cast<CanvasDocument *>(session ? session->modelObject() : nullptr);
}

CanvasTiledImage smallImage()
{
    CanvasTiledImage image;
    image.size = QSize(40, 24);
    image.tileSize = 32;
    for (int column = 0; column < image.columns(); ++column) {
        QImage tile(image.tileRect(column, 0).size(), QImage::Format_ARGB32_Premultiplied);
        tile.fill(qPremultiply(qRgba(20 * column, 90, 200, 160)));
        image.tiles << tile;
    }
    return image;
}

bool saved(DocumentSession *session, const std::function<bool()> &start)
{
    QSignalSpy finished(session, &DocumentSession::saveFinished);
    if (!start() || !finished.wait(10000)) return false;
    return finished.constFirst().at(0).toBool();
}

QVariantMap window()
{
    return {{QStringLiteral("windowId"), QStringLiteral("main")},
            {QStringLiteral("screenId"), QStringLiteral("screen")},
            {QStringLiteral("geometry"), QVariantMap{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                                                     {QStringLiteral("width"), 800},
                                                     {QStringLiteral("height"), 600}}}};
}

const QVariantList screens{QVariantMap{{QStringLiteral("id"), QStringLiteral("screen")},
    {QStringLiteral("availableRect"), QVariantMap{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                                                  {QStringLiteral("width"), 1280},
                                                  {QStringLiteral("height"), 800}}}}};
} // namespace

class TstDomainSession final : public QObject {
    Q_OBJECT
private slots:
    void propertyAndCanvasSessionsShareServicesIndependently();
    void canvasRecoveryAfterExternalSaveConflict();
    void recoveryFailuresKeepTheirFiles();
    void staleReceiptsMarkOnlyTheCapturedPosition();
    void formatRegistryUsesTheSchemaBoundary();
};

void TstDomainSession::propertyAndCanvasSessionsShareServicesIndependently()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DocumentManager manager;
    manager.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("recovery")));
    DocumentSession *notes = manager.createReferenceDocument();
    QVERIFY(notes && notes->document());
    QVERIFY(notes->document()->defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                               0.0, QStringLiteral("dB"), true, true}));
    const EntityId track = EntityId::create();
    QVERIFY(notes->document()->addEntity(track));
    DocumentSession *board = manager.createDocument(canvasSchema);
    QVERIFY(board);
    QVERIFY(!board->document()); // generic consumers use model/kind/schema
    QCOMPARE(board->kind(), QStringLiteral("canvas"));
    QCOMPARE(board->schemaId(), canvasSchema);
    CanvasDocument *canvas = canvasOf(board);
    QVERIFY(canvas);
    QCOMPARE(canvas->undoService(), manager.undoService());

    // A standalone history cannot be recorded by a second service; adoption
    // into a session moves it instead of duplicating it.
    PropertyDocument loose;
    UndoService first, second;
    QVERIFY(first.addDocument(&loose));
    QVERIFY(!second.addDocument(&loose));
    QVERIFY(second.adoptModel(&loose) && !first.hasModel(&loose));
    auto standalone = std::make_unique<CanvasDocument>();
    CanvasDocument *sketch = standalone.get();
    QVERIFY(sketch->addRect(0, 0, 5, 5, Qt::red) != 0);
    UndoService *local = sketch->undoService();
    QVERIFY(!first.addModel(sketch));
    DocumentSession *sketchSession = manager.create(std::move(standalone));
    QVERIFY(sketchSession);
    QCOMPARE(sketch->undoService(), manager.undoService());
    QVERIFY(!local->hasModel(sketch));
    QVERIFY(sketch->canUndo());
    QVERIFY(manager.closeDocument(sketchSession->documentId(), DocumentManager::ClosePolicy::Discard));

    QVERIFY(setGain(manager, notes, track, 1.0));
    QVERIFY(setGain(manager, notes, track, 2.0));
    QVERIFY(canvas->addRect(10, 10, 40, 30, Qt::red) != 0);
    const CanvasImageId image = canvas->addImage(smallImage());
    QVERIFY(image != 0);
    CanvasObject picture;
    picture.shape = CanvasShape::Image;
    picture.image = image;
    picture.rect = QRectF(0, 0, 40, 24);
    picture.fill = QColor(Qt::white);
    QVERIFY(canvas->insert(picture) != 0);

    // Independent histories behind one active-document facade.
    QVERIFY(manager.activate(notes->documentId()));
    const quint64 canvasRevision = canvas->revision();
    QVERIFY(manager.undoService()->undo());
    QCOMPARE(gain(notes, track), 1.0);
    QCOMPARE(canvas->revision(), canvasRevision);
    QVERIFY(manager.activate(board->documentId()));
    const QString notesRevision = notes->revision();
    QVERIFY(manager.undoService()->undo());
    QCOMPARE(canvas->objectCount(), 1);
    QCOMPARE(notes->revision(), notesRevision);
    QCOMPARE(gain(notes, track), 1.0);
    QVERIFY(canvas->redo());
    QVERIFY(manager.activate(notes->documentId()));
    QVERIFY(manager.undoService()->redo());
    QCOMPARE(gain(notes, track), 2.0);
    QCOMPARE(canvas->objectCount(), 2);

    const QString notesPath = temp.filePath(QStringLiteral("notes.qtk"));
    const QString boardPath = temp.filePath(QStringLiteral("board.qtkc"));
    QVERIFY(saved(notes, [&] { return notes->requestSaveAs(notesPath); }));
    QVERIFY(saved(board, [&] { return board->requestSaveAs(boardPath); }));
    QVERIFY(!notes->isDirty() && !board->isDirty());
    QVERIFY(setGain(manager, notes, track, 3.0));
    QVERIFY(canvas->setLayerVisible(canvas->activeLayer(), false));
    QVERIFY(notes->isDirty() && board->isDirty());
    QSignalSpy all(&manager, &DocumentManager::saveAllFinished);
    QVERIFY(manager.requestSaveAll());
    QVERIFY(all.wait(10000));
    const QVariantList outcomes = all.constFirst().constFirst().toList();
    QCOMPARE(outcomes.size(), 2);
    for (const QVariant &outcome : outcomes) QVERIFY(outcome.toMap().value(QStringLiteral("success")).toBool());
    QVERIFY(!notes->isDirty() && !board->isDirty());

    // Close and reopen: the canvas returns with the same identity and bytes.
    const QByteArray savedBoard = content(board);
    const QString boardId = board->documentId();
    QVERIFY(manager.closeDocument(boardId, DocumentManager::ClosePolicy::Cancel));
    DocumentSession *reopened = manager.openDocument(boardPath);
    QVERIFY2(reopened, qPrintable(manager.lastError()));
    QCOMPARE(reopened->documentId(), boardId);
    QCOMPARE(reopened->kind(), QStringLiteral("canvas"));
    QVERIFY(!reopened->isDirty());
    QCOMPARE(content(reopened), savedBoard);
    QVERIFY(!manager.openReferenceDocument(boardPath)); // the path holds another kind
    QCOMPARE(manager.findSession(boardId), reopened);

    // Workspace records carry schemas; restore opens both kinds generically.
    const QString secondView = manager.duplicateView(boardId);
    QVERIFY(!secondView.isEmpty());
    DockModel dock;
    WorkspaceSession workspace;
    workspace.setPath(temp.filePath(QStringLiteral("workspace.json")));
    QVERIFY(workspace.capture(&manager, &dock, {window()}, {}, QStringLiteral("main")));
    QVERIFY2(workspace.save(), qPrintable(workspace.lastError()));
    for (const QVariant &record : workspace.documents())
        QVERIFY(!record.toMap().value(QStringLiteral("schemaId")).toString().isEmpty());
    DocumentManager restartedManager;
    restartedManager.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("recovery")));
    DockModel restartedDock;
    WorkspaceSession restartedWorkspace;
    restartedWorkspace.setPath(workspace.path());
    QVERIFY(restartedWorkspace.load(screens));
    QVERIFY2(restartedWorkspace.restore(&restartedManager, &restartedDock),
             qPrintable(restartedWorkspace.lastError()));
    QVERIFY(restartedWorkspace.unavailableDocuments().isEmpty());
    DocumentSession *restoredBoard = restartedManager.findSession(boardId);
    DocumentSession *restoredNotes = restartedManager.findSession(notes->documentId());
    QVERIFY(restoredBoard && restoredNotes);
    QCOMPARE(restoredBoard->kind(), QStringLiteral("canvas"));
    QCOMPARE(content(restoredBoard), savedBoard);
    QCOMPARE(gain(restoredNotes, track), 3.0);
    QCOMPARE(restartedManager.viewsFor(boardId), manager.viewsFor(boardId));
    QVERIFY(!restoredBoard->isDirty() && !restoredNotes->isDirty());
}

void TstDomainSession::canvasRecoveryAfterExternalSaveConflict()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString recoveryDir = temp.filePath(QStringLiteral("recovery"));
    const QString path = temp.filePath(QStringLiteral("board.qtkc"));
    DocumentManager manager;
    manager.recoveryManager()->setDirectory(recoveryDir);
    DocumentSession *board = manager.createDocument(canvasSchema);
    board->setAutosaveDelayMs(600000); // frames come only from the explicit saves here
    CanvasDocument *canvas = canvasOf(board);
    QVERIFY(canvas->addRect(0, 0, 20, 20, Qt::blue) != 0);
    QVERIFY(saved(board, [&] { return board->requestSaveAs(path); }));
    QVERIFY(canvas->moveObjects({canvas->objectIds().first()}, 7, 0));
    QVERIFY(canvas->addEllipse(30, 30, 10, 10, Qt::green) != 0);
    // Another process replaces the file after this session last saved it.
    const QByteArray foreign("another writer's bytes");
    QFile external(path);
    QVERIFY(external.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(external.write(foreign), qint64(foreign.size()));
    external.close();
    QSignalSpy finished(board, &DocumentSession::saveFinished);
    QVERIFY(board->requestSave());
    QVERIFY(finished.wait(10000));
    QVERIFY(!finished.constFirst().at(0).toBool());
    QVERIFY(board->isDirty() && board->externalChanged());
    const QVariantMap offered = manager.recoveryManager()->inspectId(board->documentId());
    QCOMPARE(offered.value(QStringLiteral("schemaId")).toString(), canvasSchema);
    QCOMPARE(offered.value(QStringLiteral("revision")).toString(), board->revision());
    QVERIFY(offered.value(QStringLiteral("codecAvailable")).toBool());
    const QByteArray expected = content(board);

    // The process "restarts" with the same recovery directory.
    DocumentManager restarted;
    restarted.recoveryManager()->setDirectory(recoveryDir);
    DocumentSession *restored = restarted.restoreRecovered(board->documentId(), path);
    QVERIFY2(restored, qPrintable(restarted.lastError()));
    QVERIFY(restored->recovered() && restored->isDirty());
    QCOMPARE(restored->kind(), QStringLiteral("canvas"));
    QCOMPARE(content(restored), expected);
    // Foreign bytes are no baseline: a normal save refuses to replace them.
    QSignalSpy restoredSave(restored, &DocumentSession::saveFinished);
    QVERIFY(restored->requestSave());
    QVERIFY(restoredSave.wait(10000));
    QVERIFY(!restoredSave.constFirst().at(0).toBool());
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), foreign);
    QVERIFY(saved(restored, [&] { return restored->requestSaveAs(temp.filePath(QStringLiteral("kept.qtkc"))); }));
    QVERIFY(!restored->isDirty() && !restored->recovered());
    QVERIFY(restarted.recoveryManager()->inspectId(board->documentId()).isEmpty());
}

void TstDomainSession::recoveryFailuresKeepTheirFiles()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString recoveryDir = temp.filePath(QStringLiteral("recovery"));
    DocumentManager writer;
    writer.recoveryManager()->setDirectory(recoveryDir);
    DocumentSession *board = writer.createDocument(canvasSchema);
    board->setAutosaveDelayMs(600000);
    CanvasDocument *canvas = canvasOf(board);
    QVERIFY(canvas->addImage(smallImage()) != 0);
    QVERIFY(board->autosaveNow());
    const QString firstRevision = board->revision();
    QVERIFY(canvas->addRect(1, 1, 3, 3, Qt::black) != 0);
    QVERIFY(board->autosaveNow());
    const QString id = board->documentId();
    const QString journal = QDir(recoveryDir).filePath(id + QStringLiteral(".journal"));
    QFile file(journal);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QByteArray bytes = file.readAll();
    bytes[bytes.size() - 5] = char(bytes[bytes.size() - 5] ^ 0x44); // damage the newest frame
    QVERIFY(file.seek(0) && file.write(bytes) == bytes.size());
    file.close();
    const QVariantMap damaged = writer.recoveryManager()->inspectId(id);
    QVERIFY(damaged.value(QStringLiteral("truncatedTail")).toBool());
    QCOMPARE(damaged.value(QStringLiteral("revision")).toString(), firstRevision);

    const auto limited = std::make_shared<DocumentCodecRegistry>();
    QVERIFY(limited->registerCodec(referenceDocumentCodec()));
    const auto tight = std::make_shared<DocumentCodecRegistry>();
    QVERIFY(tight->registerCodec(makeCanvasDocumentCodec(CanvasDocumentLimits{.maximumImageBytes = 1024})));
    const auto fileSizes = [&] {
        QStringList sizes;
        for (const QFileInfo &info : QDir(recoveryDir).entryInfoList(QDir::Files, QDir::Name))
            sizes << info.fileName() + QLatin1Char('=') + QString::number(info.size());
        return sizes;
    };
    const QStringList before = fileSizes();
    for (const auto &[registry, reason] : {std::pair{limited, ErrorCode::Unavailable},
                                           std::pair{tight, ErrorCode::Unsupported}}) {
        DocumentManager reader;
        QVERIFY(reader.setCodecRegistry(registry));
        reader.recoveryManager()->setDirectory(recoveryDir);
        OperationError error;
        QVERIFY(!reader.recoveryManager()->restoreDocument(DocumentId::parse(id), &error));
        QVERIFY2(error.code == reason, qPrintable(error.message));
        QVERIFY(!reader.restoreRecovered(id));
        QCOMPARE(fileSizes(), before);
    }
    DocumentManager limitedReader;
    QVERIFY(limitedReader.setCodecRegistry(limited));
    limitedReader.recoveryManager()->setDirectory(recoveryDir);
    QVERIFY(!limitedReader.recoveryManager()->inspectId(id).value(QStringLiteral("codecAvailable")).toBool());
    DocumentManager capable;
    capable.recoveryManager()->setDirectory(recoveryDir);
    DocumentSession *restored = capable.restoreRecovered(id);
    QVERIFY2(restored, qPrintable(capable.lastError()));
    QCOMPARE(restored->revision(), firstRevision);

    // A version-1 reference frame from an older build stays readable, and
    // version-2 frames append after it.
    PropertyDocument legacy;
    QVERIFY(legacy.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(), 4.0, {}, true, false}));
    QVERIFY(legacy.addEntity(EntityId::create()));
    const SnapshotPtr snapshot = ReferenceDocumentCodec::capture(legacy);
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << quint32(0x51544b52) << quint32(1) << quint64(snapshot->payload.size())
        << quint64(snapshot->header.revision.value);
    frame += QCryptographicHash::hash(snapshot->payload, QCryptographicHash::Sha256) + snapshot->payload;
    QFile old(QDir(recoveryDir).filePath(legacy.documentIdString() + QStringLiteral(".journal")));
    QVERIFY(old.open(QIODevice::WriteOnly) && old.write(frame) == frame.size());
    old.close();
    QCOMPARE(capable.recoveryManager()->inspectId(legacy.documentIdString())
                 .value(QStringLiteral("schemaId")).toString(), QString::fromLatin1(PropertyDocument::SchemaId));
    QVERIFY(!capable.recoveryManager()->record(ReferenceDocumentCodec::capture(legacy)));
    QCOMPARE(capable.recoveryManager()->inspectId(legacy.documentIdString())
                 .value(QStringLiteral("validEntries")).toInt(), 2);
    DocumentSession *legacySession = capable.restoreRecovered(legacy.documentIdString());
    QVERIFY(legacySession && legacySession->document());
}

void TstDomainSession::staleReceiptsMarkOnlyTheCapturedPosition()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    auto store = std::make_shared<HeldStore>();
    DocumentManager manager;
    manager.setStore(store);
    manager.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("recovery")));
    DocumentSession *board = manager.createDocument(canvasSchema);
    board->setAutosaveDelayMs(600000);
    CanvasDocument *canvas = canvasOf(board);
    QVERIFY(canvas->addRect(0, 0, 10, 10, Qt::red) != 0);
    QVERIFY(saved(board, [&] { return board->requestSaveAs(temp.filePath(QStringLiteral("b.qtkc"))); }));
    QVERIFY(canvas->addRect(20, 0, 10, 10, Qt::red) != 0); // captured position
    QSignalSpy finished(board, &DocumentSession::saveFinished);
    store->holdNextSave();
    QVERIFY(board->requestSave());
    QVERIFY(store->waitUntilHeld());
    QVERIFY(canvas->addRect(40, 0, 10, 10, Qt::red) != 0); // newer than the capture
    store->release();
    QVERIFY(finished.wait(10000));
    QVERIFY(finished.constLast().at(0).toBool());
    QVERIFY(board->isDirty());
    QVERIFY(!manager.recoveryManager()->inspectId(board->documentId()).isEmpty());
    QVERIFY(canvas->undo());
    QVERIFY(!board->isDirty()); // exactly the bytes the late receipt wrote
    QVERIFY(canvas->undo());
    QVERIFY(board->isDirty());
    QVERIFY(canvas->redo());
    QVERIFY(!board->isDirty());

    // A reset during the save: the receipt's generation is stale.
    QVERIFY(canvas->addEllipse(0, 20, 5, 5, Qt::blue) != 0);
    store->holdNextSave();
    QVERIFY(board->requestSave());
    QVERIFY(store->waitUntilHeld());
    canvas->clear();
    store->release();
    QVERIFY(finished.wait(10000));
    QVERIFY(finished.constLast().at(0).toBool());
    QVERIFY(board->isDirty());
    QVERIFY(!canvas->canUndo());
    QVERIFY(!manager.recoveryManager()->inspectId(board->documentId()).isEmpty());
}

void TstDomainSession::formatRegistryUsesTheSchemaBoundary()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    DocumentManager manager;
    manager.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("recovery")));
    DocumentSession *board = manager.createDocument(canvasSchema);
    CanvasDocument *canvas = canvasOf(board);
    QVERIFY(canvas->addRect(3, 4, 5, 6, Qt::darkCyan) != 0);
    QVERIFY(canvas->addImage(smallImage()) != 0);
    FormatRegistry formats;
    const QUrl reference = QUrl::fromLocalFile(temp.filePath(QStringLiteral("board.qtk")));
    const QVariantMap refused = formats.preflightExportSession(board, reference, QStringLiteral("qindatk.reference.v2"));
    QVERIFY(!refused.value(QStringLiteral("ok")).toBool());
    QCOMPARE(refused.value(QStringLiteral("error")).toMap().value(QStringLiteral("code")).toInt(),
             int(ErrorCode::Unsupported));
    QVERIFY(formats.startExportSession(board, reference, QStringLiteral("text.utf8")).isEmpty());
    QVERIFY(formats.registerProvider(std::make_shared<CanvasNativeFormat>()));
    const QUrl destination = QUrl::fromLocalFile(temp.filePath(QStringLiteral("board.qtkc")));
    const QVariantMap plan = formats.preflightExportSession(board, destination, QStringLiteral("qindatk.canvas.v1"));
    QVERIFY2(plan.value(QStringLiteral("ok")).toBool(),
             qPrintable(plan.value(QStringLiteral("error")).toMap().value(QStringLiteral("message")).toString()));
    QSignalSpy finished(formats.jobs(), &JobModel::jobFinished);
    const QString exportId = formats.startExportSession(board, destination, QStringLiteral("qindatk.canvas.v1"),
                                                        plan.value(QStringLiteral("options")).toMap());
    QVERIFY(!exportId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QCOMPARE(formats.jobs()->job(exportId).value(QStringLiteral("state")).toString(), QStringLiteral("succeeded"));
    QVERIFY(board->isDirty()); // an export is not a save of the session
    QCOMPARE(formats.detectImport(destination), QStringLiteral("qindatk.canvas.v1"));
    const QString importId = formats.startImport(destination);
    QVERIFY(!importId.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);
    DocumentManager other;
    other.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("other")));
    DocumentSession *imported = formats.adoptImport(&other, importId);
    QVERIFY2(imported, qPrintable(formats.lastError()));
    QCOMPARE(imported->kind(), QStringLiteral("canvas"));
    QCOMPARE(content(imported), content(board));
}

QTEST_MAIN(TstDomainSession)
#include "tst_domain_session.moc"
