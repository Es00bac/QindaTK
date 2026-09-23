// SPDX-License-Identifier: LGPL-3.0-or-later
#include "resource_plan_model.h"

#include <QSignalSpy>
#include <QtTest>
#include <limits>

using namespace QindaTK;

class TstResourcePlanModel final : public QObject {
    Q_OBJECT
private slots:
    void allocationUpdatesUnitScopedTotalsAndConflictsAtomically()
    {
        ResourcePlanModel plan;
        QVERIFY(plan.setResources({QVariantMap{{"id", "artist"}, {"name", "Artist"},
                                              {"unit", "hours"}, {"capacity", 8.0}},
                                   QVariantMap{{"id", "render"}, {"name", "Render node"},
                                              {"unit", "GPU hours"}, {"capacity", 6.0}}}));
        QVERIFY(plan.setBuckets({QVariantMap{{"id", "monday"}, {"name", "Monday"}},
                                 QVariantMap{{"id", "tuesday"}, {"name", "Tuesday"}}}));
        QCOMPARE(plan.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Monday"));
        QCOMPARE(plan.headerData(0, Qt::Vertical).toString(), QStringLiteral("Artist"));
        QVERIFY(plan.requestAllocation("artist", "monday", 5));
        QVERIFY(plan.requestAllocation("artist", "tuesday", 9));
        QVERIFY(plan.requestAllocation("render", "monday", 8));

        QCOMPARE(plan.resourceUsage("artist"), 14.0);
        QCOMPARE(plan.resourceCapacity("artist"), 16.0);
        QVERIFY(plan.resourceConflict("artist"));
        QVERIFY(plan.overAllocated("artist", "tuesday"));
        QVERIFY(!plan.overAllocated("artist", "monday"));
        QCOMPARE(plan.bucketSummary("monday").value("hours").toMap()
                     .value("usage").toDouble(), 5.0);
        QCOMPARE(plan.bucketSummary("monday").value("hours").toMap()
                     .value("capacity").toDouble(), 8.0);
        QVERIFY(!plan.bucketSummary("monday").value("hours").toMap()
                    .value("overAllocated").toBool());
        QVERIFY(plan.bucketSummary("monday").value("GPU hours").toMap()
                    .value("overAllocated").toBool());

        const quint64 revision = plan.revision().toULongLong();
        QSignalSpy changed(&plan, &ResourcePlanModel::planChanged);
        QVERIFY(plan.requestAllocation("artist", "tuesday", 3));
        QCOMPARE(plan.revision().toULongLong(), revision + 1);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(plan.resourceUsage("artist"), 8.0);
        QVERIFY(!plan.resourceConflict("artist"));
        QCOMPARE(plan.bucketSummary("tuesday").value("hours").toMap()
                     .value("usage").toDouble(), 3.0);
        QVERIFY(!plan.bucketSummary("tuesday").value("hours").toMap()
                    .value("overAllocated").toBool());

        QVERIFY(plan.undo());
        QCOMPARE(plan.allocation("artist", "tuesday"), 9.0);
        QCOMPARE(plan.resourceUsage("artist"), 14.0);
        QVERIFY(plan.resourceConflict("artist"));
        QVERIFY(plan.redo());
        QCOMPARE(plan.resourceUsage("artist"), 8.0);
        QVERIFY(!plan.resourceConflict("artist"));
    }

    void stableIdsPreserveAllocationsAndStructureInvalidatesHistory()
    {
        ResourcePlanModel plan;
        QVERIFY(plan.setResources({QVariantMap{{"id", "r1"}, {"name", "One"},
                                              {"unit", "hours"}, {"capacity", 4.0}},
                                   QVariantMap{{"id", "r2"}, {"name", "Two"},
                                              {"unit", "hours"}, {"capacity", 3.0}}}));
        QVERIFY(plan.setBuckets({QVariantMap{{"id", "b1"}, {"name", "First"}},
                                 QVariantMap{{"id", "b2"}, {"name", "Second"}}}));
        QVERIFY(plan.requestAllocation("r2", "b2", 2.5));
        QVERIFY(plan.setResources({QVariantMap{{"id", "r2"}, {"name", "Two renamed"},
                                               {"unit", "hours"}, {"capacity", 5.0}},
                                   QVariantMap{{"id", "r1"}, {"name", "One"},
                                               {"unit", "hours"}, {"capacity", 4.0}}}));
        QCOMPARE(plan.resourceIndex("r2"), 0);
        QCOMPARE(plan.allocation("r2", "b2"), 2.5);
        QCOMPARE(plan.resourceName("r2"), QStringLiteral("Two renamed"));
        QVERIFY(!plan.canUndo());
        QVERIFY(plan.requestAllocation("r2", "b2", 3.0));
        QVERIFY(plan.setBuckets({QVariantMap{{"id", "b2"}, {"name", "Second"}},
                                 QVariantMap{{"id", "b1"}, {"name", "First"}}}));
        QCOMPARE(plan.bucketIndex("b2"), 0);
        QCOMPARE(plan.allocation("r2", "b2"), 3.0);
        QVERIFY(!plan.canUndo());
    }

    void invalidRequestsDoNotMutateAndBoundsAreEnforced()
    {
        ResourcePlanModel plan;
        QVERIFY(plan.setResources({QVariantMap{{"id", "r"}, {"name", "Worker"},
                                               {"unit", "hours"}, {"capacity", 1.0}}}));
        QVERIFY(plan.setBuckets({QVariantMap{{"id", "b"}, {"name", "Week"}}}));
        QVERIFY(plan.requestAllocation("r", "b", 4.0)); // Conflicts are visible, not rejected.
        const QString revision = plan.revision();
        QVERIFY(!plan.requestAllocation("r", "b", -1));
        QVERIFY(!plan.requestAllocation("missing", "b", 0));
        QVERIFY(!plan.requestAllocation("r", "b", std::numeric_limits<qreal>::infinity()));
        QCOMPARE(plan.revision(), revision);
        QCOMPARE(plan.allocation("r", "b"), 4.0);
        QVERIFY(plan.overAllocated("r", "b"));
        QVERIFY(!plan.setBuckets(QVariantList(257, QVariantMap{{"id", "b"}, {"name", "Week"}})));
    }
};

QTEST_GUILESS_MAIN(TstResourcePlanModel)
#include "tst_resource_plan_model.moc"
