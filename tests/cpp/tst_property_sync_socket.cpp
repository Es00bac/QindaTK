// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_sync_socket.h"
#include "reference_document_codec.h"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace QindaTK;

namespace {
constexpr auto ActorEditor = "00000000-0000-0000-0000-00000000000a";
constexpr auto ActorViewer = "00000000-0000-0000-0000-00000000000b";
constexpr auto EditorCredential = "editor-credential-123456";
constexpr auto ViewerCredential = "viewer-credential-123456";

int worker(const QStringList &arguments)
{
    // --worker mode baseline local-store actor entity socket credential
    if (arguments.size() != 9) return 10;
    QFile file(arguments.at(3));
    if (!file.open(QIODevice::ReadOnly)) return 11;
    auto decoded = ReferenceDocumentCodec::decode(file.readAll());
    if (!decoded) return 12;
    auto document = std::move(decoded->document);
    LocalPropertySyncService local;
    local.setDocumentId(document->documentIdString());
    local.setStoragePath(arguments.at(4));
    if (!local.open()) return 13;
    LocalPropertySyncClient editor;
    editor.setDocument(document.get());
    editor.setService(&local);
    editor.setActorId(arguments.at(5));
    if (!editor.connectToService()) return 14;
    if (arguments.at(2) == QStringLiteral("offline"))
        return editor.setValue(arguments.at(6), QStringLiteral("gain"),
                               arguments.at(5) == QString::fromLatin1(ActorViewer) ? 9.0 : 7.0)
            ? 0 : 15;
    PropertySyncSocketClient socket;
    socket.setService(&local);
    socket.setActorId(arguments.at(5));
    socket.setSocketName(arguments.at(7));
    socket.setCredential(arguments.at(8));
    socket.setLocalClient(&editor);
    int result = 16;
    QEventLoop loop;
    QObject::connect(&socket, &PropertySyncSocketClient::connected, &loop, [&] {
        if (arguments.at(2) == QStringLiteral("presence"))
            editor.setPresence({{QStringLiteral("label"), QStringLiteral("Remote editor")},
                                {QStringLiteral("selection"), arguments.at(6)}});
        QTimer::singleShot(arguments.at(2) == QStringLiteral("presence") ? 2500 : 800,
                           &loop, [&] { result = 0; loop.quit(); });
    });
    QObject::connect(&socket, &PropertySyncSocketClient::operationRejected, &loop,
                     [&](const QString &, const QString &) { result = 17; loop.quit(); });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    if (!socket.connectToServer()) return 18;
    loop.exec();
    return result;
}

void startWorker(QProcess &process, const QString &mode, const QString &baseline,
                 const QString &store, const QString &actor, const QString &entity,
                 const QString &socket, const QString &credential)
{
    process.start(QCoreApplication::applicationFilePath(),
        {QStringLiteral("--worker"), mode, baseline, store, actor, entity,
         socket, credential});
}
}

class TstPropertySyncSocket final : public QObject {
    Q_OBJECT
private slots:
    void presenceReplaysAndLeavesAcrossProcesses()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PropertyDocument base;
        PropertyDescriptor gain;
        gain.id = QStringLiteral("gain");
        gain.type = QMetaType::fromType<double>();
        gain.defaultValue = 0.0;
        QVERIFY(base.defineProperty(gain));
        const EntityId entity = EntityId::create();
        QVERIFY(base.addEntity(entity));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        const QString baseline = directory.filePath(QStringLiteral("baseline.json"));
        QFile file(baseline);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(snapshot->payload), snapshot->payload.size());
        file.close();

        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        const QString serverStore = directory.filePath(QStringLiteral("server.json"));
        service.setStoragePath(serverStore);
        QVERIFY(service.open());
        PropertySyncSocketServer server;
        server.setService(&service);
        const QString socketName = directory.filePath(QStringLiteral("sync.sock"));
        server.setSocketName(socketName);
        QVERIFY(server.grant(QString::fromLatin1(ActorEditor),
                             QString::fromLatin1(EditorCredential), true));
        QVERIFY(server.grant(QString::fromLatin1(ActorViewer),
                             QString::fromLatin1(ViewerCredential), false));
        QVERIFY(server.listen());

        auto parentReplica = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(parentReplica);
        LocalPropertySyncService parentLocal;
        parentLocal.setDocumentId(base.documentIdString());
        parentLocal.setStoragePath(directory.filePath(QStringLiteral("parent.json")));
        QVERIFY(parentLocal.open());
        LocalPropertySyncClient parentClient;
        parentClient.setDocument(parentReplica->document.get());
        parentClient.setService(&parentLocal);
        parentClient.setActorId(QString::fromLatin1(ActorViewer));
        QVERIFY(parentClient.connectToService());
        PropertySyncSocketClient parentSocket;
        parentSocket.setService(&parentLocal);
        parentSocket.setActorId(QString::fromLatin1(ActorViewer));
        parentSocket.setCredential(QString::fromLatin1(ViewerCredential));
        parentSocket.setSocketName(socketName);
        parentSocket.setLocalClient(&parentClient);
        QVERIFY(parentSocket.connectToServer());
        QTRY_VERIFY(parentSocket.isConnected());
        QSignalSpy present(&parentClient, &LocalPropertySyncClient::presenceChanged);
        QSignalSpy left(&parentClient, &LocalPropertySyncClient::presenceLeft);

        const QString childStore = directory.filePath(QStringLiteral("child.json"));
        QProcess child;
        startWorker(child, QStringLiteral("presence"), baseline, childStore,
                    QString::fromLatin1(ActorEditor), entity.toString(), socketName,
                    QString::fromLatin1(EditorCredential));
        QTRY_COMPARE_WITH_TIMEOUT(present.size(), 1, 5000);
        QCOMPARE(present.first().at(0).toString(), QString::fromLatin1(ActorEditor));
        QCOMPARE(present.first().at(1).toMap().value(QStringLiteral("selection")).toString(),
                 entity.toString());

        // A reconnecting observer receives the still-live actor snapshot;
        // disconnect removes the old presence once from its local view.
        parentSocket.disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(left.size(), 1, 5000);
        QVERIFY(parentSocket.connectToServer());
        QTRY_COMPARE_WITH_TIMEOUT(present.size(), 2, 5000);
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitCode(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(left.size(), 2, 5000);

        QProcess childRestarted;
        startWorker(childRestarted, QStringLiteral("presence"), baseline, childStore,
                    QString::fromLatin1(ActorEditor), entity.toString(), socketName,
                    QString::fromLatin1(EditorCredential));
        QTRY_COMPARE_WITH_TIMEOUT(present.size(), 3, 5000);
        QVERIFY(childRestarted.waitForFinished(5000));
        QCOMPARE(childRestarted.exitCode(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(left.size(), 3, 5000);
        QCOMPARE(service.operationCount(), 0);
        QVERIFY(!QFile::exists(serverStore));
    }

    void durableOfflineEditCrossesProcessAndViewerIsRejected()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PropertyDocument base;
        PropertyDescriptor gain;
        gain.id = QStringLiteral("gain");
        gain.type = QMetaType::fromType<double>();
        gain.defaultValue = 0.0;
        QVERIFY(base.defineProperty(gain));
        const EntityId entity = EntityId::create();
        QVERIFY(base.addEntity(entity));
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        const QString baseline = directory.filePath(QStringLiteral("baseline.json"));
        QFile file(baseline);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(snapshot->payload), snapshot->payload.size());
        file.close();

        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        service.setStoragePath(directory.filePath(QStringLiteral("server.json")));
        QVERIFY(service.open());
        PropertySyncSocketServer server;
        server.setService(&service);
        const QString socketName = directory.filePath(QStringLiteral("sync.sock"));
        server.setSocketName(socketName);
        QVERIFY(server.grant(QString::fromLatin1(ActorEditor),
                             QString::fromLatin1(EditorCredential), true));
        QVERIFY(server.grant(QString::fromLatin1(ActorViewer),
                             QString::fromLatin1(ViewerCredential), false));
        QVERIFY(server.listen());

        auto parentReplica = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(parentReplica);
        LocalPropertySyncClient parentClient;
        parentClient.setDocument(parentReplica->document.get());
        parentClient.setService(&service);
        parentClient.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000c"));
        QVERIFY(parentClient.connectToService());

        const QString editorStore = directory.filePath(QStringLiteral("editor.json"));
        QProcess offline;
        startWorker(offline, QStringLiteral("offline"), baseline, editorStore,
                    QString::fromLatin1(ActorEditor), entity.toString(), socketName,
                    QString::fromLatin1(EditorCredential));
        QVERIFY(offline.waitForFinished(5000));
        QCOMPARE(offline.exitCode(), 0);
        QCOMPARE(service.operationCount(), 0);
        QVERIFY(QFile::exists(editorStore));

        // A new process reopens the durable local log and sends the same
        // operation UUID. The parent server and live parent replica receive it.
        QProcess editor;
        startWorker(editor, QStringLiteral("connect"), baseline, editorStore,
                    QString::fromLatin1(ActorEditor), entity.toString(), socketName,
                    QString::fromLatin1(EditorCredential));
        QTRY_COMPARE_WITH_TIMEOUT(service.operationCount(), 1, 5000);
        QCOMPARE(parentReplica->document->value(entity, QStringLiteral("gain")).toDouble(), 7.0);
        QVERIFY(editor.waitForFinished(5000));
        QCOMPARE(editor.exitCode(), 0);

        // An actor UUID alone cannot authenticate a second process.
        QProcess impostor;
        startWorker(impostor, QStringLiteral("connect"), baseline, editorStore,
                    QString::fromLatin1(ActorEditor), entity.toString(), socketName,
                    QStringLiteral("incorrect-credential"));
        QTRY_COMPARE_WITH_TIMEOUT(impostor.state(), QProcess::NotRunning, 5000);
        QCOMPARE(impostor.exitCode(), 17);
        QCOMPARE(service.operationCount(), 1);

        const QString viewerStore = directory.filePath(QStringLiteral("viewer.json"));
        QProcess viewerOffline;
        startWorker(viewerOffline, QStringLiteral("offline"), baseline, viewerStore,
                    QString::fromLatin1(ActorViewer), entity.toString(), socketName,
                    QString::fromLatin1(ViewerCredential));
        QVERIFY(viewerOffline.waitForFinished(5000));
        QCOMPARE(viewerOffline.exitCode(), 0);
        QProcess viewer;
        startWorker(viewer, QStringLiteral("connect"), baseline, viewerStore,
                    QString::fromLatin1(ActorViewer), entity.toString(), socketName,
                    QString::fromLatin1(ViewerCredential));
        QTRY_COMPARE_WITH_TIMEOUT(viewer.state(), QProcess::NotRunning, 5000);
        QCOMPARE(viewer.exitCode(), 17);
        QCOMPARE(service.operationCount(), 1);
        QCOMPARE(parentReplica->document->value(entity, QStringLiteral("gain")).toDouble(), 7.0);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() > 1
        && application.arguments().at(1) == QStringLiteral("--worker"))
        return worker(application.arguments());
    TstPropertySyncSocket test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_property_sync_socket.moc"
