// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTimer>

#include <qindatk/canvas_document.h>
#include <qindatk/canvas_document_format.h>
#include <qindatk/document_manager.h>
#include <qindatk/edit_transaction.h>

using namespace QindaTK;

namespace {
int failures = 0;

bool check(bool good, const char *what)
{
    if (!good) {
        qCritical("domain-session consumer: %s", what);
        ++failures;
    }
    return good;
}

bool saveAs(DocumentSession *session, const QString &path)
{
    QEventLoop loop;
    bool success = false;
    QObject::connect(session, &DocumentSession::saveFinished, &loop,
                     [&](bool ok) { success = ok; loop.quit(); });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    if (!session->requestSaveAs(path)) return false;
    loop.exec();
    return success;
}

bool save(DocumentSession *session)
{
    QEventLoop loop;
    bool success = true;
    QObject::connect(session, &DocumentSession::saveFinished, &loop,
                     [&](bool ok) { success = ok; loop.quit(); });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    if (!session->requestSave()) return false;
    loop.exec();
    return success;
}

QByteArray content(DocumentSession *session)
{
    auto *canvas = qobject_cast<CanvasDocument *>(session ? session->modelObject() : nullptr);
    return canvas ? CanvasDocumentFormat::encode(canvas->state()) : QByteArray{};
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir temp;
    if (!check(temp.isValid(), "temporary directory")) return 1;
    const QString recovery = temp.filePath(QStringLiteral("recovery"));
    const QString canvasSchema = QString::fromLatin1(CanvasDocumentFormat::SchemaId);

    DocumentManager manager;
    manager.recoveryManager()->setDirectory(recovery);
    DocumentSession *notes = manager.createReferenceDocument();
    DocumentSession *board = manager.createDocument(canvasSchema);
    if (!check(notes && board && notes->document() && !board->document(), "create both kinds")) return 1;
    board->setAutosaveDelayMs(600000);
    const EntityId track = EntityId::create();
    check(notes->document()->defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                             0.0, {}, true, false})
              && notes->document()->addEntity(track), "define the property document");
    EditTransaction edit;
    edit.setDocument(notes->document());
    edit.setUndoService(manager.undoService());
    check(edit.begin({track}, QStringLiteral("gain"), QStringLiteral("Gain"))
              && !edit.updateValue(6.0) && !edit.commitEdit(), "property edit");
    auto *canvas = qobject_cast<CanvasDocument *>(board->modelObject());
    check(canvas && canvas->addRect(0, 0, 20, 10, Qt::red) && canvas->addEllipse(5, 5, 8, 8, Qt::blue),
          "canvas edits");
    check(manager.activate(notes->documentId()) && manager.undoService()->undo()
              && notes->document()->value(track, QStringLiteral("gain")).toDouble() == 0.0
              && canvas->objectCount() == 2, "undo reaches only the active property session");
    check(manager.activate(board->documentId()) && manager.undoService()->undo()
              && canvas->objectCount() == 1 && canvas->redo(), "undo reaches only the active canvas");

    const QString notesPath = temp.filePath(QStringLiteral("notes.qtk"));
    const QString boardPath = temp.filePath(QStringLiteral("board.qtkc"));
    check(saveAs(notes, notesPath) && saveAs(board, boardPath), "save both kinds");
    const QByteArray saved = content(board);
    const QString boardId = board->documentId();
    check(manager.closeDocument(boardId, DocumentManager::ClosePolicy::Cancel), "close clean canvas");
    DocumentSession *reopened = manager.openDocument(boardPath);
    check(reopened && reopened->kind() == QStringLiteral("canvas") && !reopened->isDirty()
              && content(reopened) == saved, "reopen canvas by content with identical state");

    auto *live = qobject_cast<CanvasDocument *>(reopened ? reopened->modelObject() : nullptr);
    if (!check(live != nullptr, "reopened canvas model")) return 1;
    reopened->setAutosaveDelayMs(600000);
    live->addRect(40, 0, 5, 5, Qt::green);
    QFile foreign(boardPath);
    check(foreign.open(QIODevice::WriteOnly | QIODevice::Truncate) && foreign.write("elsewhere") > 0,
          "external writer");
    foreign.close();
    check(!save(reopened) && reopened->isDirty() && reopened->externalChanged(),
          "save refuses the external replacement");
    const QByteArray expected = content(reopened);
    DocumentManager restarted;
    restarted.recoveryManager()->setDirectory(recovery);
    DocumentSession *restored = restarted.restoreRecovered(boardId, boardPath);
    check(restored && restored->recovered() && content(restored) == expected,
          "validated canvas recovery after the conflict");

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import QindaTK as Tk
Item {
    property bool ok: false
    Tk.DocumentManager { id: manager }
    Component.onCompleted: {
        manager.recoveryManager.directory = ""
        const session = manager.createDocument("qindatk.canvas-document.v1")
        session.model.addRect(1, 2, 3, 4, "red")
        ok = session.kind === "canvas" && session.document === null
            && manager.undoService.activeModel === session.model && manager.undoService.canUndo
    }
})", QUrl());
    std::unique_ptr<QObject> root(component.create());
    check(root && root->property("ok").toBool(), qPrintable(component.errorString()));
    if (failures) return 1;
    qInfo("domain-session consumer: property and canvas sessions, shared undo, save, reopen, "
          "conflict recovery and QML session model passed");
    return 0;
}
