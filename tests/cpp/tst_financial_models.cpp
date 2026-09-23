// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ohlc_model.h"
#include "target_model.h"

#include <QtTest>
#include <limits>

using namespace QindaTK;

class TestFinancialModels : public QObject {
  Q_OBJECT
private slots:
  void ohlcKnownSessionAndMissing();
  void ohlcRejectsInvalidWithoutMutation();
  void targetKnownBandsAndMissing();
  void targetRejectsInvalidWithoutMutation();
};

void TestFinancialModels::ohlcKnownSessionAndMissing() {
  OhlcModel model;
  const QVariantMap first{{QStringLiteral("id"), QStringLiteral("mon")},
                          {QStringLiteral("utcMs"), QStringLiteral("1704067200000")},
                          {QStringLiteral("open"), 100}, {QStringLiteral("high"), 110},
                          {QStringLiteral("low"), 95}, {QStringLiteral("close"), 108}};
  const QVariantMap missing{{QStringLiteral("id"), QStringLiteral("holiday")},
                            {QStringLiteral("utcMs"), QStringLiteral("1704153600000")},
                            {QStringLiteral("missing"), true}};
  const QVariantMap third{{QStringLiteral("id"), QStringLiteral("wed")},
                          {QStringLiteral("utcMs"), QStringLiteral("1704240000000")},
                          {QStringLiteral("open"), 108}, {QStringLiteral("high"), 112},
                          {QStringLiteral("low"), 106}, {QStringLiteral("close"), 107}};
  QVERIFY(model.replaceCandles({first, missing, third}, QStringLiteral("USD/share"),
                               QStringLiteral("Host trading-day session"),
                               QStringLiteral("UTC"), 100.0));
  QCOMPARE(model.count(), 3);
  QCOMPARE(model.minimum(), 95.0);
  QCOMPARE(model.maximum(), 112.0);
  QCOMPARE(model.indexOfId(QStringLiteral("holiday")), 1);
  QVERIFY(model.get(1).value(QStringLiteral("missing")).toBool());
  QVERIFY(!model.get(1).value(QStringLiteral("open")).isValid());
  QCOMPARE(model.page(1, 2).size(), 2);
  QCOMPARE(model.page(0, 513).size(), 0);
  QVERIFY(model.csv().contains(QStringLiteral("\"holiday\",\"1704153600000\",,,,,true")));
  QVERIFY(model.replaceCandles({missing, third}, QStringLiteral("USD/share"),
                               QStringLiteral("Host trading-day session"),
                               QStringLiteral("UTC"), QVariant()));
  QCOMPARE(model.indexOfId(QStringLiteral("wed")), 1);
}

void TestFinancialModels::ohlcRejectsInvalidWithoutMutation() {
  OhlcModel model;
  QVariantMap row{{QStringLiteral("id"), QStringLiteral("a")},
                  {QStringLiteral("utcMs"), QStringLiteral("1")},
                  {QStringLiteral("open"), 10}, {QStringLiteral("high"), 12},
                  {QStringLiteral("low"), 8}, {QStringLiteral("close"), 11}};
  QVERIFY(model.replaceCandles({row}, QStringLiteral("USD/share"),
                               QStringLiteral("1 host session"), QStringLiteral("UTC"),
                               QVariant()));
  const auto revision = model.generation();
  row[QStringLiteral("high")] = 9;
  QVERIFY(!model.replaceCandles({row}, QStringLiteral("USD/share"),
                                QStringLiteral("1 host session"), QStringLiteral("UTC"),
                                QVariant()));
  QCOMPARE(model.generation(), revision);
  QCOMPARE(model.get(0).value(QStringLiteral("close")).toDouble(), 11.0);
  row[QStringLiteral("high")] = 12;
  row[QStringLiteral("utcMs")] = QStringLiteral("9007199254740993");
  QVERIFY(model.replaceCandles({row}, QStringLiteral("USD/share"),
                               QStringLiteral("1 host session"), QStringLiteral("UTC"),
                               QVariant()));
  QCOMPARE(model.get(0).value(QStringLiteral("utcMs")).toString(),
           QStringLiteral("9007199254740993"));
  row[QStringLiteral("utcMs")] = QStringLiteral("01");
  QVERIFY(!model.replaceCandles({row}, QStringLiteral("USD/share"),
                                QStringLiteral("1 host session"), QStringLiteral("UTC"),
                                QVariant()));
  row[QStringLiteral("utcMs")] = QStringLiteral("1");
  row[QStringLiteral("open")] = std::numeric_limits<double>::infinity();
  QVERIFY(!model.replaceCandles({row}, QStringLiteral("USD/share"),
                                QStringLiteral("1 host session"), QStringLiteral("UTC"),
                                QVariant()));
  QCOMPARE(model.count(), 1);
}

void TestFinancialModels::targetKnownBandsAndMissing() {
  TargetModel model;
  const QVariantList bands{
      QVariantMap{{QStringLiteral("end"), 40},
                  {QStringLiteral("meaning"), QStringLiteral("Low")}},
      QVariantMap{{QStringLiteral("end"), 70},
                  {QStringLiteral("meaning"), QStringLiteral("Watch")}},
      QVariantMap{{QStringLiteral("end"), 100},
                  {QStringLiteral("meaning"), QStringLiteral("On target")}}};
  QVariantMap row{{QStringLiteral("id"), QStringLiteral("uptime")},
                  {QStringLiteral("label"), QStringLiteral("Uptime")},
                  {QStringLiteral("unit"), QStringLiteral("%")},
                  {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 100},
                  {QStringLiteral("value"), 82}, {QStringLiteral("target"), 95},
                  {QStringLiteral("direction"), QStringLiteral("higher")},
                  {QStringLiteral("thresholdMeaning"), QStringLiteral("Service availability")},
                  {QStringLiteral("bands"), bands}};
  QVariantMap missing = row;
  missing[QStringLiteral("id")] = QStringLiteral("planned");
  missing.remove(QStringLiteral("value"));
  missing[QStringLiteral("missing")] = true;
  QVERIFY(model.replaceTargets({row, missing}));
  QCOMPARE(model.count(), 2);
  QCOMPARE(model.get(0).value(QStringLiteral("value")).toDouble(), 82.0);
  QCOMPARE(model.get(0).value(QStringLiteral("target")).toDouble(), 95.0);
  QCOMPARE(model.get(0).value(QStringLiteral("bands")).toList()[1].toMap()
               .value(QStringLiteral("start")).toDouble(), 40.0);
  QVERIFY(!model.get(1).value(QStringLiteral("value")).isValid());
  QVERIFY(model.csv().contains(QStringLiteral("40–70 Watch")));
  QVERIFY(model.replaceTargets({missing, row}));
  QCOMPARE(model.indexOfId(QStringLiteral("uptime")), 1);
}

void TestFinancialModels::targetRejectsInvalidWithoutMutation() {
  TargetModel model;
  QVariantMap row{{QStringLiteral("id"), QStringLiteral("load")},
                  {QStringLiteral("label"), QStringLiteral("Load")},
                  {QStringLiteral("unit"), QStringLiteral("ms")},
                  {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10},
                  {QStringLiteral("value"), 5}, {QStringLiteral("target"), 3},
                  {QStringLiteral("direction"), QStringLiteral("lower")},
                  {QStringLiteral("thresholdMeaning"), QStringLiteral("Latency budget")},
                  {QStringLiteral("bands"), QVariantList{
                       QVariantMap{{QStringLiteral("end"), 3},
                                   {QStringLiteral("meaning"), QStringLiteral("Within budget")}},
                       QVariantMap{{QStringLiteral("end"), 10},
                                   {QStringLiteral("meaning"), QStringLiteral("Over budget")}}}}};
  QVERIFY(model.replaceTargets({row}));
  const auto revision = model.generation();
  row[QStringLiteral("value")] = 11;
  QVERIFY(!model.replaceTargets({row}));
  QCOMPARE(model.generation(), revision);
  QCOMPARE(model.get(0).value(QStringLiteral("value")).toDouble(), 5.0);
  row[QStringLiteral("value")] = 5;
  row[QStringLiteral("target")] = -1;
  QVERIFY(!model.replaceTargets({row}));
  row[QStringLiteral("target")] = 3;
  row[QStringLiteral("bands")] = QVariantList{
      QVariantMap{{QStringLiteral("end"), 9},
                  {QStringLiteral("meaning"), QStringLiteral("Incomplete")}}};
  QVERIFY(!model.replaceTargets({row}));
  QCOMPARE(model.count(), 1);
}

QTEST_GUILESS_MAIN(TestFinancialModels)
#include "tst_financial_models.moc"
