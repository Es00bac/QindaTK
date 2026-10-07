// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_host.h"
#include "scoped_agent_host.h"
#include "scoped_agent_policy.h"
#include <QThread>
#include <QtTest>
#include <functional>
#include <limits>
#include <memory>
using namespace QindaTK;
namespace {
const ScopedAgentPrincipal principal{"session-a", "task-a", "transport-a"};
class Tasks final : public ScopedAgentTaskPort {
public:
  bool live = true;
  bool isCurrent(const ScopedAgentPrincipal &p) const override {
    return live && p == principal;
  }
};
class Privacy final : public ScopedAgentPrivacyPort {
public:
  ScopedAgentPrivacy value{"session-a", 1, ScopedAgentPrivacyState::Unlocked,
                           100};
  ScopedAgentPrivacy current() const override { return value; }
};
class Identity final : public AgentProvider {
public:
  mutable int broadCalls = 0;
  QJsonObject state() const override {
    ++broadCalls;
    return {{"other_document_secret", "must never be captured"}};
  }
  QJsonArray actions() const override {
    ++broadCalls;
    return {};
  }
  QJsonObject execute(const QString &, const QJsonObject &) override {
    ++broadCalls;
    return {};
  }
};
class Resources final : public ScopedAgentResourcePort {
public:
  explicit Resources(const QString &provider) : providerId(provider) {}
  QString providerId;
  quint64 generation = 1;
  int version = 1, captures = 0;
  mutable int lookups = 0;
  bool open = true;
  QJsonObject payload{{"text", "selected document"}};
  mutable std::function<void()> onRevision;
  std::function<void()> onCapture;
  QString currentRevision(const ScopedAgentResource &r) const override {
    ++lookups;
    // Take a callback copy: the test may reset itself during invocation.
    const auto callback = onRevision;
    if (callback)
      callback();
    if (!open || r.providerId != providerId || r.resourceId != "document-a" ||
        r.generation != generation)
      return {};
    return QString("r-%1").arg(version);
  }
  ScopedAgentSnapshot capture(const ScopedAgentResource &r,
                              const QString &expected) override {
    ++captures;
    if (currentRevision(r) != expected)
      return {ScopedAgentStatus::StaleRevision, {}, {}};
    const auto copy = payload;
    const auto callback = onCapture;
    if (callback)
      callback();
    return {ScopedAgentStatus::Ready, expected, copy};
  }
};
struct Fixture {
  Tasks tasks;
  Privacy privacy;
  Identity identity;
  Resources resources{identity.id()};
  ScopedAgentHost host{tasks, privacy};
  ScopedAgentResource resource{identity.id(), "document-a", 1};
  Fixture() { host.addProvider(&identity, &resources); }
  ScopedAgentGrantResult grant(qint64 duration = 1000) {
    return host.grantRead(principal, resource, duration);
  }
};
void verifyEmpty(const ScopedAgentReadResult &result) {
  QVERIFY(result.status != ScopedAgentStatus::Ready);
  QVERIFY(result.revision.isEmpty());
  QVERIFY(result.content.isEmpty());
}
} // namespace
class ScopedAgentHostTest : public QObject {
  Q_OBJECT
private slots:
  void explicitResourceSnapshotNeverCallsBroadV1() {
    Fixture f;
    AgentHost legacy;
    QVERIFY(legacy.addProvider(&f.identity));
    const auto grant = f.grant();
    QCOMPARE(grant.status, ScopedAgentStatus::Ready);
    const auto result = f.host.read(principal, grant.handle, f.resource);
    QCOMPARE(result.status, ScopedAgentStatus::Ready);
    QCOMPARE(result.content.value("text").toString(),
             QString("selected document"));
    QVERIFY(!result.content.contains("other_document_secret"));
    QCOMPARE(f.identity.broadCalls, 0);
    QCOMPARE(legacy.grant(f.identity.id()), AgentHost::Grant::Off);
    QCOMPARE(f.resources.captures, 1);
    auto second = f.resource;
    second.resourceId = "document-b";
    const int lookups = f.resources.lookups;
    verifyEmpty(f.host.read(principal, grant.handle, second));
    QCOMPARE(f.resources.lookups, lookups);
    QCOMPARE(f.host.grantRead(principal, second, 100).status,
             ScopedAgentStatus::Denied);
  }
  void forgedHandlesAndBindingsDoNotReachProvider() {
    Fixture f;
    const auto grant = f.grant();
    const int before = f.resources.lookups;
    auto forged = grant.handle;
    forged.id = "guessed";
    verifyEmpty(f.host.read(principal, forged, f.resource));
    forged = grant.handle;
    ++forged.epoch;
    verifyEmpty(f.host.read(principal, forged, f.resource));
    forged = grant.handle;
    forged.hostInstanceId = "other-host";
    verifyEmpty(f.host.read(principal, forged, f.resource));
    auto p = principal;
    p.taskId = "other-task";
    verifyEmpty(f.host.read(p, grant.handle, f.resource));
    p = principal;
    p.transportId = "reconnected-transport";
    verifyEmpty(f.host.read(p, grant.handle, f.resource));
    p = principal;
    p.sessionId = "other-session";
    verifyEmpty(f.host.read(p, grant.handle, f.resource));
    auto r = f.resource;
    ++r.generation;
    verifyEmpty(f.host.read(principal, grant.handle, r));
    QCOMPARE(f.resources.lookups, before);
    QCOMPARE(f.resources.captures, 0);
  }
  void privacyTaskAndDeadlineGates_data() {
    QTest::addColumn<int>("mode");
    for (int i = 0; i < 8; ++i)
      QTest::newRow(qPrintable(QString::number(i))) << i;
  }
  void privacyTaskAndDeadlineGates() {
    QFETCH(int, mode);
    Fixture f;
    const auto grant = f.grant(10);
    const int before = f.resources.lookups;
    switch (mode) {
    case 0:
      f.privacy.value.state = ScopedAgentPrivacyState::Unknown;
      break;
    case 1:
      f.privacy.value.state = ScopedAgentPrivacyState::Blocked;
      break;
    case 2:
      ++f.privacy.value.epoch;
      break; // Lock/unlock without delivered notification.
    case 3:
      f.privacy.value.sessionId = "other";
      break;
    case 4:
      f.privacy.value.monotonicMs = 110;
      break;
    case 5:
      f.privacy.value.monotonicMs = 99;
      break;
    case 6:
      f.tasks.live = false;
      break;
    case 7:
      f.privacy.value.epoch = 0;
      break;
    }
    verifyEmpty(f.host.read(principal, grant.handle, f.resource));
    QVERIFY(
        !f.host.disclosureAllowed(principal, grant.handle, f.resource, "r-1"));
    QCOMPARE(f.resources.lookups, before);
  }
  void revisionAndGenerationPreventReplacedDocumentDisclosure() {
    Fixture f;
    const auto grant = f.grant();
    ++f.resources.version;
    QCOMPARE(f.host.read(principal, grant.handle, f.resource, "r-1").status,
             ScopedAgentStatus::StaleRevision);
    QCOMPARE(f.resources.captures, 0);
    QCOMPARE(f.host.read(principal, grant.handle, f.resource, "r-2").status,
             ScopedAgentStatus::Ready);
    ++f.resources.generation;
    verifyEmpty(f.host.read(principal, grant.handle, f.resource));
    auto reopened = f.resource;
    ++reopened.generation;
    verifyEmpty(f.host.read(principal, grant.handle, reopened));
    const auto fresh = f.host.grantRead(principal, reopened, 100);
    QCOMPARE(fresh.status, ScopedAgentStatus::Ready);
    QCOMPARE(f.host.read(principal, fresh.handle, reopened).status,
             ScopedAgentStatus::Ready);
  }
  void revokeRegrantNeverRestoresOldHandle() {
    Fixture f;
    const auto old = f.grant();
    QVERIFY(f.host.revoke(old.handle));
    QVERIFY(!f.host.revoke(old.handle));
    const auto fresh = f.grant();
    QVERIFY(fresh.handle != old.handle);
    verifyEmpty(f.host.read(principal, old.handle, f.resource));
    QCOMPARE(f.host.read(principal, fresh.handle, f.resource).status,
             ScopedAgentStatus::Ready);
    f.host.revokeAll();
    verifyEmpty(f.host.read(principal, fresh.handle, f.resource));
  }
  void invalidationDuringCaptureDiscardsCopiedContent_data() {
    QTest::addColumn<int>("mode");
    for (int i = 0; i < 7; ++i)
      QTest::newRow(qPrintable(QString::number(i))) << i;
  }
  void invalidationDuringCaptureDiscardsCopiedContent() {
    QFETCH(int, mode);
    Fixture f;
    const auto grant = f.grant();
    f.resources.onCapture = [&] {
      switch (mode) {
      case 0:
        f.host.revoke(grant.handle);
        break;
      case 1:
        ++f.privacy.value.epoch;
        break;
      case 2:
        f.tasks.live = false;
        break;
      case 3:
        ++f.resources.version;
        break;
      case 4:
        f.resources.open = false;
        break;
      case 5:
        ++f.resources.generation;
        break;
      case 6:
        f.privacy.value.monotonicMs = 1100;
        break;
      }
    };
    verifyEmpty(f.host.read(principal, grant.handle, f.resource));
    QCOMPARE(f.resources.captures, 1);
    QCOMPARE(f.identity.broadCalls, 0);
  }
  void resourceQueryCannotMintGrantAfterRevokeOrExpiry() {
    Fixture f;
    f.resources.onRevision = [&] { f.host.revokeAll(); };
    QCOMPARE(f.grant().status, ScopedAgentStatus::Denied);
    f.resources.onRevision = [&] { f.privacy.value.monotonicMs += 1000; };
    QCOMPARE(f.grant(1).status, ScopedAgentStatus::Denied);
  }
  void finalQueuedDisclosureRechecksAuthorityAndResource() {
    Fixture f;
    const auto grant = f.grant();
    const auto result = f.host.read(principal, grant.handle, f.resource);
    QCOMPARE(result.status, ScopedAgentStatus::Ready);
    QVERIFY(f.host.disclosureAllowed(principal, grant.handle, f.resource,
                                     result.revision));
    ++f.resources.version;
    QVERIFY(!f.host.disclosureAllowed(principal, grant.handle, f.resource,
                                      result.revision));
    --f.resources.version; // Deliberate fixture reset; production revisions
                           // never repeat.
    f.host.revoke(grant.handle);
    QVERIFY(!f.host.disclosureAllowed(principal, grant.handle, f.resource,
                                      result.revision));
  }
  void destructionWithdrawsOnlyAffectedProvider() {
    Fixture f;
    auto identity = std::make_unique<Identity>();
    auto resources = std::make_unique<Resources>(identity->id());
    QVERIFY(f.host.addProvider(identity.get(), resources.get()));
    ScopedAgentResource r{identity->id(), "document-a", 1};
    const auto other = f.host.grantRead(principal, r, 100);
    const auto own = f.grant();
    resources.reset();
    verifyEmpty(f.host.read(principal, other.handle, r));
    QCOMPARE(f.host.read(principal, own.handle, f.resource).status,
             ScopedAgentStatus::Ready);
    auto identity2 = std::make_unique<Identity>();
    Resources resource2(identity2->id());
    QVERIFY(f.host.addProvider(identity2.get(), &resource2));
    r.providerId = identity2->id();
    const auto second = f.host.grantRead(principal, r, 100);
    identity2.reset();
    verifyEmpty(f.host.read(principal, second.handle, r));
  }
  void identityDestructionDuringCaptureDiscardsSnapshot() {
    Tasks tasks;
    Privacy privacy;
    auto identity = std::make_unique<Identity>();
    Resources resources(identity->id());
    ScopedAgentHost host(tasks, privacy);
    QVERIFY(host.addProvider(identity.get(), &resources));
    const ScopedAgentResource resource{identity->id(), "document-a", 1};
    const auto grant = host.grantRead(principal, resource, 100);
    resources.onCapture = [&] { identity.reset(); };
    verifyEmpty(host.read(principal, grant.handle, resource));
    QCOMPARE(resources.captures, 1);
  }
  void movedAdapterIsNeverCalledFromWrongThread() {
    Fixture f;
    const auto grant = f.grant();
    const int lookups = f.resources.lookups;
    QThread worker;
    QObject mover;
    mover.moveToThread(&worker);
    QVERIFY(f.resources.moveToThread(&worker));
    worker.start();
    verifyEmpty(f.host.read(principal, grant.handle, f.resource));
    QCOMPARE(f.resources.lookups, lookups);
    // Restore ownership on the current adapter thread before fixture teardown.
    QThread *owner = QThread::currentThread();
    bool moved = false;
    QMetaObject::invokeMethod(
        &mover,
        [&] {
          moved = f.resources.moveToThread(owner);
          mover.moveToThread(owner);
        },
        Qt::BlockingQueuedConnection);
    worker.quit();
    QVERIFY(worker.wait());
    QVERIFY(moved);
  }
  void limitsDoNotEvictExistingAuthority() {
    Fixture f;
    const auto first = f.grant();
    for (int i = 1; i < 64; ++i)
      QCOMPARE(f.grant().status, ScopedAgentStatus::Ready);
    QCOMPARE(f.grant().status, ScopedAgentStatus::ResourceLimit);
    QCOMPARE(f.host.read(principal, first.handle, f.resource).status,
             ScopedAgentStatus::Ready);
    f.resources.payload = {{"huge", QString(200000, 'x')}};
    QCOMPARE(f.host.read(principal, first.handle, f.resource).status,
             ScopedAgentStatus::ResourceLimit);
    f.host.revokeAll();
    f.privacy.value.monotonicMs = std::numeric_limits<qint64>::max() - 5;
    QCOMPARE(f.grant(10).status, ScopedAgentStatus::Denied);
    QCOMPARE(f.grant(0).status, ScopedAgentStatus::Invalid);
    QCOMPARE(f.grant(scopedAgentMaximumGrantMs + 1).status,
             ScopedAgentStatus::Invalid);
  }
  void refreshPermanentlyDropsInvalidGrant() {
    Fixture f;
    const auto grant = f.grant();
    f.tasks.live = false;
    f.host.refreshAdmission();
    f.tasks.live = true;
    verifyEmpty(f.host.read(principal, grant.handle, f.resource));
    QCOMPARE(f.grant().status, ScopedAgentStatus::Ready);
  }
  void wrongThreadRefusesBeforeAnyPortCall() {
    Fixture f;
    const auto grant = f.grant();
    const int lookups = f.resources.lookups;
    ScopedAgentStatus readStatus = ScopedAgentStatus::Ready;
    ScopedAgentStatus grantStatus = ScopedAgentStatus::Ready;
    bool disclosure = true, revoked = true;
    std::unique_ptr<QThread> worker(QThread::create([&] {
      readStatus = f.host.read(principal, grant.handle, f.resource).status;
      grantStatus = f.grant().status;
      disclosure =
          f.host.disclosureAllowed(principal, grant.handle, f.resource, "r-1");
      revoked = f.host.revoke(grant.handle);
    }));
    worker->start();
    QVERIFY(worker->wait());
    QCOMPARE(readStatus, ScopedAgentStatus::WrongThread);
    QCOMPARE(grantStatus, ScopedAgentStatus::WrongThread);
    QVERIFY(!disclosure);
    QVERIFY(!revoked);
    QCOMPARE(f.resources.lookups, lookups);
    QCOMPARE(f.host.read(principal, grant.handle, f.resource).status,
             ScopedAgentStatus::Ready);
  }
};
QTEST_GUILESS_MAIN(ScopedAgentHostTest)
#include "tst_scoped_agent_host.moc"
