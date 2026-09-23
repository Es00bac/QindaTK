// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_manager.h"
#include "edit_transaction.h"
#include "reference_document_codec.h"
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QWaitCondition>
#include <QtTest>

using namespace QindaTK;

class ControlledDocumentStore final : public DocumentStore {
public:
    SaveReceipt save(const SaveRequest &request) override
    {
        {
            QMutexLocker lock(&mutex);
            if (pauseNext) {
                pauseNext = false;
                entered = true;
                changed.wakeAll();
                while (!released) changed.wait(&mutex);
                entered = false;
                released = false;
            }
            SaveReceipt result;
            result.path = request.path;
            if (request.snapshot) result.saved = request.snapshot->header;
            if (request.cancellation.isCancelled()) {
                result.error = {ErrorCode::Cancelled, QStringLiteral("cancelled"), {}, {}};
                return result;
            }
            if (failPaths.contains(request.path)) {
                result.error = {ErrorCode::BackendFailure, QStringLiteral("injected write failure"),
                                request.path, QStringLiteral("test")};
                return result;
            }
            const auto existing = files.constFind(request.path);
            if ((request.expectation == FileExpectation::Missing && existing != files.cend())
                || (request.expectation == FileExpectation::MatchingHash
                    && (existing == files.cend()
                        || AtomicDocumentStore::sha256(existing.value()) != request.expectedSha256))) {
                result.error = {ErrorCode::Conflict, QStringLiteral("injected conflict"),
                                request.path, QStringLiteral("test")};
                return result;
            }
            if (!request.snapshot) {
                result.error = {ErrorCode::InvalidArgument, QStringLiteral("missing snapshot"), {}, {}};
                return result;
            }
            files.insert(request.path, request.snapshot->payload);
            result.sha256 = AtomicDocumentStore::sha256(request.snapshot->payload);
            return result;
        }
    }

    LoadReceipt load(const QString &path) override
    {
        QMutexLocker lock(&mutex);
        const auto it = files.constFind(path);
        if (it == files.cend())
            return {{}, {}, {ErrorCode::Unavailable, QStringLiteral("not found"), path, {}}};
        return {it.value(), AtomicDocumentStore::sha256(it.value()), {}};
    }

    void pauseOneSave()
    {
        QMutexLocker lock(&mutex);
        pauseNext = true;
        entered = false;
        released = false;
    }

    bool waitUntilPaused(int timeoutMs = 5000)
    {
        QMutexLocker lock(&mutex);
        return entered || (changed.wait(&mutex, timeoutMs) && entered);
    }

    void releaseSave()
    {
        QMutexLocker lock(&mutex);
        released = true;
        changed.wakeAll();
    }

    void failPath(const QString &path)
    {
        QMutexLocker lock(&mutex);
        failPaths.insert(path);
    }

    QByteArray bytes(const QString &path) const
    {
        QMutexLocker lock(&mutex);
        return files.value(path);
    }

private:
    mutable QMutex mutex;
    QWaitCondition changed;
    QHash<QString, QByteArray> files;
    QSet<QString> failPaths;
    bool pauseNext = false;
    bool entered = false;
    bool released = false;
};

class TstDocumentSession final : public QObject {
    Q_OBJECT
private:
    static EntityId addGain(PropertyDocument *document, double initial)
    {
        if (!document->defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                       0.0, QStringLiteral("dB"), true, true}))
            return {};
        const auto id = EntityId::create();
        if (!document->addEntity(id, {{QStringLiteral("gain"), initial}})) return {};
        return id;
    }

    static bool edit(DocumentManager &manager, DocumentSession *session,
                     EntityId target, double value)
    {
        EditTransaction transaction;
        transaction.setDocument(session->document());
        transaction.setUndoService(manager.undoService());
        if (!transaction.begin({target}, QStringLiteral("gain"), QStringLiteral("Set gain")))
            return false;
        return !transaction.updateValue(value) && !transaction.commitEdit();
    }

    static DocumentSession *createGainDocument(DocumentManager &manager, EntityId *target,
                                               double initial)
    {
        auto document = std::make_unique<PropertyDocument>();
        *target = addGain(document.get(), initial);
        if (!target->isValid()) return nullptr;
        return manager.create(std::move(document));
    }

private slots:
    void asynchronousSaveKeepsNewerEditsDirtyAcrossTwoDocuments()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<ControlledDocumentStore>();
        DocumentManager manager;
        manager.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("recovery")));
        manager.setStore(store);
        EntityId firstTarget, secondTarget;
        auto *first = createGainDocument(manager, &firstTarget, 1.0);
        auto *second = createGainDocument(manager, &secondTarget, 2.0);
        QVERIFY(first && second);
        QVERIFY(first->documentId() != second->documentId());
        QCOMPARE(first->document()->contains(firstTarget), true);
        QCOMPARE(second->document()->contains(secondTarget), true);

        QSignalSpy saved(first, &DocumentSession::saveFinished);
        const QString firstPath = temp.filePath(QStringLiteral("first.qtk"));
        QVERIFY(first->requestSaveAs(firstPath));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
        QVERIFY(saved.constFirst().at(0).toBool());
        QVERIFY(!first->isDirty());

        QVERIFY(edit(manager, first, firstTarget, 3.0));
        QVERIFY(first->isDirty());
        store->pauseOneSave();
        QVERIFY(first->requestSave());
        QVERIFY(store->waitUntilPaused());
        QVERIFY(edit(manager, first, firstTarget, 4.0));
        store->releaseSave();
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 2, 5000);
        QVERIFY(saved.constLast().at(0).toBool());
        QVERIFY(first->isDirty());
        QCOMPARE(first->document()->value(firstTarget, QStringLiteral("gain")).toDouble(), 4.0);
        OperationError error;
        const auto written = ReferenceDocumentCodec::decode(store->bytes(firstPath), &error);
        QVERIFY2(written.has_value(), qPrintable(error.message));
        QCOMPARE(written->document->value(firstTarget, QStringLiteral("gain")).toDouble(), 3.0);

        QVERIFY(edit(manager, second, secondTarget, 9.0));
        QVERIFY(second->isDirty());
        QVERIFY(manager.activate(first->documentId()));
        QCOMPARE(manager.activeSession(), first);
        QVERIFY(manager.activate(second->documentId()));
        QCOMPARE(manager.activeSession(), second);
    }

    void failedSavePreservesPriorBytesAndSaveAllReportsPartialResults()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto store = std::make_shared<ControlledDocumentStore>();
        DocumentManager manager;
        manager.recoveryManager()->setDirectory(temp.filePath(QStringLiteral("recovery")));
        manager.setStore(store);
        EntityId goodTarget, failingTarget;
        auto *good = createGainDocument(manager, &goodTarget, 1.0);
        auto *failing = createGainDocument(manager, &failingTarget, 2.0);
        QVERIFY(good && failing);
        const QString goodPath = temp.filePath(QStringLiteral("good.qtk"));
        const QString failingPath = temp.filePath(QStringLiteral("failing.qtk"));
        QSignalSpy goodSaved(good, &DocumentSession::saveFinished);
        QSignalSpy failingSaved(failing, &DocumentSession::saveFinished);
        QVERIFY(good->requestSaveAs(goodPath));
        QVERIFY(failing->requestSaveAs(failingPath));
        QTRY_COMPARE_WITH_TIMEOUT(goodSaved.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(failingSaved.size(), 1, 5000);
        const QByteArray previous = store->bytes(failingPath);
        QVERIFY(!previous.isEmpty());

        QVERIFY(edit(manager, good, goodTarget, 3.0));
        QVERIFY(edit(manager, failing, failingTarget, 4.0));
        store->failPath(failingPath);
        QSignalSpy allFinished(&manager, &DocumentManager::saveAllFinished);
        QVERIFY(manager.requestSaveAll());
        QTRY_COMPARE_WITH_TIMEOUT(allFinished.size(), 1, 5000);
        const auto outcomes = allFinished.constFirst().constFirst().toList();
        QCOMPARE(outcomes.size(), 2);
        int successes = 0;
        int failures = 0;
        for (const auto &entry : outcomes) {
            const auto outcome = entry.toMap();
            if (outcome.value(QStringLiteral("success")).toBool()) ++successes;
            else ++failures;
        }
        QCOMPARE(successes, 1);
        QCOMPARE(failures, 1);
        QCOMPARE(store->bytes(failingPath), previous);
        QVERIFY(!good->isDirty());
        QVERIFY(failing->isDirty());
    }

    void closeCancelRetainsDirtySessionAndViews()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        DocumentManager manager;
        manager.recoveryManager()->setDirectory(temp.path());
        EntityId target;
        auto *session = createGainDocument(manager, &target, 0.0);
        QVERIFY(session);
        QVERIFY(edit(manager, session, target, 5.0));
        const QString id = session->documentId();
        const QString view = manager.duplicateView(id);
        QVERIFY(!view.isEmpty());
        QSignalSpy blocked(&manager, &DocumentManager::closeBlocked);
        QVERIFY(!manager.closeDocument(id, DocumentManager::ClosePolicy::Cancel));
        QCOMPARE(blocked.size(), 1);
        QCOMPARE(manager.findSession(id), session);
        QVERIFY(manager.viewsFor(id).contains(view));
        QVERIFY(session->isDirty());
    }

    void recoveryUsesLastValidRevisionAfterTruncatedTailAndReopens()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        RecoveryManager recovery;
        recovery.setDirectory(temp.path());
        PropertyDocument document;
        const auto target = addGain(&document, 1.0);
        QVERIFY(target.isValid());
        OperationError error;
        auto first = ReferenceDocumentCodec::capture(document, {}, &error);
        QVERIFY2(first, qPrintable(error.message));
        QVERIFY(!recovery.record(first));

        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        EditTransaction edit;
        edit.setDocument(&document);
        edit.setUndoService(&undo);
        QVERIFY(edit.begin({target}, QStringLiteral("gain"), QStringLiteral("Change gain")));
        QVERIFY(edit.update(7.0));
        QVERIFY(edit.commit());
        auto latest = ReferenceDocumentCodec::capture(document, {}, &error);
        QVERIFY2(latest, qPrintable(error.message));
        QVERIFY(!recovery.record(latest));

        const QString journal = temp.filePath(document.documentIdString() + QStringLiteral(".journal"));
        QFile tail(journal);
        QVERIFY(tail.open(QIODevice::WriteOnly | QIODevice::Append));
        QCOMPARE(tail.write(QByteArray("broken-tail", 11)), qint64(11));
        tail.close();
        const auto recoveredInfo = recovery.inspect(document.documentId());
        QVERIFY(recoveredInfo.has_value());
        QVERIFY(recoveredInfo->truncatedTail);
        QCOMPARE(recoveredInfo->snapshot->header.revision, document.revision());

        DocumentManager manager;
        manager.recoveryManager()->setDirectory(temp.path());
        const auto recovered = manager.restoreRecovered(document.documentIdString());
        QVERIFY(recovered);
        QVERIFY(recovered->recovered());
        QVERIFY(recovered->isDirty());
        QCOMPARE(recovered->revision(), document.revisionString());
        QCOMPARE(recovered->document()->value(target, QStringLiteral("gain")).toDouble(), 7.0);
        QVERIFY(recovery.inspect(document.documentId())->truncatedTail);
    }
};

QTEST_GUILESS_MAIN(TstDocumentSession)
#include "tst_document_session.moc"
