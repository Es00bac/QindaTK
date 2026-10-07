// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scoped_agent_policy.h"
#include <QJsonArray>
#include <QtTest>
#include <limits>
using namespace QindaTK;
class ScopedAgentPolicyTest : public QObject {
  Q_OBJECT
private slots:
  void copiedFieldsNeverAuthorizeByThemselves() {
    const ScopedAgentPrincipal p{"session", "task", "transport"};
    const ScopedAgentResource r{"provider", "document", 1};
    const ScopedAgentGrant g{{"host", "grant", 1}, p, r, 7, 10, 20};
    ScopedAgentPrivacy privacy{"session", 7, ScopedAgentPrivacyState::Unlocked,
                               10};
    QVERIFY(admitsScopedAgentRead(g, p, r, privacy));
    auto other = p;
    other.taskId = "other";
    QVERIFY(!admitsScopedAgentRead(g, other, r, privacy));
    other = p;
    other.transportId = "other";
    QVERIFY(!admitsScopedAgentRead(g, other, r, privacy));
    other = p;
    other.sessionId = "other";
    QVERIFY(!admitsScopedAgentRead(g, other, r, privacy));
    auto replaced = r;
    ++replaced.generation;
    QVERIFY(!admitsScopedAgentRead(g, p, replaced, privacy));
    replaced = r;
    replaced.resourceId = "second-document";
    QVERIFY(!admitsScopedAgentRead(g, p, replaced, privacy));
    privacy.state = ScopedAgentPrivacyState::Unknown;
    QVERIFY(!admitsScopedAgentRead(g, p, r, privacy));
    privacy.state = ScopedAgentPrivacyState::Blocked;
    QVERIFY(!admitsScopedAgentRead(g, p, r, privacy));
    privacy.state = ScopedAgentPrivacyState::Unlocked;
    ++privacy.epoch;
    QVERIFY(!admitsScopedAgentRead(g, p, r, privacy));
  }
  void expiryClockAndOverflowBoundaries() {
    const ScopedAgentPrincipal p{"s", "t", "u"};
    const ScopedAgentResource r{"p", "r", 1};
    ScopedAgentGrant g{{"h", "g", 1}, p, r, 1, 10, 20};
    ScopedAgentPrivacy privacy{"s", 1, ScopedAgentPrivacyState::Unlocked, 19};
    QVERIFY(admitsScopedAgentRead(g, p, r, privacy));
    for (qint64 now : {qint64(-1), qint64(9), qint64(20), qint64(21)}) {
      privacy.monotonicMs = now;
      QVERIFY(!admitsScopedAgentRead(g, p, r, privacy));
    }
    privacy.monotonicMs = 10;
    g.expiresAtMs = std::numeric_limits<qint64>::max();
    QVERIFY(!admitsScopedAgentRead(g, p, r, privacy));
    g.issuedAtMs = std::numeric_limits<qint64>::min();
    QVERIFY(!admitsScopedAgentRead(g, p, r, privacy));
    g.issuedAtMs = 10;
    g.expiresAtMs = 10;
    QVERIFY(!admitsScopedAgentRead(g, p, r, privacy));
  }
  void inputShapeAndRevisionBounds() {
    QVERIFY(validScopedAgentPrincipal({"s", "t", "channel:1"}));
    QVERIFY(!validScopedAgentPrincipal({"", "t", "u"}));
    QVERIFY(!validScopedAgentPrincipal({"s", QString(129, 'x'), "u"}));
    QVERIFY(!validScopedAgentPrincipal({"s", "t", "../socket/path"}));
    QVERIFY(!validScopedAgentResource({"p", "r", 0}));
    QVERIFY(!validScopedAgentHandle({"h", "g", 0}));
    QVERIFY(!validScopedAgentRevision({}));
    QVERIFY(!validScopedAgentRevision("revision\nspoof"));
    QVERIFY(!validScopedAgentRevision(QString(257, 'r')));
    QVERIFY(validScopedAgentRevision(QString(256, 'r')));
    QVERIFY(validScopedAgentRevision(QString::fromUtf8("version-é")));
  }
  void payloadBoundsBeforeSerialization() {
    QVERIFY(boundedScopedAgentContent({{"text", "permitted document"}}));
    QVERIFY(!boundedScopedAgentContent({{"text", QString(200000, 'x')}}));
    QJsonArray many;
    for (int i = 0; i < 17000; ++i)
      many.append(1);
    QVERIFY(!boundedScopedAgentContent({{"items", many}}));
    QJsonObject nested{{"leaf", true}};
    for (int i = 0; i < 18; ++i)
      nested = {{"child", nested}};
    QVERIFY(!boundedScopedAgentContent(nested));
    QVERIFY(!boundedScopedAgentContent({{QString(200000, 'k'), 1}}));
  }
};
QTEST_GUILESS_MAIN(ScopedAgentPolicyTest)
#include "tst_scoped_agent_policy.moc"
