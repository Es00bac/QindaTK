// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbench_host.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTextStream>
#include <QTimer>
#include <qindatk/density.h>

namespace {
void dumpItems(QTextStream &out, QQuickItem *item)
{
    if (!item) return;
    if (item->objectName().startsWith(QStringLiteral("workbench")))
        out << item->objectName() << ' ' << item->x() << ',' << item->y() << ' '
            << item->width() << 'x' << item->height() << '\n';
    for (QQuickItem *child : item->childItems()) dumpItems(out, child);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const int densityOption = arguments.indexOf(QStringLiteral("--density"));
    if (densityOption >= 0 && densityOption + 1 < arguments.size())
        Density::instance()->setModeName(arguments.at(densityOption + 1));
    WorkbenchHost host;
    QQuickView window;
    window.engine()->addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    window.rootContext()->setContextProperty(QStringLiteral("host"), &host);
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setSource(QUrl::fromLocalFile(QStringLiteral(WORKBENCH_QML_FILE)));
    if (window.status() != QQuickView::Ready || !window.rootObject()) return 1;
    host.setNameBinding(window.rootObject()->findChild<PropertyBinding *>(
        QStringLiteral("workbenchNameBinding")));
    if (!host.commands()->resolve(QStringLiteral("workbench.rename")).enabled
        || !host.commands()->resolve(QStringLiteral("workbench.rename")).visible) return 1;
    window.resize(1280, 800);
    window.show();
    if (arguments.contains(QStringLiteral("--check"))) {
        QCoreApplication::processEvents();
        auto *board = host.boardSession();
        auto *sheet = host.sheetSession();
        if (!board || !sheet || host.manager()->sessions().size() != 2
            || host.boardGrid()->readOnly() || !host.boardGrid()->externalUndo()
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[0].id),
                                           QStringLiteral("name"))
                   != QStringLiteral("Opening shot")) return 2;
        QVariant gridAccepted;
        const QString gridName = QStringLiteral("Opening from grid");
        const quint64 gridRevisionBefore = board->document()->revision().value;
        if (!QMetaObject::invokeMethod(window.rootObject(), "editGridCell",
                                       Q_RETURN_ARG(QVariant, gridAccepted),
                                       Q_ARG(QVariant, QVariant(QString::fromLatin1(boardRows[0].id))),
                                       Q_ARG(QVariant, QVariant(QStringLiteral("name"))),
                                       Q_ARG(QVariant, QVariant(gridName)))
            || !gridAccepted.toBool()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != gridName
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != gridName
            || window.rootObject()->property("selectedName").toString() != gridName
            || host.boardAssets()->data(host.boardAssets()->index(0, 0), Qt::UserRole + 4)
                   != QStringLiteral("Opening from grid · Ready")
            || sheet->document()->valueFor(QString::fromLatin1(sheetRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Dialogue")
            || board->document()->revision().value != gridRevisionBefore + 1) return 3;
        if (!host.manager()->undoService()->undo()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening shot")
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening shot")
            || window.rootObject()->property("selectedName").toString() != QStringLiteral("Opening shot")
            || host.boardAssets()->data(host.boardAssets()->index(0, 0), Qt::UserRole + 4)
                   != QStringLiteral("Opening shot · Ready")) return 4;
        const QVariantList pasteRows{
            QVariant(QVariantList{QStringLiteral("Opening pasted")}),
            QVariant(QVariantList{QStringLiteral("Title pasted")})};
        const int historyBeforePaste = host.manager()->undoService()->historyIndex();
        const quint64 revisionBeforePaste = board->document()->revision().value;
        QVariant pasteAccepted;
        if (!QMetaObject::invokeMethod(window.rootObject(), "pasteGridRows",
                                       Q_RETURN_ARG(QVariant, pasteAccepted),
                                       Q_ARG(QVariant, QVariant(0)),
                                       Q_ARG(QVariant, QVariant(pasteRows)))
            || !pasteAccepted.toBool()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening pasted")
            || board->document()->valueFor(QString::fromLatin1(boardRows[1].id),
                                            QStringLiteral("name")) != QStringLiteral("Title pasted")
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening pasted")
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[1].id),
                                            QStringLiteral("name")) != QStringLiteral("Title pasted")
            || host.boardGrid()->sparseCellCount() != 0
            || window.rootObject()->property("selectedName").toString() != QStringLiteral("Opening pasted")
            || host.boardAssets()->data(host.boardAssets()->index(0, 0), Qt::UserRole + 4)
                   != QStringLiteral("Opening pasted · Ready")
            || host.boardAssets()->data(host.boardAssets()->index(1, 0), Qt::UserRole + 4)
                   != QStringLiteral("Title pasted · Review")
            || sheet->document()->valueFor(QString::fromLatin1(sheetRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Dialogue")
            || board->document()->revision().value != revisionBeforePaste + 2
            || host.manager()->undoService()->historyIndex() != historyBeforePaste + 1) return 5;
        if (!host.manager()->undoService()->undo()
            || host.manager()->undoService()->historyIndex() != historyBeforePaste
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening shot")
            || board->document()->valueFor(QString::fromLatin1(boardRows[1].id),
                                            QStringLiteral("name")) != QStringLiteral("Title card")
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening shot")
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[1].id),
                                            QStringLiteral("name")) != QStringLiteral("Title card")
            || host.boardGrid()->sparseCellCount() != 0
            || window.rootObject()->property("selectedName").toString() != QStringLiteral("Opening shot")
            || host.boardAssets()->data(host.boardAssets()->index(0, 0), Qt::UserRole + 4)
                   != QStringLiteral("Opening shot · Ready")
            || host.boardAssets()->data(host.boardAssets()->index(1, 0), Qt::UserRole + 4)
                   != QStringLiteral("Title card · Review")) return 6;
        const auto invokeQml = [&](const char *method, const QVariant &argument) {
            QVariant result;
            const bool invoked = QMetaObject::invokeMethod(window.rootObject(), method,
                Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
            QCoreApplication::processEvents();
            return invoked && result.toBool();
        };
        const auto drainQml = [&] {
            for (int pass = 0; pass < 4; ++pass) QCoreApplication::processEvents();
        };
        const QString retainedBoardRow = QString::fromLatin1(boardRows[2].id);
        const auto activeGridRow = [&] {
            auto *grid = window.rootObject()->findChild<QQuickItem *>(QStringLiteral("workbenchEditableGrid"));
            return grid ? grid->property("activeRowId").toString() : QString{};
        };
        if (!invokeQml("selectGridRow", retainedBoardRow)) return 13;
        if (!invokeQml("toggleGridSort", {}) || host.boardGrid()->rowIdAt(1) != retainedBoardRow
            || activeGridRow() != retainedBoardRow) return 18;
        if (!invokeQml("setGridFilter", QStringLiteral("opening")) || host.boardGrid()->rowCount() != 1)
            return 19;
        if (!invokeQml("setGridFilter", QStringLiteral("")) || host.boardGrid()->rowCount() != 4
            || activeGridRow() != retainedBoardRow) return 20;
        QVariant sortedEditAccepted;
        if (!QMetaObject::invokeMethod(window.rootObject(), "editGridCell",
                                       Q_RETURN_ARG(QVariant, sortedEditAccepted),
                                       Q_ARG(QVariant, QVariant(retainedBoardRow)),
                                       Q_ARG(QVariant, QVariant(QStringLiteral("name"))),
                                       Q_ARG(QVariant, QVariant(QStringLiteral("Aardvark"))))
            || !sortedEditAccepted.toBool()) return 23;
        drainQml();
        if (host.boardGrid()->rowIdAt(0) != retainedBoardRow
            || !host.manager()->undoService()->undo()) return 24;
        drainQml();
        if (host.boardGrid()->rowIdAt(1) != retainedBoardRow
            || activeGridRow() != retainedBoardRow) return 25;
        host.manager()->activate(sheet->documentId());
        drainQml();
        const QString retainedSheetRow = QString::fromLatin1(sheetRows[2].id);
        if (!invokeQml("selectGridRow", retainedSheetRow)) return 14;
        host.manager()->activate(board->documentId());
        drainQml();
        auto *gridItem = window.rootObject()->findChild<QQuickItem *>(QStringLiteral("workbenchEditableGrid"));
        if (!gridItem || gridItem->property("activeRowId").toString() != retainedBoardRow
            || host.sheetGrid()->rowIdAt(0) != QString::fromLatin1(sheetRows[0].id)) return 15;
        host.manager()->activate(sheet->documentId());
        drainQml();
        if (gridItem->property("activeRowId").toString() != retainedSheetRow) return 16;
        if (!invokeQml("selectGridRow", QString::fromLatin1(sheetRows[0].id))) return 22;
        host.manager()->activate(board->documentId());
        drainQml();
        if (!invokeQml("setGridFilter", QStringLiteral(""))) return 17;
        if (!invokeQml("selectGridRow", QString::fromLatin1(boardRows[0].id))) return 21;
        QVariant staged;
        QVariant activated;
        const QString firstName = QStringLiteral("Opening revised");
        const quint64 boardRevisionBefore = board->document()->revision().value;
        if (!QMetaObject::invokeMethod(window.rootObject(), "stageNameValue",
                                       Q_RETURN_ARG(QVariant, staged),
                                       Q_ARG(QVariant, QVariant(firstName)))
            || !staged.toBool()
            || !QMetaObject::invokeMethod(window.rootObject(), "invokeRenameFromToolbar",
                                          Q_RETURN_ARG(QVariant, activated))
            || !activated.toBool()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != firstName
            || host.boardAssets()->data(host.boardAssets()->index(0, 0), Qt::UserRole + 4)
                   != QStringLiteral("Opening revised · Ready")
            || board->document()->revision().value != boardRevisionBefore + 1) return 7;
        if (!host.manager()->undoService()->undo()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name"))
                   != QStringLiteral("Opening shot")) return 8;

        const QString menuName = QStringLiteral("Opening from menu");
        if (!QMetaObject::invokeMethod(window.rootObject(), "stageNameValue",
                                       Q_RETURN_ARG(QVariant, staged),
                                       Q_ARG(QVariant, QVariant(menuName)))
            || !staged.toBool()
            || !QMetaObject::invokeMethod(window.rootObject(), "invokeRenameFromMenu",
                                          Q_RETURN_ARG(QVariant, activated))
            || !activated.toBool()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != menuName
            || !host.manager()->undoService()->undo()) return 9;

        host.manager()->activate(sheet->documentId());
        drainQml();
        if (window.rootObject()->property("activeWorkspaceName") != QStringLiteral("Mix sheet")
            || window.rootObject()->property("selectedName") != QStringLiteral("Dialogue"))
            return 10;
        const QString sheetName = QStringLiteral("Dialogue via shortcut");
        if (!QMetaObject::invokeMethod(window.rootObject(), "stageNameValue",
                                       Q_RETURN_ARG(QVariant, staged),
                                       Q_ARG(QVariant, QVariant(sheetName)))
            || !staged.toBool()) return 11;
        auto *shortcuts = window.rootObject()->findChild<ShortcutMap *>(
            QStringLiteral("workbenchShortcutMap"));
        if (!shortcuts || !shortcuts->dispatchPortable(QStringLiteral("Ctrl+Shift+R"))
            || sheet->document()->valueFor(QString::fromLatin1(sheetRows[0].id),
                                            QStringLiteral("name")) != sheetName
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening shot")
            || !host.manager()->undoService()->undo()
            || sheet->document()->valueFor(QString::fromLatin1(sheetRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Dialogue"))
            return 12;
        host.manager()->activate(board->documentId());
        qInfo("WP-033 workbench: grid sort/filter selection retention, edit/undo, command routing and inactive-document isolation passed");
    }
    const int grabOption = arguments.indexOf(QStringLiteral("--grab"));
    const bool dump = arguments.contains(QStringLiteral("--dump"));
    if (grabOption >= 0 || dump || arguments.contains(QStringLiteral("--check"))) {
        QTimer::singleShot(250, &app, [&] {
            if (dump) {
                QTextStream out(stdout);
                dumpItems(out, window.rootObject());
                out << "documents=" << host.manager()->sessions().size()
                    << " active=" << host.manager()->activeSession()->documentId() << '\n';
                out.flush();
            }
            if (grabOption >= 0 && grabOption + 1 < arguments.size())
                if (!window.grabWindow().save(arguments.at(grabOption + 1)))
                    qCritical("Could not save workbench capture");
            app.quit();
        });
    }
    return app.exec();
}
