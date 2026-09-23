// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diff_model.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>
#include <memory>

using namespace QindaTK;

class HostAdapter final : public DiffTransactionAdapter {
public:
  using DiffTransactionAdapter::DiffTransactionAdapter;
  bool submitDecision(const DiffDecision &decision) override {
    queued.insert(decision.requestId, decision);
    return true;
  }
  void cancelDecision(const QString &requestId) override {
    queued.remove(requestId);
  }
  bool commit(const QString &requestId, QStringList &left) {
    if (!queued.contains(requestId))
      return false;
    const DiffDecision decision = queued.take(requestId);
    for (int i = 0; i < decision.leftCount; ++i)
      left.removeAt(decision.leftStart);
    for (int i = 0; i < decision.replacement.size(); ++i)
      left.insert(decision.leftStart + i, decision.replacement[i]);
    return true;
  }
  QHash<QString, DiffDecision> queued;
};

class LargeProvider final : public DiffPageProvider {
public:
  using DiffPageProvider::DiffPageProvider;
  int regionCount() const override { return 20000; }
  DiffRegion regionAt(int index) const override {
    ++reads;
    const bool changed = index == 15000;
    return {changed ? QStringLiteral("change") : QStringLiteral("equal"),
            changed ? QStringLiteral("before") : QString::number(index),
            changed ? QStringLiteral("after") : QString::number(index),
            changed ? QStringLiteral("stable-large-id") : QString(),
            index + 1,
            index + 1,
            {},
            changed ? QVariant(QVariantMap{{QStringLiteral("value"), 42}})
                    : QVariant{},
            changed ? QStringLiteral("conflict-stable") : QString()};
  }
  int hunkCount() const override { return 1; }
  DiffHunk hunkAt(int index) const override {
    return index == 0 ? DiffHunk{QStringLiteral("stable-large-id"),
                                 15000,
                                 15000,
                                 1,
                                 15000,
                                 1,
                                 QStringLiteral("conflict-stable")}
                      : DiffHunk{};
  }
  QStringList replacementFor(const DiffHunk &,
                             const QString &action) const override {
    return {action == QStringLiteral("accept") ? QStringLiteral("after")
                                               : QStringLiteral("before")};
  }
  mutable int reads = 0;
};

static int countVisibleRegions(QQuickItem *root) {
  int count = root->objectName() == QStringLiteral("diffRegion") ? 1 : 0;
  for (QQuickItem *child : root->childItems())
    count += countVisibleRegions(child);
  return count;
}

class TestDiffModel : public QObject {
  Q_OBJECT
private slots:
  void knownAnswerAndStableIdentity();
  void cancellationAndStaleRevision();
  void repeatedAndEmpty();
  void boundsAndRevisionDiscipline();
  void largeLazyProviderAndLifetime();
};

void TestDiffModel::knownAnswerAndStableIdentity() {
  DiffModel model;
  HostAdapter host;
  model.setTransactionAdapter(&host);
  QStringList left{QStringLiteral("header"), QStringLiteral("old A"),
                   QStringLiteral("bridge"), QStringLiteral("old B"),
                   QStringLiteral("tail")};
  const QStringList right{QStringLiteral("header"), QStringLiteral("new A"),
                          QStringLiteral("bridge"), QStringLiteral("new B"),
                          QStringLiteral("tail")};
  QVERIFY(model.setDocuments(left, right, QStringLiteral("L1"),
                             QStringLiteral("R1")));
  QCOMPARE(model.rowCount(), 5);
  QCOMPARE(model.hunkCount(), 2);
  QCOMPARE(model.get(1).value(QStringLiteral("leftText")).toString(),
           QStringLiteral("old A"));
  QCOMPARE(model.get(1).value(QStringLiteral("rightText")).toString(),
           QStringLiteral("new A"));
  QCOMPARE(model.get(2).value(QStringLiteral("kind")).toString(),
           QStringLiteral("equal"));
  const QString first =
      model.hunks()[0].toMap().value(QStringLiteral("id")).toString();
  const QString second =
      model.hunks()[1].toMap().value(QStringLiteral("id")).toString();
  QVERIFY(first != second);
  const QString request =
      model.requestDecision(first, QStringLiteral("accept"),
                            QStringLiteral("L1"), QStringLiteral("R1"));
  QVERIFY(!request.isEmpty());
  QCOMPARE(left[1], QStringLiteral("old A")); // The provider never edits.
  QCOMPARE(host.queued[request].replacement,
           QStringList{QStringLiteral("new A")});
  QVERIFY(host.commit(request, left));
  QVERIFY(model.setDocuments(left, right, QStringLiteral("L2"),
                             QStringLiteral("R1")));
  QCOMPARE(model.hunkCount(), 1);
  QCOMPARE(model.hunks()[0].toMap().value(QStringLiteral("id")).toString(),
           second);
  QCOMPARE(model.hunkRow(second), 3);
}

void TestDiffModel::cancellationAndStaleRevision() {
  DiffModel model;
  HostAdapter host;
  model.setTransactionAdapter(&host);
  QStringList left{QStringLiteral("keep"), QStringLiteral("before")};
  const QStringList right{QStringLiteral("keep"), QStringLiteral("after")};
  QVERIFY(model.setDocuments(left, right, QStringLiteral("1"),
                             QStringLiteral("2")));
  const QString id =
      model.hunks()[0].toMap().value(QStringLiteral("id")).toString();
  QVERIFY(model
              .requestDecision(id, QStringLiteral("accept"),
                               QStringLiteral("stale"), QStringLiteral("2"))
              .isEmpty());
  QVERIFY(host.queued.isEmpty());
  const QString request = model.requestDecision(
      id, QStringLiteral("accept"), QStringLiteral("1"), QStringLiteral("2"));
  QVERIFY(model.cancelDecision(request));
  QVERIFY(!host.commit(request, left));
  QCOMPARE(left,
           (QStringList{QStringLiteral("keep"), QStringLiteral("before")}));
  QCOMPARE(right[1], QStringLiteral("after"));
  QVERIFY(!model.cancelDecision(request));
  const QString pending = model.requestDecision(
      id, QStringLiteral("accept"), QStringLiteral("1"), QStringLiteral("2"));
  QVERIFY(!pending.isEmpty());
  QVERIFY(model.setDocuments(left, right, QStringLiteral("3"),
                             QStringLiteral("2")));
  QVERIFY(!host.commit(pending, left));
  QVERIFY(model
              .requestDecision(id, QStringLiteral("accept"),
                               QStringLiteral("1"), QStringLiteral("2"))
              .isEmpty());
  QCOMPARE(left[1], QStringLiteral("before"));
}

void TestDiffModel::repeatedAndEmpty() {
  DiffModel model;
  QVERIFY(model.setDocuments({}, {}, QStringLiteral("a"), QStringLiteral("b")));
  QCOMPARE(model.rowCount(), 0);
  QCOMPARE(model.hunkCount(), 0);
  const QStringList left{QStringLiteral("x"), QStringLiteral("repeat"),
                         QStringLiteral("repeat"), QStringLiteral("y")};
  const QStringList right{QStringLiteral("x"), QStringLiteral("repeat"),
                          QStringLiteral("changed"), QStringLiteral("y")};
  QVERIFY(model.setDocuments(left, right, QStringLiteral("c"),
                             QStringLiteral("d")));
  QCOMPARE(model.hunkCount(), 1);
  QCOMPARE(model.get(1).value(QStringLiteral("kind")).toString(),
           QStringLiteral("equal"));
  QCOMPARE(model.get(2).value(QStringLiteral("leftText")).toString(),
           QStringLiteral("repeat"));
  QCOMPARE(model.get(2).value(QStringLiteral("rightText")).toString(),
           QStringLiteral("changed"));
  QVERIFY(model.setDocuments({QStringLiteral("a"), QStringLiteral("c")},
                             {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                             QStringLiteral("e"), QStringLiteral("f")));
  QCOMPARE(model.get(1).value(QStringLiteral("leftLine")).toInt(), 0);
  QCOMPARE(model.get(1).value(QStringLiteral("rightLine")).toInt(), 2);
  QVERIFY(model.setDocuments({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                             {QStringLiteral("a"), QStringLiteral("c")},
                             QStringLiteral("g"), QStringLiteral("h")));
  QCOMPARE(model.get(1).value(QStringLiteral("leftLine")).toInt(), 2);
  QCOMPARE(model.get(1).value(QStringLiteral("rightLine")).toInt(), 0);
}

void TestDiffModel::boundsAndRevisionDiscipline() {
  DiffModel model;
  QVERIFY(model.setDocuments({QStringLiteral("same")}, {QStringLiteral("same")},
                             QStringLiteral("a"), QStringLiteral("b")));
  QVERIFY(!model.setDocuments({QStringLiteral("changed")},
                              {QStringLiteral("same")}, QStringLiteral("a"),
                              QStringLiteral("b")));
  QCOMPARE(model.rowCount(), 1);
  QStringList large;
  for (int i = 0; i < 513; ++i)
    large.append(QString::number(i));
  QVERIFY(
      !model.setDocuments(large, {}, QStringLiteral("c"), QStringLiteral("d")));
  QCOMPARE(model.rowCount(), 1);
}

void TestDiffModel::largeLazyProviderAndLifetime() {
  DiffModel model;
  HostAdapter host;
  model.setTransactionAdapter(&host);
  auto provider = std::make_unique<LargeProvider>();
  QVERIFY(model.setPageProvider(provider.get(), QStringLiteral("L100"),
                                QStringLiteral("R100")));
  QCOMPARE(model.rowCount(), 20000);
  QCOMPARE(provider->reads, 0);
  QCOMPARE(model.get(15000).value(QStringLiteral("rightText")).toString(),
           QStringLiteral("after"));
  QCOMPARE(provider->reads, 1);
  QCOMPARE(model.get(15000)
               .value(QStringLiteral("rightValue"))
               .toMap()
               .value(QStringLiteral("value"))
               .toInt(),
           42);
  QCOMPARE(provider->reads, 2);
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("sourceDiff"),
                                           &model);
  QQmlComponent component(&engine);
  component.setData(
      QByteArrayLiteral(
          "import QtQuick\nimport QindaTK as Tk\n"
          "Tk.DiffView { width: 800; height: 360; diffModel: sourceDiff }"),
      QUrl());
  QScopedPointer<QObject> view(component.create());
  QVERIFY2(view, qPrintable(component.errorString()));
  QQuickWindow window;
  window.resize(800, 360);
  auto *viewItem = qobject_cast<QQuickItem *>(view.data());
  QVERIFY(viewItem);
  viewItem->setParentItem(window.contentItem());
  window.show();
  QTRY_VERIFY(countVisibleRegions(viewItem) > 0);
  QVERIFY(countVisibleRegions(viewItem) < 50);
  QCOMPARE(model.hunkAt(0).value(QStringLiteral("row")).toInt(), 15000);
  QCOMPARE(model.hunkAt(0).value(QStringLiteral("conflictId")).toString(),
           QStringLiteral("conflict-stable"));
  const QString id = model.hunkAt(0).value(QStringLiteral("id")).toString();
  const QString request =
      model.requestDecision(id, QStringLiteral("accept"),
                            QStringLiteral("L100"), QStringLiteral("R100"));
  QVERIFY(!request.isEmpty());
  QCOMPARE(host.queued[request].replacement,
           QStringList{QStringLiteral("after")});
  QVERIFY(model.cancelDecision(request));
  provider.reset();
  QCOMPARE(model.rowCount(), 0);
  QCOMPARE(model.hunkCount(), 0);
  QVERIFY(model
              .requestDecision(id, QStringLiteral("accept"),
                               QStringLiteral("L100"), QStringLiteral("R100"))
              .isEmpty());
}

QTEST_MAIN(TestDiffModel)
#include "tst_diff_model.moc"
