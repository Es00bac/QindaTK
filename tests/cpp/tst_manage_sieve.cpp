// SPDX-License-Identifier: LGPL-3.0-or-later
#include "manage_sieve_client.h"
#include "mail_test_servers.h"
#include <QSignalSpy>
#include <QtTest>
using namespace QindaTK;
namespace {
class SieveServer : public LineServer {
public:
    QMap<QByteArray,QByteArray> scripts{{"OK","keep;"}};
    QByteArray active = "OK";
    bool offerTls = false;
    QByteArray lastScript;
protected:
    void greet(Connection &c) override
    {
        QByteArray caps = "\"IMPLEMENTATION\" \"Test server\"\r\n\"SASL\" \"PLAIN\"\r\n\"SIEVE\" \"fileinto vacation\"\r\n";
        if (offerTls && !encrypted(c)) caps += "\"STARTTLS\"\r\n";
        send(c, caps + "OK\r\n");
    }
    void line(Connection &c, const QByteArray &text) override
    {
        if (c.state == 1) {
            lastScript = text;
            if (!m_putName.isEmpty()) scripts.insert(m_putName, text);
            c.state = 0; send(c,"OK (WARNINGS) \"checked\"\r\n"); return;
        }
        const auto args = arguments(text);
        const auto command = args.value(0).toUpper();
        if (command == "STARTTLS") {
            if (!offerTls || !tlsEnabled()) { send(c,"NO \"TLS unavailable\"\r\n"); return; }
            if (auto *socket = TestTls::ssl(c.socket)) {
                QObject::connect(socket,&QSslSocket::encrypted,socket,[this,&c] { greet(c); });
                upgrade(c,"OK\r\n");
            }
            return;
        }
        if (command == "AUTHENTICATE") {
            const auto fields = QByteArray::fromBase64(args.value(2)).split('\0');
            c.authenticated = fields.size() == 3 && fields[1] == "ada" && fields[2] == "sieve-secret";
            send(c,c.authenticated ? "OK\r\n" : "NO (AUTH-TOOWEAK) \"bad auth\"\r\n"); return;
        }
        if (command == "CAPABILITY") { greet(c); return; }
        if (command == "LOGOUT") { send(c,"OK\r\n"); return; }
        if (!c.authenticated) { send(c,"NO \"authenticate first\"\r\n"); return; }
        const auto name = args.value(1);
        if (command == "LISTSCRIPTS") {
            for(auto it=scripts.cbegin();it!=scripts.cend();++it)
                send(c,'"'+it.key()+'"'+(active==it.key()?" ACTIVE\r\n":"\r\n"));
            send(c,"OK\r\n");
        } else if (command=="GETSCRIPT") {
            if (!scripts.contains(name)) { send(c,"NO (NONEXISTENT) \"no such script\"\r\n"); return; }
            const auto script=scripts.value(name);
            // A real literal, including embedded newlines, not quoted text.
            send(c,'{'+QByteArray::number(script.size())+"}\r\n"+script+"\r\nOK\r\n");
        } else if(command=="PUTSCRIPT"||command=="CHECKSCRIPT") {
            m_putName=command=="PUTSCRIPT"?name:QByteArray(); c.state=1;
        } else if(command=="SETACTIVE") { active=name; send(c,"OK\r\n"); }
        else if(command=="DELETESCRIPT") { scripts.remove(name); send(c,"OK\r\n"); }
        else if(command=="RENAMESCRIPT") { scripts.insert(args.value(2),scripts.take(name)); send(c,"OK\r\n"); }
        else send(c,"NO (UNKNOWN) \"unknown command\"\r\n");
    }
private:
    QByteArray m_putName;
    static QList<QByteArray> arguments(const QByteArray &text)
    {
        QList<QByteArray> out;
        QByteArray word;
        bool quoted=false,escaped=false;
        for(char c:text) {
            if(escaped) {word+=c;escaped=false;continue;}
            if(c=='\\'&&quoted) {escaped=true;continue;}
            if(c=='"') {quoted=!quoted;continue;}
            if(c==' '&&!quoted) {out.append(word);word.clear();} else word+=c;
        }
        out.append(word);return out;
    }
};
void configure(ManageSieveClient &client, quint16 port, const QString &security = "none")
{
    client.setProperty("host","127.0.0.1"); client.setProperty("port",port);
    client.setProperty("security",security); client.setProperty("userName","ada");
    client.setProperty("timeoutMs",5000); client.setSecret("sieve-secret");
}
QVariantMap request(ManageSieveClient &client,const QString &command,const QString &name={},const QByteArray &script={},const QString &newName={})
{
    QSignalSpy spy(&client,&ManageSieveClient::finished);
    client.request("req",command,name,script,newName);
    if(spy.isEmpty()&&!spy.wait(10000))return {};
    return spy.first()[1].toMap();
}
}
class ManageSieveTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { TestTls::trustTestCa(false); }
    void allCommandsLiteralsAndResponseCodes()
    {
        SieveServer server;
        ManageSieveClient client;
        configure(client,server.port());
        auto result=request(client,"capabilities");
        QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("capabilities").toMap().value("SASL").toString(),QStringLiteral("PLAIN"));
        result=request(client,"list");
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("scripts").toList().size(),1);
        QCOMPARE(result.value("scripts").toList().first().toMap().value("name").toString(),QStringLiteral("OK"));
        QVERIFY(request(client,"put","rules","discard;").value("ok").toBool());
        QCOMPARE(server.scripts.value("rules"),QByteArray("discard;"));
        result=request(client,"check",{},"keep;");
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("responseCode").toString(),QStringLiteral("WARNINGS"));
        const QByteArray multiline="require [\"fileinto\"];\r\nfileinto \"Archive\";\r\n";
        server.scripts.insert("rules",multiline);
        result=request(client,"get","rules");
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("script").toByteArray(),multiline);
        QVERIFY(!result.value("transcript").toStringList().join('\n').contains("fileinto \"Archive\""));
        QVERIFY(request(client,"rename","rules",{},"renamed").value("ok").toBool());
        QVERIFY(request(client,"activate","renamed").value("ok").toBool());
        QCOMPARE(server.active,QByteArray("renamed"));
        QVERIFY(request(client,"activate").value("ok").toBool());
        QVERIFY(server.active.isEmpty());
        QVERIFY(request(client,"delete","renamed").value("ok").toBool());
        result=request(client,"get","renamed");
        QVERIFY(!result.value("ok").toBool());
        QCOMPARE(result.value("responseCode").toString(),QStringLiteral("NONEXISTENT"));
        QVERIFY(!result.value("transcript").toStringList().join('\n').contains("sieve-secret"));
        QVERIFY(!request(client,"delete","bad\r\nname").value("ok").toBool());
    }
    void requiredTlsNeverFallsBackToAuthentication()
    {
        SieveServer server;
        ManageSieveClient client;
        configure(client,server.port(),"starttls");
        const auto result=request(client,"list");
        QVERIFY(!result.value("ok").toBool());
        for(const auto &line:server.log) QVERIFY(!line.startsWith("AUTHENTICATE"));
    }
    void startTlsRefreshesCapabilitiesBeforeAuthentication()
    {
        if (!TestTls::available()) QSKIP("No TLS fixture backend");
        TestTls::trustTestCa(true);
        SieveServer server;
        server.offerTls=true;server.enableTls(false);
        ManageSieveClient client;
        configure(client,server.port(),"starttls");
        const auto result=request(client,"list");
        QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));
        QVERIFY(server.encryptedLog.join('\n').contains("AUTHENTICATE"));
        QVERIFY(!result.value("transcript").toStringList().join('\n').contains("sieve-secret"));
    }
};
QTEST_GUILESS_MAIN(ManageSieveTest)
#include "tst_manage_sieve.moc"
