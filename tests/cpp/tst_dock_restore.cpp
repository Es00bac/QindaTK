// SPDX-License-Identifier: LGPL-3.0-or-later
#include "core/dock_model.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using QindaTK::DockModel;

namespace {

void registerPanels(DockModel &model)
{
    model.registerPanel(QStringLiteral("edit"), QStringLiteral("layers"),
                        {{QStringLiteral("zone"), QStringLiteral("right")}});
    model.registerPanel(QStringLiteral("edit"), QStringLiteral("history"),
                        {{QStringLiteral("zone"), QStringLiteral("left")}});
    model.setWorkspace(QStringLiteral("edit"));
}

QString document(const QJsonObject &workspaces, int version = 1)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), version},
        {QStringLiteral("workspaces"), workspaces},
        {QStringLiteral("saved"), QJsonObject{}}
    }).toJson(QJsonDocument::Compact));
}

} // namespace

class TstDockRestore final : public QObject {
    Q_OBJECT
private slots:
    void rejectsUnsupportedVersionWithoutMutation()
    {
        DockModel model;
        registerPanels(model);
        model.floatPanel(QStringLiteral("layers"), 31, 42);
        const QString before = model.serialize();
        QVERIFY(!model.deserialize(document({{QStringLiteral("edit"), QJsonObject{}}}, 2)));
        QCOMPARE(model.serialize(), before);
    }

    void rejectsMalformedNestedArrangementAtomically()
    {
        DockModel model;
        registerPanels(model);
        const QString before = model.serialize();
        const QJsonObject panels{
            {QStringLiteral("layers"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("floating")},
                                                    {QStringLiteral("floatingRect"), QJsonObject{
                                                        {QStringLiteral("x"), 300}, {QStringLiteral("y"), 20},
                                                        {QStringLiteral("width"), -1}, {QStringLiteral("height"), 100}}}}},
            {QStringLiteral("history"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("hidden")}}}
        };
        QVERIFY(!model.deserialize(document({{QStringLiteral("edit"), panels}})));
        QCOMPARE(model.serialize(), before);
        QVERIFY(!model.panel(QStringLiteral("history")).value(QStringLiteral("hidden")).toBool());
    }

    void validRestoreKeepsUnknownPanelsPending()
    {
        DockModel model;
        model.setWorkspace(QStringLiteral("edit"));
        const QJsonObject panels{
            {QStringLiteral("future-panel"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("floating")},
                                                         {QStringLiteral("floatingRect"), QJsonObject{
                                                             {QStringLiteral("x"), 20}, {QStringLiteral("y"), 30},
                                                             {QStringLiteral("width"), 240}, {QStringLiteral("height"), 180}}}}}
        };
        QVERIFY(model.deserialize(document({{QStringLiteral("edit"), panels}})));
        model.registerPanel(QStringLiteral("edit"), QStringLiteral("future-panel"), {});
        QCOMPARE(model.panel(QStringLiteral("future-panel")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("floating"));
        QCOMPARE(model.panel(QStringLiteral("future-panel")).value(QStringLiteral("x")).toDouble(), 20.0);
    }

    void floatingHeadersStayReachableAfterViewportShrink()
    {
        DockModel model;
        registerPanels(model);
        model.floatPanel(QStringLiteral("layers"), -900, 1200);
        model.setFloatingRect(QStringLiteral("layers"), -900, 1200, 400, 300);
        QVERIFY(model.constrainFloatingToViewport(640, 360, 48, 24));
        QVariantMap panel = model.panel(QStringLiteral("layers"));
        QCOMPARE(panel.value(QStringLiteral("x")).toDouble(), -352.0);
        QCOMPARE(panel.value(QStringLiteral("y")).toDouble(), 336.0);
        QVERIFY(!model.constrainFloatingToViewport(640, 360, 48, 24));
        QVERIFY(!model.constrainFloatingToViewport(0, 360, 48, 24));
    }
};

QTEST_GUILESS_MAIN(TstDockRestore)
#include "tst_dock_restore.moc"
