// SPDX-License-Identifier: LGPL-3.0-or-later
#include "distribution_model.h"

#include <QtTest>
#include <cmath>
#include <limits>

using namespace QindaTK;

class TestDistributionModel : public QObject {
  Q_OBJECT
private slots:
  void knownQuantilesAndInterval();
  void sourceIdentityAndReorder();
  void validationAndBounds();
  void constantDensity();
};

void TestDistributionModel::knownQuantilesAndInterval() {
  DistributionModel model;
  const QVariantMap interval{
      {QStringLiteral("low"), 1.0},
      {QStringLiteral("high"), 5.0},
      {QStringLiteral("estimate"), 3.0},
      {QStringLiteral("meaning"), QStringLiteral("Observed range")}};
  QVERIFY(model.replaceGroups(
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("g1")},
                   {QStringLiteral("label"), QStringLiteral("Control")},
                   {QStringLiteral("unit"), QStringLiteral("ms")},
                   {QStringLiteral("values"), QVariantList{1, 2, 3, 4, 5}},
                   {QStringLiteral("interval"), interval}}}));
  QCOMPARE(model.count(), 1);
  const QVariantMap group = model.get(0);
  QCOMPARE(group.value(QStringLiteral("minimum")).toDouble(), 1.0);
  QCOMPARE(group.value(QStringLiteral("q1")).toDouble(), 2.0);
  QCOMPARE(group.value(QStringLiteral("median")).toDouble(), 3.0);
  QCOMPARE(group.value(QStringLiteral("q3")).toDouble(), 4.0);
  QCOMPARE(group.value(QStringLiteral("maximum")).toDouble(), 5.0);
  QCOMPARE(group.value(QStringLiteral("mean")).toDouble(), 3.0);
  QVERIFY(std::abs(group.value(QStringLiteral("bandwidth")).toDouble() -
                   0.9735846228506357) < 1e-9);
  QCOMPARE(group.value(QStringLiteral("intervalLow")).toDouble(), 1.0);
  QCOMPARE(group.value(QStringLiteral("intervalHigh")).toDouble(), 5.0);
  QCOMPARE(model.intervalMeaning(), QStringLiteral("Observed range"));
  QCOMPARE(group.value(QStringLiteral("density")).toList().size(), 64);
  QVERIFY(model.minimum() < 1.0);
  QVERIFY(model.maximum() > 5.0);
  QVERIFY(model.summaryCsv().contains(
      QStringLiteral("\"g1\",\"Control\",\"ms\",5,1,2,3,4,5,3,1,5")));
}

void TestDistributionModel::sourceIdentityAndReorder() {
  DistributionModel model;
  QVariantMap alpha{{QStringLiteral("id"), QStringLiteral("alpha")},
                    {QStringLiteral("label"), QStringLiteral("Alpha")},
                    {QStringLiteral("unit"), QStringLiteral("kg")},
                    {QStringLiteral("values"), QVariantList{4, 1, 3, 2}}};
  QVariantMap beta{{QStringLiteral("id"), QStringLiteral("beta")},
                   {QStringLiteral("label"), QStringLiteral("Beta")},
                   {QStringLiteral("unit"), QStringLiteral("kg")},
                   {QStringLiteral("values"), QVariantList{10, 12, 14}}};
  QVERIFY(model.replaceGroups({alpha, beta}));
  QVERIFY(!model.get(0).value(QStringLiteral("intervalLow")).isValid());
  QCOMPARE(model.get(0).value(QStringLiteral("q1")).toDouble(), 1.75);
  QCOMPARE(model.get(0).value(QStringLiteral("median")).toDouble(), 2.5);
  QCOMPARE(model.get(0).value(QStringLiteral("q3")).toDouble(), 3.25);
  QCOMPARE(model.sourceValues(QStringLiteral("alpha"))[0]
               .toMap()
               .value(QStringLiteral("value"))
               .toDouble(),
           4.0);
  QVERIFY(model.replaceGroups({beta, alpha}));
  QCOMPARE(model.indexOfId(QStringLiteral("alpha")), 1);
  QCOMPARE(model.get(1).value(QStringLiteral("groupId")).toString(),
           QStringLiteral("alpha"));
}

void TestDistributionModel::validationAndBounds() {
  DistributionModel model;
  const QVariantMap good{{QStringLiteral("id"), QStringLiteral("good")},
                         {QStringLiteral("label"), QStringLiteral("Good")},
                         {QStringLiteral("unit"), QStringLiteral("s")},
                         {QStringLiteral("values"), QVariantList{1, 2}}};
  QVERIFY(model.replaceGroups({good}));
  QVariantMap bad = good;
  bad[QStringLiteral("id")] = QStringLiteral("bad");
  bad[QStringLiteral("unit")] = QStringLiteral("ms");
  QVERIFY(!model.replaceGroups({good, bad}));
  QCOMPARE(model.count(), 1);
  bad[QStringLiteral("unit")] = QStringLiteral("s");
  bad[QStringLiteral("values")] =
      QVariantList{1, std::numeric_limits<double>::infinity()};
  QVERIFY(!model.replaceGroups({bad}));
  QCOMPARE(model.count(), 1);
  bad[QStringLiteral("values")] = QVariantList{1, 2};
  bad[QStringLiteral("interval")] =
      QVariantMap{{QStringLiteral("low"), 1}, {QStringLiteral("high"), 2}};
  QVERIFY(!model.replaceGroups({bad}));
  QCOMPARE(model.count(), 1);
  bad[QStringLiteral("interval")] = QVariantMap{
      {QStringLiteral("low"), 3},
      {QStringLiteral("high"), 2},
      {QStringLiteral("meaning"), QStringLiteral("Observed range")}};
  QVERIFY(!model.replaceGroups({bad}));
  QCOMPARE(model.count(), 1);
  QVariantList huge;
  for (int i = 0; i < 10001; ++i)
    huge.append(i);
  bad.remove(QStringLiteral("interval"));
  bad[QStringLiteral("values")] = huge;
  QVERIFY(!model.replaceGroups({bad}));
  QCOMPARE(model.count(), 1);
  bad[QStringLiteral("values")] = QVariantList{-1e308, 1e308};
  QVERIFY(!model.replaceGroups({bad}));
  QCOMPARE(model.count(), 1);
  bad[QStringLiteral("values")] = QVariantList{1, 2};
  bad[QStringLiteral("interval")] = QVariantMap{
      {QStringLiteral("low"), -1e308},
      {QStringLiteral("high"), 1e308},
      {QStringLiteral("meaning"), QStringLiteral("Observed range")}};
  QVERIFY(!model.replaceGroups({bad}));
  QCOMPARE(model.count(), 1);
}

void TestDistributionModel::constantDensity() {
  DistributionModel model;
  QVERIFY(model.replaceGroups(
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("constant")},
                   {QStringLiteral("label"), QStringLiteral("Constant")},
                   {QStringLiteral("unit"), QStringLiteral("V")},
                   {QStringLiteral("values"), QVariantList{5, 5, 5}}}}));
  const QVariantList density =
      model.get(0).value(QStringLiteral("density")).toList();
  QCOMPARE(density.size(), 64);
  const double first =
      density.first().toMap().value(QStringLiteral("density")).toDouble();
  const double center =
      density[31].toMap().value(QStringLiteral("density")).toDouble();
  QVERIFY(std::isfinite(center));
  QVERIFY(center > first);
  QVERIFY(model.minimum() < 5.0);
  QVERIFY(model.maximum() > 5.0);
}

QTEST_GUILESS_MAIN(TestDistributionModel)
#include "tst_distribution_model.moc"
