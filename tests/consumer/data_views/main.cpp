// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QUrl>

#include <qindatk/editable_grid_model.h>
#include <qindatk/query_controller.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1) return 1;

    QObject *root = engine.rootObjects().first();
    const QStringList viewNames{
        QStringLiteral("installedVirtualList"), QStringLiteral("installedVirtualTree"),
        QStringLiteral("installedAssetGrid"), QStringLiteral("installedEditableGrid"),
        QStringLiteral("installedFilterBar"), QStringLiteral("installedQueryBuilder"),
        QStringLiteral("installedPivotView")};
    for (const QString &name : viewNames)
        if (!root->findChild<QObject *>(name)) return 2;

    auto *source = root->findChild<QindaTK::EditableGridModel *>(
        QStringLiteral("installedSourceGrid"));
    auto *query = root->findChild<QindaTK::QueryController *>(
        QStringLiteral("installedQueryController"));
    if (!source || !query || source->cellValue(QStringLiteral("source-east"),
                                               QStringLiteral("amount")).toInt() != 10)
        return 3;

    if (query->status() != QStringLiteral("complete")) {
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(query, &QindaTK::QueryController::statusChanged, &loop,
                         [&] { if (query->status() == QStringLiteral("complete")
                                   || query->status() == QStringLiteral("rejected")) loop.quit(); });
        deadline.start(5000);
        loop.exec();
    }
    if (query->status() != QStringLiteral("complete") || !query->resultModel()) return 4;
    if (!query->resultModel()->readOnly()) return 5;

    bool foundEast = false;
    for (int row = 0; row < query->resultModel()->rows(); ++row) {
        const QString rowId = query->resultModel()->rowIdAt(row);
        if (query->rowLabels().value(rowId).toString() != QStringLiteral("East")) continue;
        const QString columnId = query->resultModel()->columnIdAt(0);
        foundEast = query->cellValue(rowId, columnId).toInt() == 10
            && query->sourceRows(rowId, columnId)
                   == QStringList{QStringLiteral("source-east")};
        break;
    }
    return foundEast ? 0 : 6;
}
