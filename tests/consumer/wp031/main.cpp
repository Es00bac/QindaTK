// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

#include <qindatk/distribution_model.h>
#include <qindatk/gantt_task_model.h>
#include <qindatk/heatmap_model.h>
#include <qindatk/ohlc_model.h>
#include <qindatk/part_whole_model.h>
#include <qindatk/polar_data_model.h>
#include <qindatk/target_model.h>
#include <qindatk/uncertainty_model.h>

using namespace QindaTK;

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) qCritical("WP-031 installed consumer: %s", message);
    return condition;
}
QString utc(const char *iso)
{
    return QString::number(QDateTime::fromString(QString::fromLatin1(iso),
                                                 Qt::ISODate).toMSecsSinceEpoch());
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (!check(engine.rootObjects().size() == 1, "installed QML import")) return 1;
    QObject *root = engine.rootObjects().first();
    const QStringList viewNames{
        QStringLiteral("installedHeatmap"), QStringLiteral("installedDistributionPlot"),
        QStringLiteral("installedGanttView"), QStringLiteral("installedPartWholeView"),
        QStringLiteral("installedCandlestickView"), QStringLiteral("installedTargetDisplay"),
        QStringLiteral("installedPolarView"), QStringLiteral("installedUncertaintyView")};
    for (const QString &name : viewNames)
        if (!check(root->findChild<QObject *>(name) != nullptr,
                   qPrintable(QStringLiteral("missing QML view %1").arg(name)))) return 2;

    auto *heat = root->findChild<HeatmapModel *>(QStringLiteral("installedHeatmapModel"));
    auto *distribution = root->findChild<DistributionModel *>(
        QStringLiteral("installedDistributionModel"));
    auto *tasks = root->findChild<GanttTaskModel *>(QStringLiteral("installedGanttTaskModel"));
    auto *parts = root->findChild<PartWholeModel *>(QStringLiteral("installedPartWholeModel"));
    auto *candles = root->findChild<OhlcModel *>(QStringLiteral("installedOhlcModel"));
    auto *targets = root->findChild<TargetModel *>(QStringLiteral("installedTargetModel"));
    auto *polar = root->findChild<PolarDataModel *>(QStringLiteral("installedPolarDataModel"));
    auto *intervals = root->findChild<UncertaintyModel *>(
        QStringLiteral("installedUncertaintyModel"));
    if (!check(heat && distribution && tasks && parts && candles && targets && polar
                   && intervals, "installed C++ model types")) return 3;

    if (!check(heat->setDimensions(2, 2)
                   && heat->setCell(0, 0, QStringLiteral("cold"), -5.0)
                   && heat->setCell(1, 1, QStringLiteral("hot"), 7.0)
                   && heat->storedCellCount() == 2
                   && heat->cellAt(1, 1).value(QStringLiteral("id"))
                          == QStringLiteral("hot"),
               "heatmap source cells")) return 4;

    const QVariantMap group{{QStringLiteral("id"), QStringLiteral("sample")},
                            {QStringLiteral("label"), QStringLiteral("Sample")},
                            {QStringLiteral("unit"), QStringLiteral("ms")},
                            {QStringLiteral("values"), QVariantList{1, 2, 3, 4, 5}},
                            {QStringLiteral("interval"), QVariantMap{
                                {QStringLiteral("low"), 2}, {QStringLiteral("high"), 4},
                                {QStringLiteral("estimate"), 3},
                                {QStringLiteral("meaning"),
                                 QStringLiteral("95% confidence interval")}}}};
    if (!check(distribution->replaceGroups({group})
                   && distribution->get(0).value(QStringLiteral("q1")).toDouble() == 2
                   && distribution->get(0).value(QStringLiteral("median")).toDouble() == 3
                   && distribution->get(0).value(QStringLiteral("q3")).toDouble() == 4
                   && distribution->sourceValues(QStringLiteral("sample")).size() == 5,
               "distribution quantiles and provenance")) return 5;

    const QString taskA = QStringLiteral("00000000-0000-0000-0000-000000000001");
    const QString taskB = QStringLiteral("00000000-0000-0000-0000-000000000002");
    const QVariantList schedule{
        QVariantMap{{QStringLiteral("taskId"), taskA},
                    {QStringLiteral("label"), QStringLiteral("Plan")},
                    {QStringLiteral("startUtc"), utc("2025-03-07T16:00:00Z")},
                    {QStringLiteral("durationMinutes"), 60}},
        QVariantMap{{QStringLiteral("taskId"), taskB},
                    {QStringLiteral("label"), QStringLiteral("Build")},
                    {QStringLiteral("startUtc"), utc("2025-03-10T09:00:00Z")},
                    {QStringLiteral("durationMinutes"), 120},
                    {QStringLiteral("dependencies"), QVariantList{taskA}}}};
    if (!check(tasks->replaceTasks(schedule) && tasks->indexOfId(taskB) == 1
                   && tasks->get(1).value(QStringLiteral("dependencies")).toList().size() == 1,
               "Gantt stable task IDs and dependencies")) return 6;

    if (!check(parts->replaceNodes({QVariantMap{{QStringLiteral("id"), QStringLiteral("all")},
                                                {QStringLiteral("value"), 4}},
                                     QVariantMap{{QStringLiteral("id"), QStringLiteral("a")},
                                                 {QStringLiteral("parentId"), QStringLiteral("all")},
                                                 {QStringLiteral("value"), 1}},
                                     QVariantMap{{QStringLiteral("id"), QStringLiteral("b")},
                                                 {QStringLiteral("parentId"), QStringLiteral("all")},
                                                 {QStringLiteral("value"), 3}}})
                   && parts->knownTotal() == 4
                   && parts->visibleRootIds()
                          == QStringList({QStringLiteral("a"), QStringLiteral("b")}),
               "part-to-whole hierarchy and totals")) return 7;

    const QVariantMap candle{{QStringLiteral("id"), QStringLiteral("trade")},
                             {QStringLiteral("utcMs"), QStringLiteral("1704067200000")},
                             {QStringLiteral("open"), 100}, {QStringLiteral("high"), 110},
                             {QStringLiteral("low"), 95}, {QStringLiteral("close"), 108}};
    const QVariantMap gap{{QStringLiteral("id"), QStringLiteral("holiday")},
                          {QStringLiteral("utcMs"), QStringLiteral("1704153600000")},
                          {QStringLiteral("missing"), true}};
    if (!check(candles->replaceCandles({candle, gap}, QStringLiteral("USD/share"),
                                       QStringLiteral("host session"), QStringLiteral("UTC"),
                                       100.0)
                   && candles->minimum() == 95 && candles->maximum() == 110
                   && candles->get(1).value(QStringLiteral("missing")).toBool(),
               "OHLC session gap and price domain")) return 8;

    const QVariantMap target{{QStringLiteral("id"), QStringLiteral("uptime")},
                             {QStringLiteral("label"), QStringLiteral("Uptime")},
                             {QStringLiteral("unit"), QStringLiteral("%")},
                             {QStringLiteral("minimum"), 0},
                             {QStringLiteral("maximum"), 100},
                             {QStringLiteral("value"), 82},
                             {QStringLiteral("target"), 95},
                             {QStringLiteral("direction"), QStringLiteral("higher")},
                             {QStringLiteral("thresholdMeaning"),
                              QStringLiteral("service availability")},
                             {QStringLiteral("bands"), QVariantList{
                                 QVariantMap{{QStringLiteral("end"), 50},
                                             {QStringLiteral("meaning"), QStringLiteral("low")}},
                                 QVariantMap{{QStringLiteral("end"), 100},
                                             {QStringLiteral("meaning"), QStringLiteral("healthy")}}}}};
    if (!check(targets->replaceTargets({target})
                   && targets->get(0).value(QStringLiteral("target")).toDouble() == 95
                   && targets->indexOfId(QStringLiteral("uptime")) == 0,
               "target value, direction and bands")) return 9;

    if (!check(polar->replacePoints({
                   QVariantMap{{QStringLiteral("seriesId"), QStringLiteral("wind")},
                               {QStringLiteral("pointId"), QStringLiteral("north")},
                               {QStringLiteral("angle"), 0}, {QStringLiteral("radius"), 4}},
                   QVariantMap{{QStringLiteral("seriesId"), QStringLiteral("wind")},
                               {QStringLiteral("pointId"), QStringLiteral("east")},
                               {QStringLiteral("angle"), 90}, {QStringLiteral("radius"), 8}}})
                   && polar->get(1).value(QStringLiteral("radius")).toDouble() == 8,
               "polar source coordinates")) return 10;

    if (!check(intervals->replaceIntervals({
                   QVariantMap{{QStringLiteral("id"), QStringLiteral("i0")},
                               {QStringLiteral("sourceId"), QStringLiteral("sensor-0")},
                               {QStringLiteral("x"), 0}, {QStringLiteral("estimate"), 4},
                               {QStringLiteral("low"), 3}, {QStringLiteral("high"), 5}},
                   QVariantMap{{QStringLiteral("id"), QStringLiteral("i1")},
                               {QStringLiteral("sourceId"), QStringLiteral("sensor-1")},
                               {QStringLiteral("x"), 1}, {QStringLiteral("estimate"), 6}}})
                   && intervals->missingCount() == 1
                   && intervals->interval(QStringLiteral("i0"))
                          .value(QStringLiteral("meaning")) == QStringLiteral("confidence"),
               "typed uncertainty and missing interval")) return 11;

    QCoreApplication::processEvents();
    if (!check(root->property("viewsReady").toBool(), "live QML view/model bindings"))
        return 12;
    return 0;
}
