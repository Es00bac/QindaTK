// SPDX-License-Identifier: LGPL-3.0-or-later
#include "window_host.h"
#include "document_manager.h"
#include "file_dialog_adapter.h"
#include "desktop_integration.h"
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class FakeDialogBackend final : public FileDialogBackend {
public:
    bool available() const override { return true; }
    bool supportsRemoteUrls() const override { return false; }
    void start(const QString &requestId, const FileDialogRequest &request, QWindow *,
               std::function<void(FileDialogResult)> completion) override
    {
        ids << requestId;
        modes << request.mode;
        callbacks.insert(requestId, std::move(completion));
    }
    void cancel(const QString &requestId) override { canceled << requestId; }
    QStringList ids, canceled;
    QList<FileDialogRequest::Mode> modes;
    QHash<QString, std::function<void(FileDialogResult)>> callbacks;
};

class TstWindowHost final : public QObject {
    Q_OBJECT
private slots:
    void twoNativeWindowsShareOneDocumentAndGuardLastClose()
    {
        DocumentManager documents;
        auto *session = documents.createReferenceDocument();
        QVERIFY(session);
        const QString documentId = session->documentId();
        const QStringList views = documents.viewsFor(documentId);
        QCOMPARE(views.size(), 1);
        const QString secondView = documents.duplicateView(documentId);
        QVERIFY(!secondView.isEmpty());
        WindowHost host;
        host.setDocumentManager(&documents);
        const QString first = host.createWindow(documentId, views.first());
        const QString second = host.createWindow(documentId, secondView);
        QVERIFY(!first.isEmpty() && !second.isEmpty() && first != second);
        QVERIFY(host.window(first) && host.window(second));
        QCOMPARE(host.windows().size(), 2);
        QCOMPARE(host.windows().first().toMap().value(QStringLiteral("documentId")).toString(), documentId);
        QVERIFY(host.activate(second));
        QSignalSpy blocked(&host, &WindowHost::closeBlocked);
        QVERIFY(host.closeWindow(first));
        QCOMPARE(host.windows().size(), 1);
        QCOMPARE(documents.findSession(documentId), session);
        QCOMPARE(documents.viewsFor(documentId), QStringList{secondView});
        QVERIFY(!host.closeWindow(second));
        QCOMPARE(blocked.size(), 1);
        QCOMPARE(host.windows().size(), 1);
        QVERIFY(host.closeWindow(second, true));
        QCOMPARE(host.windows().size(), 0);
        QVERIFY(!documents.findSession(documentId));
    }
    void autoAssignedViewHasDistinctIdentity()
    {
        DocumentManager documents;
        auto *session = documents.createReferenceDocument();
        WindowHost host;
        host.setDocumentManager(&documents);
        const QString first = host.createWindow(session->documentId());
        const QString second = host.createWindow(session->documentId());
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        const auto windows = host.windows();
        QVERIFY(windows.at(0).toMap().value(QStringLiteral("viewId"))
                != windows.at(1).toMap().value(QStringLiteral("viewId")));
        QCOMPARE(documents.viewsFor(session->documentId()).size(), 2);
        QVERIFY(host.closeWindow(first));
        QCOMPARE(documents.viewsFor(session->documentId()).size(), 1);
        QVERIFY(host.closeWindow(second, true));
    }
    void externalWindowIsNotOwnedAndNativeCloseIsGuarded()
    {
        DocumentManager documents;
        auto *session = documents.createReferenceDocument();
        QQuickWindow external;
        WindowHost host;
        host.setDocumentManager(&documents);
        const QString id = host.registerWindow(&external, session->documentId(),
                                               documents.viewsFor(session->documentId()).first());
        QVERIFY(!id.isEmpty());
        external.show();
        QVERIFY(!external.close());
        QVERIFY(host.window(id) == &external);
        QVERIFY(host.closeWindow(id, true));
        QVERIFY(!host.window(id));
    }
    void destroyedExternalWindowReleasesItsView()
    {
        DocumentManager documents;
        auto *session = documents.createReferenceDocument();
        const QString documentId = session->documentId();
        auto *external = new QQuickWindow;
        WindowHost host;
        host.setDocumentManager(&documents);
        const QString first = host.registerWindow(external, documentId);
        const QString second = host.createWindow(documentId);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QCOMPARE(documents.viewsFor(documentId).size(), 2);
        delete external;
        QCOMPARE(host.windows().size(), 1);
        QCOMPARE(documents.viewsFor(documentId).size(), 1);
        QVERIFY(host.closeWindow(second, true));
    }
    void screenGeometryAndUnavailableScreen()
    {
        WindowHost host;
        const QString id = host.createWindow();
        QVERIFY(!id.isEmpty());
        auto *screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        QVERIFY(host.moveWindow(id, QRect(-10000, -10000, 500, 400), screen->name()));
        const QRect available = screen->availableGeometry();
        QVERIFY(available.contains(host.window(id)->geometry().topLeft()));
        const QRect before = host.window(id)->geometry();
        QVERIFY(!host.moveWindow(id, QRect(10, 10, 200, 200), QStringLiteral("removed-monitor")));
        QCOMPARE(host.window(id)->geometry(), before);
        host.reconcileScreens();
        QVERIFY(available.intersects(host.window(id)->geometry()));
        QCOMPARE(host.capabilities().value(QStringLiteral("logicalGeometry")).toBool(), true);
        QVERIFY(host.closeWindow(id));
    }
    void dialogCancelRevokedLocationAndUnsupportedRemote()
    {
        FileDialogAdapter adapter;
        QSignalSpy finished(&adapter, &FileDialogAdapter::finished);
        const QString unavailable = adapter.request(QStringLiteral("open"));
        QVERIFY(!unavailable.isEmpty());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst().at(1).toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("unsupported"));
        auto backend = std::make_shared<FakeDialogBackend>();
        adapter.setBackend(backend);
        const QString remote = adapter.request(QStringLiteral("open"), {}, QUrl(QStringLiteral("https://example.invalid/a")));
        QVERIFY(!remote.isEmpty());
        QCOMPARE(finished.takeFirst().at(1).toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("unsupported"));
        const QString canceled = adapter.request(QStringLiteral("open"));
        QCOMPARE(adapter.pendingCount(), 1);
        QVERIFY(adapter.cancel(canceled));
        QCOMPARE(finished.takeFirst().at(1).toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("canceled"));
        QCOMPARE(backend->canceled, QStringList{canceled});
        backend->callbacks.value(canceled)({FileDialogResult::Status::Accepted,
                                             {QUrl::fromLocalFile(QStringLiteral("/gone"))}, {}});
        QCOMPARE(finished.size(), 0);
        const QString revoked = adapter.request(QStringLiteral("open"));
        backend->callbacks.value(revoked)({FileDialogResult::Status::Accepted,
                                           {QUrl::fromLocalFile(QStringLiteral("/path/that/is/missing"))}, {}});
        QCOMPARE(finished.takeFirst().at(1).toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("error"));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString save = adapter.request(QStringLiteral("save"));
        const QUrl target = QUrl::fromLocalFile(temp.filePath(QStringLiteral("new.qtk")));
        backend->callbacks.value(save)({FileDialogResult::Status::Accepted, {target}, {}});
        QCOMPARE(finished.takeFirst().at(1).toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("accepted"));
    }
    void desktopCapabilitiesAndExternalFileOpen()
    {
        DesktopIntegration desktop;
        const auto capabilities = desktop.capabilities();
        QCOMPARE(capabilities.value(QStringLiteral("osRecentRegistration")).toBool(), false);
        QCOMPARE(capabilities.value(QStringLiteral("secondInstanceIPC")).toBool(), false);
        QVERIFY(!desktop.registerRecent(QUrl::fromLocalFile(QStringLiteral("/missing"))));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(desktop.registerRecent(QUrl::fromLocalFile(temp.path())));
        QCOMPARE(desktop.recentItems().size(), 1);
        QVERIFY(!desktop.reveal(QUrl(QStringLiteral("https://example.invalid/a"))));
        QSignalSpy open(&desktop, &DesktopIntegration::openDocumentRequested);
        const QUrl url = QUrl::fromLocalFile(temp.filePath(QStringLiteral("external.qtk")));
        QFileOpenEvent event(url);
        QCoreApplication::sendEvent(qApp, &event);
        QCOMPARE(open.size(), 1);
        QCOMPARE(open.first().first().toUrl(), url);
    }
};
QTEST_MAIN(TstWindowHost)
#include "tst_window_host.moc"
