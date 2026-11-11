// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_service.h"
#include <QUrl>
#include <QtTest>
using namespace QindaTK;

class Service : public QObject {
  Q_OBJECT
public:
  int calls = 0;
  Q_INVOKABLE QVariantMap read(qint64 id, bool inactive = false) const {
    return {{"id", id}, {"inactive", inactive}};
  }
  Q_INVOKABLE QVariantMap write(const QVariantMap &map, const QUrl &url) {
    ++calls;
    return {{"map", map}, {"url", url.toString()}};
  }
  Q_INVOKABLE QObject *unsafe() const { return nullptr; }
signals:
  void signalOnly();
};
class AgentServiceTest : public QObject {
  Q_OBJECT
private slots:
  void explicitBoundaryAndDefaults() {
    Service service;
    AgentServiceCatalog catalog;
    QString error;
    QVERIFY(catalog.add(&service, "read", "read", "Read a record", true, {}, &error));
    QVERIFY(!catalog.add(&service, "read", "read", "duplicate", true));
    QVERIFY(!catalog.add(&service, "delete", "deleteLater", "inherited", false));
    QVERIFY(!catalog.add(&service, "signal", "signalOnly", "signal", false));
    QVERIFY(!catalog.add(&service, "unsafe", "unsafe", "pointer", true));
    QVERIFY(!catalog.add(&service, "write", "write", "const lie", true));
    auto result = catalog.invoke("read", {{"id", "9223372036854775807"}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.value.toMap()["id"].toLongLong(), std::numeric_limits<qint64>::max());
    QVERIFY(!result.value.toMap()["inactive"].toBool());
    result = catalog.invoke("read", {{"id", 5}, {"inactive", true}});
    QVERIFY(result.ok);
    QVERIFY(result.value.toMap()["inactive"].toBool());
    QVERIFY(!catalog.invoke("read", {{"id", "9223372036854775808"}}).ok);
    QVERIFY(!catalog.invoke("read", {{"id", 1.5}}).ok);
    QVERIFY(!catalog.invoke("read", {{"id", 5}, {"inactive", "false"}}).ok);
    QVERIFY(!catalog.invoke("read", {{"id", 5}, {"typo", true}}).ok);
    QVERIFY(!catalog.invoke("read", {}).ok);
    QVERIFY(!catalog.invoke("unknown", {}).ok);
    QCOMPARE(service.calls, 0);
  }
  void schemasAndTargetLifetime() {
    AgentServiceCatalog catalog;
    auto *service = new Service;
    const QJsonObject mapSchema{{"type", "object"}, {"required", QJsonArray{"name"}},
        {"properties", QJsonObject{{"name", QJsonObject{{"type", "string"}}}}},
        {"additionalProperties", false}};
    QVERIFY(catalog.add(service, "write", "write", "Save", false, {{"map", mapSchema}}));
    QVERIFY(!catalog.invoke("write", {{"map", QJsonObject{{"typo", "x"}}}, {"url", "file:///tmp/a"}}).ok);
    QCOMPARE(service->calls, 0);
    const auto result = catalog.invoke("write", {{"map", QJsonObject{{"name", "Account"}}},
                                                {"url", "file:///tmp/a"}});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(service->calls, 1);
    QCOMPARE(result.value.toMap()["url"].toString(), "file:///tmp/a");
    QVERIFY(!catalog.isReadOnly("write"));
    delete service;
    QVERIFY(!catalog.actions()[0].toObject()["enabled"].toBool());
    QVERIFY(!catalog.invoke("write", {}).ok);
  }
};
QTEST_GUILESS_MAIN(AgentServiceTest)
#include "tst_agent_service.moc"
