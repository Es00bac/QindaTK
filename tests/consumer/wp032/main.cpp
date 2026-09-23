// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QVariantMap>

#include <qindatk/diff_model.h>
#include <qindatk/event_log_model.h>
#include <qindatk/resource_plan_model.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1) return 1;
    QObject *root = engine.rootObjects().first();
    for (const QString &name : {QStringLiteral("installedEventLogView"),
                                QStringLiteral("installedDiffView"),
                                QStringLiteral("installedResourceTable")})
        if (!root->findChild<QObject *>(name)) return 2;

    auto *events = root->findChild<QindaTK::EventLogModel *>(
        QStringLiteral("installedEventModel"));
    auto *diff = root->findChild<QindaTK::DiffModel *>(
        QStringLiteral("installedDiffModel"));
    auto *resources = root->findChild<QindaTK::ResourcePlanModel *>(
        QStringLiteral("installedResourceModel"));
    if (!events || !diff || !resources) return 3;

    if (events->appendEvent({{QStringLiteral("eventId"), QStringLiteral("event-1")},
                             {QStringLiteral("source"), QStringLiteral("consumer")},
                             {QStringLiteral("message"), QStringLiteral("loaded")}}) != 1
        || events->eventAt(0).value(QStringLiteral("eventId")) != QStringLiteral("event-1"))
        return 4;
    if (!diff->setDocuments({QStringLiteral("before")}, {QStringLiteral("after")},
                            QStringLiteral("left-1"), QStringLiteral("right-1"))
        || diff->hunkCount() != 1 || diff->get(0).value(QStringLiteral("kind"))
                                      != QStringLiteral("change"))
        return 5;
    if (!resources->setResources({QVariantMap{{QStringLiteral("id"), QStringLiteral("crew")},
                                             {QStringLiteral("name"), QStringLiteral("Crew")},
                                             {QStringLiteral("unit"), QStringLiteral("hours")},
                                             {QStringLiteral("capacity"), 8}}})
        || !resources->setBuckets({QVariantMap{{QStringLiteral("id"), QStringLiteral("day-1")},
                                            {QStringLiteral("name"), QStringLiteral("Day 1")}}})
        || !resources->requestAllocation(QStringLiteral("crew"), QStringLiteral("day-1"), 5)
        || resources->allocation(QStringLiteral("crew"), QStringLiteral("day-1")) != 5
        || !resources->undo()
        || resources->allocation(QStringLiteral("crew"), QStringLiteral("day-1")) != 0)
        return 6;
    return 0;
}
