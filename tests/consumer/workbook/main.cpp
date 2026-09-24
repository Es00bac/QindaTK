// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <qindatk/document_manager.h>
#include <qindatk/format_registry.h>
#include <qindatk/job_model.h>
#include <qindatk/undo_service.h>
#include <qindatk/workbook.h>
#include <qindatk/workbook_delimited.h>
#include <qindatk/workbook_sheet_adapter.h>

// Checks the installed WP-055 package: every view instantiates and renders
// cells from the installed module, and the C++ API computes, formats,
// validates, pastes across workbooks, exports, saves, reopens and undoes
// through the shared document framework (DocumentManager, DocumentSession,
// UndoService). Each failure has its own exit code.
namespace {
// Spins the event loop until `done` or the deadline.
template <typename Predicate>
bool waitFor(Predicate done, int milliseconds = 10000)
{
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(10);
    deadline.start(milliseconds);
    if (!done()) loop.exec();
    return done();
}
QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1) return 1;
    QObject *root = engine.rootObjects().first();
    for (const char *name : {"installedTabs", "installedFormulaBar", "installedSheetView",
                             "installedConditionalEditor", "installedValidationEditor"})
        if (!root->findChild<QObject *>(QLatin1StringView(name))) return 2;

    using namespace QindaTK;
    auto *documents = root->findChild<DocumentManager *>(QStringLiteral("installedDocuments"));
    auto *session = qobject_cast<DocumentSession *>(root->property("session").value<QObject *>());
    auto *book = session ? qobject_cast<Workbook *>(session->modelObject()) : nullptr;
    auto *other = root->findChild<Workbook *>(QStringLiteral("installedOtherWorkbook"));
    UndoService *history = documents ? documents->undoService() : nullptr;
    auto *registry = root->findChild<FormatRegistry *>(QStringLiteral("installedRegistry"));
    QObject *sheet = root->findChild<QObject *>(QStringLiteral("installedSheetView"));
    QObject *formulaBar = root->findChild<QObject *>(QStringLiteral("installedFormulaBar"));
    if (!documents || !session || !book || !other || !history || !registry) return 3;
    const QString data = book->sheetIdAt(0);

    // Formulas, a whole-column reference, a name and a number format.
    if (book->cellDisplay(data, 3, 1) != QStringLiteral("1,800.75") || book->cellValue(data, 2, 1).toDouble() != 600.25
        || book->cellDisplay(data, 1, 2) != QStringLiteral("4") || book->cellValue(data, 1, 3).toDouble() != 1800.75)
        return 4;

    // The installed SheetView renders cells through the installed adapter.
    auto *adapter = qobject_cast<WorkbookSheetAdapter *>(sheet->property("adapter").value<QObject *>());
    QObject *grid = sheet->property("grid").value<QObject *>();
    if (!adapter || !grid) return 5;
    if (!waitFor([&] { return grid->property("visibleCellRecords").toList().size() > 0 && adapter->sourceReadCount() > 0; }))
        return 6;
    if (sheet->property("activeAddress").toString() != QStringLiteral("B4")
        || formulaBar->property("result").toString() != QStringLiteral("1,800.75")
        || sheet->property("activeInput").toString() != QStringLiteral("=SUM(B2:B3)"))
        return 7;

    // Conditional formats and validation.
    const QString rule = book->setConditionalFormat(data, {{"ranges", QStringList{"B2:B3"}}, {"type", "cellValue"},
        {"operator", "greater"}, {"value1", "1000"}, {"style", QVariantMap{{"fillColor", "#ffeeee"}}}});
    if (rule.isEmpty() || book->cellPresentation(data, 1, 1).value("fillColor").toString() != QStringLiteral("#ffeeee")
        || book->cellPresentation(data, 2, 1).contains(QStringLiteral("fillColor")))
        return 8;
    if (book->setValidation(data, {{"ranges", QStringList{"B6:B7"}}, {"type", "wholeNumber"}, {"operator", "between"},
                                   {"value1", "1"}, {"value2", "10"}}).isEmpty()
        || book->setCellInput(data, 5, 1, QStringLiteral("11")) || !book->pasteInputs(data, 5, 1,
               {QVariant(QVariantList{"20"}), QVariant(QVariantList{"3"})}, QStringLiteral("skipInvalid"))
        || book->lastPasteReport().value("applied").toInt() != 1
        || book->lastPasteReport().value("invalid").toList().size() != 1 || book->cellDisplay(data, 6, 1) != QStringLiteral("3"))
        return 9;

    // A range pasted into another workbook: relative references move, formats follow.
    const QVariantMap copied = book->rangeTransfer(data, 1, 0, 3, 1);
    if (!copied.value("ok").toBool() || !other->pasteTransfer(other->sheetIdAt(0), 4, 2, copied.value("range").toMap()))
        return 10;
    const QString target = other->sheetIdAt(0);
    if (other->cellInput(target, 5, 3) != QStringLiteral("=D5/2") || other->cellDisplay(target, 6, 3) != QStringLiteral("1,800.75")
        || !other->canUndo() || !other->undo() || !other->cellInput(target, 5, 3).isEmpty())
        return 11;

    // CSV export through the registry, as a snapshot source.
    QTemporaryDir dir;
    if (!dir.isValid() || !book->registerFormat(registry)) return 12;
    const QUrl csv = QUrl::fromLocalFile(dir.filePath(QStringLiteral("data.csv")));
    const QString job = registry->startExportSource(book, csv, QString::fromLatin1(WorkbookDelimitedFormat::CsvId));
    if (job.isEmpty() || !waitFor([&] {
            const QString state = registry->jobs()->job(job).value("state").toString();
            return state == QStringLiteral("succeeded") || state == QStringLiteral("failed");
        }) || !readAll(csv.toLocalFile()).contains("Total,\"1,800.75\""))
        return 13;

    // Shared history: the session's UndoService records the workbook's steps.
    history->setActiveModel(book);
    if (book->undoService() != history || !history->canUndo() || history->undoText() != book->undoText()
        || !history->undo() || book->cellDisplay(data, 6, 1) != QStringLiteral("") || !history->redo())
        return 14;

    // Save on a worker through the session, then reopen it through the manager.
    const QString file = dir.filePath(QStringLiteral("book.qtkwb"));
    bool saved = false, finished = false;
    QObject::connect(session, &DocumentSession::saveFinished, session,
                     [&](bool ok, const QString &, const QString &) { saved = ok; finished = true; });
    if (!session->requestSaveAs(file) || !waitFor([&] { return finished; }) || !saved || session->isDirty()) return 15;
    const QByteArray content = book->serialize();
    DocumentManager reader;
    DocumentSession *reopened = reader.openDocument(file);
    auto *copy = reopened ? qobject_cast<Workbook *>(reopened->modelObject()) : nullptr;
    if (!copy || reopened->isDirty() || copy->serialize() != content
        || copy->cellDisplay(copy->sheetIdAt(0), 3, 1) != QStringLiteral("1,800.75"))
        return 16;
    return 0;
}
