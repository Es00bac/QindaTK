// SPDX-License-Identifier: LGPL-3.0-or-later
#include "dock_model.h"

#include <QtTest>

using namespace QindaTK;

namespace {

QStringList slotIds(const QVariantList &lanes, int lane, int slot)
{
    return lanes[lane].toMap().value(QStringLiteral("slots")).toList()[slot].toMap()
        .value(QStringLiteral("panelIds")).toStringList();
}

void registerThree(DockModel &model)
{
    model.registerPanel(QStringLiteral("image"), QStringLiteral("layers"),
                        {{QStringLiteral("title"), QStringLiteral("Layers")},
                         {QStringLiteral("zone"), QStringLiteral("right")},
                         {QStringLiteral("extent"), 260},
                         {QStringLiteral("order"), 0}});
    model.registerPanel(QStringLiteral("image"), QStringLiteral("props"),
                        {{QStringLiteral("title"), QStringLiteral("Properties")},
                         {QStringLiteral("zone"), QStringLiteral("right")},
                         {QStringLiteral("order"), 1}});
    model.registerPanel(QStringLiteral("image"), QStringLiteral("history"),
                        {{QStringLiteral("title"), QStringLiteral("History")},
                         {QStringLiteral("zone"), QStringLiteral("left")},
                         {QStringLiteral("allowedZones"), QStringList{QStringLiteral("left"), QStringLiteral("right")}},
                         {QStringLiteral("minWidth"), 180}});
    model.setWorkspace(QStringLiteral("image"));
}

} // namespace

class TstDockModel final : public QObject {
    Q_OBJECT
private slots:
    void registersAndQueries()
    {
        DockModel model;
        registerThree(model);
        QCOMPARE(model.panelIds(), (QStringList{QStringLiteral("history"), QStringLiteral("layers"), QStringLiteral("props")}));
        const QVariantList lanes = model.lanes(QStringLiteral("right"));
        QCOMPARE(lanes.size(), 1);
        QCOMPARE(lanes[0].toMap().value(QStringLiteral("slots")).toList().size(), 2);
        QCOMPARE(slotIds(lanes, 0, 0), QStringList{QStringLiteral("layers")});
        QCOMPARE(model.zoneExtent(QStringLiteral("right")), 280.0);
        QCOMPARE(model.panel(QStringLiteral("layers")).value(QStringLiteral("title")).toString(), QStringLiteral("Layers"));
        QVERIFY(model.acceptsZone(QStringLiteral("history"), QStringLiteral("left")));
        QVERIFY(!model.acceptsZone(QStringLiteral("history"), QStringLiteral("bottom")));
        QVERIFY(model.acceptsZone(QStringLiteral("layers"), QStringLiteral("bottom")));
    }
    void modes()
    {
        DockModel model;
        registerThree(model);
        QSignalSpy spy(&model, &DockModel::layoutChanged);
        model.floatPanel(QStringLiteral("layers"), 10, 20);
        QCOMPARE(model.panel(QStringLiteral("layers")).value(QStringLiteral("mode")).toString(), QStringLiteral("floating"));
        QCOMPARE(model.panel(QStringLiteral("layers")).value(QStringLiteral("x")).toDouble(), 10.0);
        QCOMPARE(model.floatingPanels().size(), 1);
        QCOMPARE(model.lanes(QStringLiteral("right"))[0].toMap().value(QStringLiteral("slots")).toList().size(), 1);
        model.hide(QStringLiteral("props"));
        QVERIFY(model.lanes(QStringLiteral("right")).isEmpty());
        model.show(QStringLiteral("props"));
        QCOMPARE(model.panel(QStringLiteral("props")).value(QStringLiteral("mode")).toString(), QStringLiteral("docked"));
        model.collapse(QStringLiteral("props"), true);
        QVERIFY(model.panel(QStringLiteral("props")).value(QStringLiteral("collapsed")).toBool());
        model.toggle(QStringLiteral("props"));
        QVERIFY(model.panel(QStringLiteral("props")).value(QStringLiteral("hidden")).toBool());
        model.dock(QStringLiteral("layers"), QStringLiteral("bottom"));
        QCOMPARE(model.panel(QStringLiteral("layers")).value(QStringLiteral("zone")).toString(), QStringLiteral("bottom"));
        model.dock(QStringLiteral("history"), QStringLiteral("bottom"));
        QCOMPARE(model.panel(QStringLiteral("history")).value(QStringLiteral("zone")).toString(), QStringLiteral("left"));
        QVERIFY(spy.count() >= 6);
    }
    void groups()
    {
        DockModel model;
        registerThree(model);
        model.groupWith(QStringLiteral("props"), QStringLiteral("layers"));
        QVariantList lanes = model.lanes(QStringLiteral("right"));
        QCOMPARE(lanes[0].toMap().value(QStringLiteral("slots")).toList().size(), 1);
        QCOMPARE(slotIds(lanes, 0, 0), (QStringList{QStringLiteral("layers"), QStringLiteral("props")}));
        QCOMPARE(lanes[0].toMap().value(QStringLiteral("slots")).toList()[0].toMap().value(QStringLiteral("activeId")).toString(),
                 QStringLiteral("props"));
        model.activateInGroup(QStringLiteral("layers"));
        lanes = model.lanes(QStringLiteral("right"));
        QCOMPARE(lanes[0].toMap().value(QStringLiteral("slots")).toList()[0].toMap().value(QStringLiteral("activeId")).toString(),
                 QStringLiteral("layers"));
        model.moveInGroup(QStringLiteral("props"), 0);
        QCOMPARE(slotIds(model.lanes(QStringLiteral("right")), 0, 0), (QStringList{QStringLiteral("props"), QStringLiteral("layers")}));
        model.ungroup(QStringLiteral("props"));
        lanes = model.lanes(QStringLiteral("right"));
        QCOMPARE(lanes[0].toMap().value(QStringLiteral("slots")).toList().size(), 2);
        QVERIFY(model.panel(QStringLiteral("layers")).value(QStringLiteral("group")).toString().isEmpty());
    }
    void dockBeside()
    {
        DockModel model;
        registerThree(model);
        model.dockBeside(QStringLiteral("history"), QStringLiteral("layers"), QStringLiteral("after"));
        QCOMPARE(slotIds(model.lanes(QStringLiteral("right")), 0, 1), QStringList{QStringLiteral("history")});
        QCOMPARE(slotIds(model.lanes(QStringLiteral("right")), 0, 2), QStringList{QStringLiteral("props")});
        model.dockBeside(QStringLiteral("history"), QStringLiteral("layers"), QStringLiteral("before"));
        QCOMPARE(slotIds(model.lanes(QStringLiteral("right")), 0, 0), QStringList{QStringLiteral("history")});
        model.dockBeside(QStringLiteral("history"), QStringLiteral("props"), QStringLiteral("lane-after"));
        const QVariantList lanes = model.lanes(QStringLiteral("right"));
        QCOMPARE(lanes.size(), 2);
        QCOMPARE(slotIds(lanes, 1, 0), QStringList{QStringLiteral("history")});
        model.dockBeside(QStringLiteral("history"), QStringLiteral("props"), QStringLiteral("tab"));
        QCOMPARE(model.lanes(QStringLiteral("right")).size(), 1);
        QCOMPARE(slotIds(model.lanes(QStringLiteral("right")), 0, 1), (QStringList{QStringLiteral("props"), QStringLiteral("history")}));
        // Zone rules still apply through dockBeside.
        model.dock(QStringLiteral("layers"), QStringLiteral("bottom"));
        model.dockBeside(QStringLiteral("history"), QStringLiteral("layers"), QStringLiteral("after"));
        QCOMPARE(model.panel(QStringLiteral("history")).value(QStringLiteral("zone")).toString(), QStringLiteral("right"));
    }
    void geometry()
    {
        DockModel model;
        registerThree(model);
        model.resizeDocked(QStringLiteral("history"), 50);
        QCOMPARE(model.zoneExtent(QStringLiteral("left")), 180.0);
        model.resizeDocked(QStringLiteral("layers"), 320);
        QCOMPARE(model.panel(QStringLiteral("props")).value(QStringLiteral("extent")).toDouble(), 320.0);
        model.setShare(QStringLiteral("layers"), 2);
        QCOMPARE(model.lanes(QStringLiteral("right"))[0].toMap().value(QStringLiteral("slots")).toList()[0].toMap().value(QStringLiteral("share")).toDouble(), 2.0);
        model.floatPanel(QStringLiteral("layers"));
        model.resizeFloating(QStringLiteral("layers"), 10, 10);
        QCOMPARE(model.panel(QStringLiteral("layers")).value(QStringLiteral("width")).toDouble(), 160.0);
        model.floatPanel(QStringLiteral("props"));
        model.bringToFront(QStringLiteral("layers"));
        QCOMPARE(model.floatingPanels().last().toMap().value(QStringLiteral("panelId")).toString(), QStringLiteral("layers"));
    }
    void presetsAndSavedLayouts()
    {
        DockModel model;
        registerThree(model);
        model.registerPreset(QStringLiteral("image"), QStringLiteral("paint"),
                             {{QStringLiteral("history"), QStringLiteral("hidden")},
                              {QStringLiteral("props"), QVariantMap{{QStringLiteral("zone"), QStringLiteral("bottom")}}}});
        QCOMPARE(model.presets(), QStringList{QStringLiteral("paint")});
        model.applyPreset(QStringLiteral("paint"));
        QVERIFY(model.panel(QStringLiteral("history")).value(QStringLiteral("hidden")).toBool());
        QCOMPARE(model.panel(QStringLiteral("props")).value(QStringLiteral("zone")).toString(), QStringLiteral("bottom"));
        model.saveLayout(QStringLiteral("mine"));
        model.reset();
        QVERIFY(!model.panel(QStringLiteral("history")).value(QStringLiteral("hidden")).toBool());
        QCOMPARE(model.savedLayouts(), QStringList{QStringLiteral("mine")});
        model.applyLayout(QStringLiteral("mine"));
        QVERIFY(model.panel(QStringLiteral("history")).value(QStringLiteral("hidden")).toBool());
        model.deleteLayout(QStringLiteral("mine"));
        QVERIFY(model.savedLayouts().isEmpty());
    }
    void serializeRoundTrip()
    {
        DockModel model;
        registerThree(model);
        model.floatPanel(QStringLiteral("layers"), 33, 44);
        model.groupWith(QStringLiteral("history"), QStringLiteral("props"));
        model.saveLayout(QStringLiteral("snap"));
        const QString json = model.serialize();
        QVERIFY(json.contains(QStringLiteral("\"workspaces\"")));

        DockModel restored;
        QVERIFY(restored.deserialize(json));      // before registration: pending
        registerThree(restored);
        QCOMPARE(restored.panel(QStringLiteral("layers")).value(QStringLiteral("mode")).toString(), QStringLiteral("floating"));
        QCOMPARE(restored.panel(QStringLiteral("layers")).value(QStringLiteral("x")).toDouble(), 33.0);
        QCOMPARE(slotIds(restored.lanes(QStringLiteral("right")), 0, 0), (QStringList{QStringLiteral("props"), QStringLiteral("history")}));
        QCOMPARE(restored.savedLayouts(), QStringList{QStringLiteral("snap")});
        QVERIFY(!restored.deserialize(QStringLiteral("not json")));
    }
};

QTEST_GUILESS_MAIN(TstDockModel)
#include "tst_dock_model.moc"
