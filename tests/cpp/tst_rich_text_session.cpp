// SPDX-License-Identifier: LGPL-3.0-or-later
// Rich text persisted through the host DocumentSession (save, reopen, recovery).
#include "document_manager.h"
#include "rich_text_codec.h"
#include "rich_text_fixtures.h"
#include "rich_text_layout.h"
#include "rich_text_session_adapter.h"
#include <QElapsedTimer>
#include <QMutex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWaitCondition>
#include <QtTest>
#include <algorithm>

using namespace QindaTK;
using namespace QindaTK::RichText;
using namespace QindaTK::RichTextFixtures;

namespace {

// Holds the next save on the worker until release(), like tst_document_session.
class HeldStore final : public DocumentStore {
public:
    SaveReceipt save(const SaveRequest &request) override
    {
        QMutexLocker lock(&mutex);
        if (holdNext) {
            holdNext = false;
            entered = true;
            changed.wakeAll();
            while (!released) changed.wait(&mutex);
        }
        SaveReceipt receipt;
        receipt.path = request.path;
        receipt.saved = request.snapshot->header;
        files.insert(request.path, request.snapshot->payload);
        receipt.sha256 = AtomicDocumentStore::sha256(request.snapshot->payload);
        return receipt;
    }
    LoadReceipt load(const QString &path) override
    {
        QMutexLocker lock(&mutex);
        return {files.value(path), AtomicDocumentStore::sha256(files.value(path)), {}};
    }
    void release()
    {
        QMutexLocker lock(&mutex);
        released = true;
        changed.wakeAll();
    }
    QMutex mutex;
    QWaitCondition changed;
    QHash<QString, QByteArray> files;
    bool holdNext = false;
    bool entered = false;
    bool released = false;
};

QVector<QPair<QUuid, Position>> pageSummary(RichTextDocument &document)
{
    RichTextLayout layout;
    layout.setDocument(&document);
    layout.paginateAll();
    QVector<QPair<QUuid, Position>> pages;
    for (int i = 0; i < layout.pageCount(); ++i) pages.append({layout.page(i).key, layout.page(i).first});
    return pages;
}

} // namespace

class RichTextSessionTest : public QObject {
    Q_OBJECT
private slots:
    void saveAndReopenAreEquivalent()
    {
        QTemporaryDir temp;
        DocumentManager manager;
        DocumentSession *session = manager.createReferenceDocument();
        RichTextDocument document;
        buildHardFixture(document);
        RichTextSessionAdapter adapter;
        adapter.setDocument(&document);
        adapter.setSession(session);
        QVERIFY(adapter.loaded());
        // Edits after attaching: a split on page 0, typing, and formatting.
        const auto ps = document.paragraphsInOrder();
        QVERIFY(document.splitParagraph({ps.at(14)->id, 3}));
        QVERIFY(document.insertText({ps.at(2)->id, 0}, QStringLiteral("א ")));
        CharFormatPatch italic;
        italic.italic = 1;
        QVERIFY(document.applyCharFormat({ps.at(3)->id, 0}, {ps.at(5)->id, 2}, italic));
        QVERIFY(session->isDirty());
        const QString path = temp.filePath(QStringLiteral("report.qtk"));
        QSignalSpy saved(session, &DocumentSession::saveFinished);
        QVERIFY(session->requestSaveAs(path));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QVERIFY(saved.first().at(0).toBool());
        QVERIFY(!session->isDirty());

        DocumentManager other;
        DocumentSession *reopened = other.openReferenceDocument(path);
        QVERIFY(reopened);
        RichTextDocument copy;
        RichTextSessionAdapter reader;
        reader.setDocument(&copy);
        reader.setSession(reopened);
        QVERIFY2(reader.loaded(), qPrintable(reader.lastError()));
        QVERIFY(!reopened->isDirty());
        QCOMPARE(RichTextCodec::encode(copy), RichTextCodec::encode(document));
        QCOMPARE(pageSummary(copy), pageSummary(document));
        QVERIFY(copy.insertText(copy.startPosition(), QStringLiteral("x")));
        QVERIFY(reopened->isDirty());
    }

    void recoveryRestoresTheRichText()
    {
        QTemporaryDir temp;
        const QString recoveryDirectory = temp.filePath(QStringLiteral("recovery"));
        DocumentManager manager;
        manager.recoveryManager()->setDirectory(recoveryDirectory);
        DocumentSession *session = manager.createReferenceDocument();
        RichTextDocument document;
        buildLines(document, 30);
        RichTextSessionAdapter adapter;
        adapter.setDocument(&document);
        adapter.setSession(session);
        QVERIFY(document.insertText(document.endPosition(), QStringLiteral(" unsaved tail")));
        QVERIFY(session->autosaveNow());
        DocumentManager restoredManager;
        restoredManager.recoveryManager()->setDirectory(recoveryDirectory);
        DocumentSession *restored = restoredManager.restoreRecovered(session->documentId());
        QVERIFY(restored);
        RichTextDocument copy;
        RichTextSessionAdapter reader;
        reader.setDocument(&copy);
        reader.setSession(restored);
        QVERIFY(reader.loaded());
        QCOMPARE(RichTextCodec::encode(copy), RichTextCodec::encode(document));
    }

    void anEditDuringASaveKeepsTheSessionDirty()
    {
        auto store = std::make_shared<HeldStore>();
        DocumentManager manager;
        manager.setStore(store);
        DocumentSession *session = manager.createReferenceDocument();
        RichTextDocument document;
        document.insertText(document.startPosition(), QStringLiteral("before save"));
        RichTextSessionAdapter adapter;
        adapter.setDocument(&document);
        adapter.setSession(session);
        store->holdNext = true;
        QSignalSpy saved(session, &DocumentSession::saveFinished);
        QVERIFY(session->requestSaveAs(QStringLiteral("/virtual/held.qtk")));
        QTRY_VERIFY_WITH_TIMEOUT([&] { QMutexLocker lock(&store->mutex); return store->entered; }(), 5000);
        QVERIFY(document.insertText(document.endPosition(), QStringLiteral(" + late edit")));
        store->release();
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QVERIFY(saved.first().at(0).toBool());
        QVERIFY(session->isDirty()); // the saved snapshot predates the late edit
        const auto decoded = ReferenceDocumentCodec::decode(store->files.value(QStringLiteral("/virtual/held.qtk")));
        QVERIFY(decoded);
        RichTextDocument savedCopy;
        QVERIFY(RichTextCodec::fromJsonObject(decoded->extensions.value(RichTextSessionAdapter::defaultExtensionKey()).toObject(), savedCopy));
        QCOMPARE(savedCopy.text(), QStringLiteral("before save"));
    }

    void readOnlySessionMakesTheDocumentReadOnly()
    {
        DocumentManager manager;
        DocumentSession *session = manager.createReferenceDocument();
        RichTextDocument document;
        RichTextSessionAdapter adapter;
        adapter.setDocument(&document);
        adapter.setSession(session);
        session->setReadOnly(true);
        QVERIFY(document.readOnly());
        QVERIFY(!document.insertText(document.startPosition(), QStringLiteral("x")));
        session->setReadOnly(false);
        QVERIFY(!document.readOnly());
    }

    void flushCostStaysFlatOnFiveHundredPages()
    {
        DocumentManager manager;
        DocumentSession *session = manager.createReferenceDocument();
        RichTextDocument document;
        buildLines(document, 500 * LinesPerPage);
        RichTextSessionAdapter adapter;
        adapter.setDocument(&document);
        QElapsedTimer timer;
        timer.start();
        adapter.setSession(session);
        const qint64 first = timer.nsecsElapsed();
        const Paragraph *target = document.paragraphsInOrder().at(250 * LinesPerPage);
        QVector<qint64> keystrokes;
        for (int i = 0; i < 20; ++i) {
            timer.restart();
            QVERIFY(document.insertText({target->id, 0}, QStringLiteral("k")));
            keystrokes.append(timer.nsecsElapsed());
        }
        std::sort(keystrokes.begin(), keystrokes.end());
        qInfo("MEASURE session flush, 500 pages: first full encode %.1f ms; keystroke+flush median %.2f ms, max %.2f ms",
              first / 1e6, keystrokes.at(10) / 1e6, keystrokes.last() / 1e6);
        QVERIFY(keystrokes.at(10) < first);
        RichTextDocument copy;
        QVERIFY(RichTextCodec::fromJsonObject(session->extensions().value(adapter.extensionKey()).toObject(), copy));
        QCOMPARE(RichTextCodec::encode(copy), RichTextCodec::encode(document));
    }
};

QTEST_MAIN(RichTextSessionTest)
#include "tst_rich_text_session.moc"
