// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_host.h"
#include "agent_schema.h"
#include "agent_server.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>
using namespace QindaTK;
class SampleProvider : public AgentProvider {
public:
  int value = 1, calls = 0;
  QJsonObject state() const override {
    return {{"title", "Budget"},
            {"type", "fixture"},
            {"quantity", value},
            {"cost", value * 2250 + 5382}};
  }
  QJsonArray actions() const override {
    return {QJsonObject{
        {"id", "set"},
        {"enabled", true},
        {"inputSchema",
         QJsonObject{
             {"type", "object"},
             {"required", QJsonArray{"quantity"}},
             {"additionalProperties", false},
             {"properties",
              QJsonObject{{"quantity", QJsonObject{{"type", "integer"},
                                                   {"minimum", 1},
                                                   {"maximum", 10}}}}}}}}};
  }
  QJsonObject execute(const QString &, const QJsonObject &args) override {
    value = args.value("quantity").toInt();
    ++calls;
    touch();
    return {{"status", "completed"}, {"result", state()}};
  }
};
class AgentTest : public QObject {
  Q_OBJECT
private slots:
  void semanticQueriesRespectReadGrantsAndDoNotUseWriteReceipts() {
    class QueryProvider : public SampleProvider {
    public:
      int reads = 0;
      QJsonArray actions() const override {
        auto result = SampleProvider::actions();
        auto query = result.first().toObject();
        query["id"] = "query";
        query["readOnly"] = true;
        result.append(query);
        query["id"] = "malformed";
        query["readOnly"] = "true";
        result.append(query);
        return result;
      }
      QJsonObject execute(const QString &action, const QJsonObject &args) override {
        if (action != QLatin1String("query"))
          return SampleProvider::execute(action, args);
        ++reads;
        return {{"status", "completed"}, {"result", state()}};
      }
    } provider;
    AgentHost host;
    QVERIFY(host.addProvider(&provider, AgentHost::Grant::Read));
    const auto actions = host.request("actions.list", {{"document_id", provider.id()}})
                             .value("actions").toArray();
    QVERIFY(!actions[0].toObject()["enabled"].toBool());
    QVERIFY(actions[1].toObject()["enabled"].toBool());
    QVERIFY(!actions[2].toObject()["enabled"].toBool());
    QJsonObject request{{"document_id", provider.id()}, {"action", "query"},
                        {"arguments", QJsonObject{{"quantity", 2}}},
                        {"if_revision", provider.revision()}, {"request_id", "read"}};
    for (int i = 0; i < 1100; ++i) {
      const auto result = host.request("action.invoke", request);
      QCOMPARE(result["status"].toString(), "completed");
      QVERIFY(result["read_only"].toBool());
      QVERIFY(!result["replayed"].toBool());
    }
    QCOMPARE(provider.reads, 1100);
    QCOMPARE(provider.calls, 0);
    const auto firstRevision = provider.revision();
    auto invalid = request;
    invalid["arguments"] = QJsonObject{{"quantity", 20}};
    QCOMPARE(host.request("action.invoke", invalid)["status"].toString(), "validation_failed");
    provider.value = 4;
    provider.touch();
    QCOMPARE(host.request("action.invoke", request)["status"].toString(), "stale_revision");
    request["if_revision"] = provider.revision();
    const auto fresh = host.request("action.invoke", request);
    QCOMPARE(fresh["result"].toObject()["quantity"].toInt(), 4);
    QVERIFY(firstRevision != fresh["revision"].toString());
    request["action"] = "malformed";
    QCOMPARE(host.request("action.invoke", request)["status"].toString(), "read_only");
    host.setGrant(provider.id(), AgentHost::Grant::Off);
    request["action"] = "query";
    QCOMPARE(host.request("action.invoke", request)["status"].toString(), "access_denied");
    host.setGrant(provider.id(), AgentHost::Grant::Edit);
    request["action"] = "set";
    request["request_id"] = "write-after-reads";
    QCOMPARE(host.request("action.invoke", request)["status"].toString(), "completed");
    QVERIFY(host.request("action.invoke", request)["replayed"].toBool());
    QCOMPARE(provider.calls, 1);
  }
  void malformedSchemasFailClosed() {
    const QJsonValue value = QJsonObject{{"quantity", 2}};
    QVERIFY(!validateAgentJson(value, {{"type", true}}).isEmpty());
    QVERIFY(
        !validateAgentJson(value, {{"properties", QJsonArray{}}}).isEmpty());
    QVERIFY(!validateAgentJson(
                 value, {{"properties", QJsonObject{{"quantity", false}}}})
                 .isEmpty());
    QVERIFY(!validateAgentJson(value, {{"required", "quantity"}}).isEmpty());
    QVERIFY(!validateAgentJson(value, {{"additionalProperties", 3}}).isEmpty());
    QVERIFY(
        !validateAgentJson(
             value,
             {{"properties",
               QJsonObject{{"absent", QJsonObject{{"unsupported", true}}}}}})
             .isEmpty());
  }
  void actionMayCloseItsProvider() {
    class ClosingProvider : public SampleProvider {
      QJsonObject execute(const QString &, const QJsonObject &) override {
        delete this;
        return {{"status", "completed"}};
      }
    };
    AgentHost host;
    auto *provider = new ClosingProvider;
    host.addProvider(provider, AgentHost::Grant::Edit);
    const auto id = provider->id();
    const auto receipt = host.request(
        "action.invoke", {{"document_id", id},
                          {"action", "set"},
                          {"arguments", QJsonObject{{"quantity", 2}}},
                          {"if_revision", provider->revision()},
                          {"request_id", "close"}});
    QCOMPARE(receipt["status"].toString(), "completed");
    QVERIFY(receipt["document_closed"].toBool());
    QVERIFY(receipt["revision"].isNull());
    QCOMPARE(host.request("document.inspect", {{"document_id", id}})["status"]
                 .toString(),
             "access_denied");
  }
  void multipleProcessesAndMcpBeforeStartup() {
    QTemporaryDir root;
    const auto directory = root.filePath("qindatk-agent");
    QDir().mkpath(directory);
    QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner |
                                         QFile::ExeOwner);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_RUNTIME_DIR", root.path());
    QProcess mcp;
    mcp.setProcessEnvironment(environment);
    mcp.start("python3", {QStringLiteral(QINDATK_AGENT_CLI), "mcp"});
    QVERIFY(mcp.waitForStarted());
    mcp.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}\n{"
              "\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/"
              "call\",\"params\":{\"name\":\"qinda_documents\"}}\n");
    mcp.closeWriteChannel();
    QTRY_COMPARE(mcp.state(), QProcess::NotRunning);
    QCOMPARE(mcp.exitCode(), 0);
    const auto lines = mcp.readAllStandardOutput().trimmed().split('\n');
    QCOMPARE(lines.size(), 2);
    QVERIFY(QJsonDocument::fromJson(lines[1])
                .object()["result"]
                .toObject()["structuredContent"]
                .toObject()["documents"]
                .toArray()
                .isEmpty());
    AgentHost first, second;
    SampleProvider a, b;
    first.addProvider(&a, AgentHost::Grant::Edit);
    second.addProvider(&b, AgentHost::Grant::Edit);
    AgentServer one(&first), two(&second);
    QString error;
    QVERIFY2(one.listen(directory + "/one.sock", &error), qPrintable(error));
    QVERIFY2(two.listen(directory + "/two.sock", &error), qPrintable(error));
    QProcess discover;
    discover.setProcessEnvironment(environment);
    discover.start("python3", {QStringLiteral(QINDATK_AGENT_CLI), "documents"});
    QTRY_COMPARE(discover.state(), QProcess::NotRunning);
    QCOMPARE(discover.exitCode(), 0);
    QCOMPARE(QJsonDocument::fromJson(discover.readAllStandardOutput())
                 .object()["documents"]
                 .toArray()
                 .size(),
             2);
    QProcess edit;
    edit.setProcessEnvironment(environment);
    edit.start("python3", {QStringLiteral(QINDATK_AGENT_CLI), "invoke", b.id(),
                           "set", "--args", "{\"quantity\":3}", "--if-revision",
                           b.revision(), "--request-id", "route"});
    QTRY_COMPARE(edit.state(), QProcess::NotRunning);
    QCOMPARE(edit.exitCode(), 0);
    QCOMPARE(a.value, 1);
    QCOMPARE(b.value, 3);
    QProcess stdinEdit;
    stdinEdit.setProcessEnvironment(environment);
    stdinEdit.start("python3", {QStringLiteral(QINDATK_AGENT_CLI), "invoke", b.id(),
                               "set", "--args-file", "-", "--if-revision", b.revision(),
                               "--request-id", "stdin-input"});
    QVERIFY(stdinEdit.waitForStarted());
    stdinEdit.write("{\"quantity\":4}");
    stdinEdit.closeWriteChannel();
    QTRY_COMPARE(stdinEdit.state(), QProcess::NotRunning);
    QCOMPARE(stdinEdit.exitCode(), 0);
    QCOMPARE(b.value, 4);
  }
  void grantsRevisionsValidationAndRetry() {
    AgentHost host;
    SampleProvider provider;
    QVERIFY(host.addProvider(&provider));
    QCOMPARE(host.request("documents.list").value("documents").toArray().size(),
             0);
    host.setGrant(provider.id(), AgentHost::Grant::Read);
    QJsonObject request{{"document_id", provider.id()},
                        {"request_id", "one"},
                        {"action", "set"},
                        {"if_revision", provider.revision()},
                        {"arguments", QJsonObject{{"quantity", 2}}}};
    QCOMPARE(host.request("action.invoke", request).value("status").toString(),
             "read_only");
    host.setGrant(provider.id(), AgentHost::Grant::Edit);
    auto invalid = request;
    invalid["arguments"] = QJsonObject{{"quantity", -2}};
    QCOMPARE(host.request("action.invoke", invalid).value("status").toString(),
             "validation_failed");
    QCOMPARE(provider.calls, 0);
    auto extra = request;
    extra["arguments"] = QJsonObject{{"quantity", 2}, {"other", true}};
    QCOMPARE(host.request("action.invoke", extra).value("status").toString(),
             "validation_failed");
    const auto first = host.request("action.invoke", request);
    QCOMPARE(first.value("status").toString(), "completed");
    QCOMPARE(provider.value, 2);
    QVERIFY(host.request("action.invoke", request).value("replayed").toBool());
    QCOMPARE(provider.calls, 1);
    auto conflict = request;
    conflict["arguments"] = QJsonObject{{"quantity", 3}};
    QCOMPARE(host.request("action.invoke", conflict).value("status").toString(),
             "request_id_conflict");
    auto stale = request;
    stale["request_id"] = "stale";
    QCOMPARE(host.request("action.invoke", stale).value("status").toString(),
             "stale_revision");
    host.setGrant(provider.id(), AgentHost::Grant::Off);
    QCOMPARE(host.request("action.invoke", request).value("status").toString(),
             "access_denied");
    QVERIFY(host.request("events.poll").value("events").toArray().isEmpty());
  }
  void destructionAndAba() {
    AgentHost host;
    auto *p = new SampleProvider;
    host.addProvider(p, AgentHost::Grant::Read);
    const auto id = p->id();
    const auto before = p->revision();
    p->value = 2;
    p->touch();
    p->value = 1;
    p->touch();
    QVERIFY(before != p->revision());
    delete p;
    QCOMPARE(host.request("document.inspect", {{"document_id", id}})
                 .value("status")
                 .toString(),
             "access_denied");
  }
  void socketAndCli() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile::setPermissions(directory.path(), QFile::ReadOwner |
                                                QFile::WriteOwner |
                                                QFile::ExeOwner);
    AgentHost host;
    SampleProvider provider;
    host.addProvider(&provider, AgentHost::Grant::Edit);
    AgentServer server(&host);
    QString error;
    const QString path = directory.filePath("app.sock");
    QVERIFY2(server.listen(path, &error), qPrintable(error));
    QProcess client;
    client.start("python3", {QStringLiteral(QINDATK_AGENT_CLI), "--socket",
                             path, "inspect", provider.id()});
    QTRY_COMPARE(client.state(), QProcess::NotRunning);
    QCOMPARE(client.exitCode(), 0);
    const auto data =
        QJsonDocument::fromJson(client.readAllStandardOutput()).object();
    QCOMPARE(data.value("quantity").toInt(), 1);
    QVERIFY(!data.value("revision").toString().isEmpty());
    QProcess edit;
    edit.start("python3", {QStringLiteral(QINDATK_AGENT_CLI), "--socket", path,
                           "invoke", provider.id(), "set", "--args",
                           "{\"quantity\":2}", "--if-revision",
                           provider.revision(), "--request-id", "cli-edit"});
    QTRY_COMPARE(edit.state(), QProcess::NotRunning);
    QCOMPARE(edit.exitCode(), 0);
    QCOMPARE(provider.value, 2);
    QProcess mcp;
    mcp.start("python3",
              {QStringLiteral(QINDATK_AGENT_CLI), "--socket", path, "mcp"});
    QVERIFY(mcp.waitForStarted());
    mcp.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}\n{"
              "\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/"
              "call\",\"params\":{\"name\":\"qinda_inspect\",\"arguments\":{"
              "\"document_id\":\"" +
              provider.id().toUtf8() + "\"}}}\n");
    mcp.closeWriteChannel();
    QTRY_COMPARE(mcp.state(), QProcess::NotRunning);
    QCOMPARE(mcp.exitCode(), 0);
    const auto lines = mcp.readAllStandardOutput().trimmed().split('\n');
    QCOMPARE(lines.size(), 2);
    QCOMPARE(QJsonDocument::fromJson(lines[1])
                 .object()["result"]
                 .toObject()["structuredContent"]
                 .toObject()["quantity"]
                 .toInt(),
             2);
    QLocalSocket socket;
    socket.connectToServer(path);
    QVERIFY(socket.waitForConnected());
    socket.write(
        "{\"jsonrpc\":\"2.0\",\"method\":\"action.invoke\",\"params\":{}}\n");
    socket.flush();
    QTest::qWait(25);
    QCOMPARE(provider.calls, 1);
    QCOMPARE(socket.bytesAvailable(), 0);
  }
};
QTEST_GUILESS_MAIN(AgentTest)
#include "tst_agent_host.moc"
