// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <qindatk/document_manager.h>
#include <qindatk/document_session.h>
#include <qindatk/edit_transaction.h>
#include <qindatk/property_document.h>

namespace {
bool check(bool condition, int code, const char *message)
{
    if (condition) return true;
    qCritical("Persistence consumer check %d failed: %s", code, message);
    return false;
}

bool waitForSave(QindaTK::DocumentSession *session)
{
    QEventLoop loop;
    bool completed = false;
    bool success = false;
    QObject::connect(session, &QindaTK::DocumentSession::saveFinished, &loop,
                     [&](bool result, const QString &, const QString &) {
                         completed = true;
                         success = result;
                         loop.quit();
                     });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    if (session->isSaving()) loop.exec();
    return completed && success;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), 1, "temporary directory is available")) return 1;

    QString savedRevision;
    QString entityId;
    {
        QindaTK::DocumentManager manager;
        auto *session = manager.createReferenceDocument();
        if (!check(session && session->isDirty(), 2, "new session starts dirty")) return 2;
        auto *document = session->document();
        const QindaTK::PropertyDescriptor gain{
            QStringLiteral("gain"), QMetaType::fromType<double>(), 0.0,
            QStringLiteral("dB"), true, true};
        const QindaTK::EntityId entity = QindaTK::EntityId::create();
        if (!check(document->defineProperty(gain)
                       && document->addEntity(entity, {{QStringLiteral("gain"), 1.5}}),
                   3, "reference document accepts a typed property and entity")) return 3;
        entityId = entity.toString();

        const QString path = directory.filePath(QStringLiteral("consumer.qtkdoc"));
        if (!check(session->requestSaveAs(path), 4, "Save As starts")) return 4;
        if (!check(waitForSave(session), 5, "Save As completes successfully")) return 5;
        savedRevision = session->revision();
        if (!check(!session->isDirty() && session->savedRevision() == savedRevision,
                   6, "successful save records the saved revision and clears dirty state")) return 6;
    }

    {
        QindaTK::DocumentManager reopenedManager;
        const QString path = directory.filePath(QStringLiteral("consumer.qtkdoc"));
        auto *reopened = reopenedManager.openReferenceDocument(path);
        if (!check(reopened && !reopened->isDirty(), 7, "saved document reopens cleanly")) return 7;
        if (!check(reopened->revision() == savedRevision
                       && reopened->document()->valueFor(
                              entityId, QStringLiteral("gain")).toDouble() == 1.5,
                   8, "reopen preserves revision, entity ID, and typed value")) return 8;

        QindaTK::EditTransaction edit;
        edit.setDocument(reopened->document());
        edit.setUndoService(reopened->undoService());
        if (!check(edit.begin({QindaTK::EntityId::parse(entityId)},
                              QStringLiteral("gain"), QStringLiteral("Change gain"))
                       && !edit.updateValue(2.25) && !edit.commitEdit(),
                   9, "public edit transaction updates the reopened session")) return 9;
        if (!check(reopened->isDirty()
                       && reopened->revision().toULongLong() > savedRevision.toULongLong(),
                   10, "new edit advances revision and marks reopened session dirty")) return 10;
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (!check(engine.rootObjects().size() == 1, 11,
               "installed QML module instantiates DocumentManager")) return 11;
    auto *qmlManager = engine.rootObjects().first()->findChild<QindaTK::DocumentManager *>(
        QStringLiteral("qmlDocumentManager"));
    if (!check(qmlManager && qmlManager->activeSession()
                   && qmlManager->activeSession()->isDirty(),
               12, "QML-created DocumentManager creates a dirty reference session")) return 12;
    return 0;
}
