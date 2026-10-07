// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "agent_provider.h"
#include "scoped_agent_types.h"
#include <QHash>
#include <QPointer>
#include <optional>

namespace QindaTK {
// Borrowed same-thread ports must outlive ScopedAgentHost and must not destroy
// it or enter nested event loops. They are trusted composition, NOT proof of
// OS peer authentication. Query methods must be side-effect free: no host,
// resource, task or privacy mutation/reentry. Desktop transport supplies exact
// live task identity; its task ID must change on every task incarnation.
class ScopedAgentTaskPort {
public:
  virtual ~ScopedAgentTaskPort() = default;
  virtual bool isCurrent(const ScopedAgentPrincipal &principal) const = 0;
};
class ScopedAgentPrivacyPort {
public:
  virtual ~ScopedAgentPrivacyPort() = default;
  // epoch changes on lock/unknown/owner/session transitions, even if the host
  // missed intermediate signals. monotonicMs never uses wall-clock time.
  virtual ScopedAgentPrivacy current() const = 0;
};
// App-owned QObject adapter, borrowed alongside its AgentProvider identity.
// It MUST validate exact resource ID/generation and produce only that resource.
// Empty revision means absent/replaced/unavailable. capture() returns an atomic
// snapshot matching expectedRevision or refuses; revisions never repeat within
// a resource generation, and generations never repeat within a provider
// lifetime; it must never call a broad state() then relabel/filter it as an
// authorized narrower resource.
class ScopedAgentResourcePort : public QObject {
public:
  using QObject::QObject;
  virtual QString
  currentRevision(const ScopedAgentResource &resource) const = 0;
  virtual ScopedAgentSnapshot capture(const ScopedAgentResource &resource,
                                      const QString &expectedRevision) = 0;
};

// Public SDK read-only scoped host, additive to cooperative AgentHost v1.
// All calls are synchronous on this QObject's owning thread. Wrong-thread
// calls refuse; callbacks must not destroy the host. Destruction of either
// borrowed provider/resource adapter withdraws grants. No RPC grants access,
// no call changes AgentHost::Grant, and no state/actions/execute fallback
// exists. Ports authenticate externally; a future wire adapter must pin peers
// and call disclosureAllowed immediately before releasing queued bytes. A
// returned copied snapshot cannot recall bytes a caller has already disclosed.
// New public static-library API: consumers rebuild with matching headers.
class ScopedAgentHost final : public QObject {
public:
  ScopedAgentHost(ScopedAgentTaskPort &tasks, ScopedAgentPrivacyPort &privacy,
                  QObject *parent = nullptr);
  bool addProvider(AgentProvider *identityOwner,
                   ScopedAgentResourcePort *resources);
  // Application UI/composition only. One resource, volatile grant, no scope
  // widening; lifetime 1ms..8h. No grant exists until Ready is returned.
  ScopedAgentGrantResult grantRead(const ScopedAgentPrincipal &principal,
                                   const ScopedAgentResource &resource,
                                   qint64 lifetimeMs);
  bool revoke(const ScopedAgentGrantHandle &handle);
  void revokeAll();
  // Composition invokes on task/privacy changes; getters also recheck, so a
  // queued notification or lock/unlock ABA cannot restore old authorization.
  void refreshAdmission();
  ScopedAgentReadResult read(const ScopedAgentPrincipal &principal,
                             const ScopedAgentGrantHandle &handle,
                             const ScopedAgentResource &resource,
                             const QString &expectedRevision = {});
  bool disclosureAllowed(const ScopedAgentPrincipal &principal,
                         const ScopedAgentGrantHandle &handle,
                         const ScopedAgentResource &resource,
                         const QString &revision);
  QString instanceId() const { return m_instanceId; }

private:
  struct Entry {
    QPointer<AgentProvider> identity;
    QPointer<ScopedAgentResourcePort> resources;
  };
  bool ownerThread() const;
  std::optional<ScopedAgentGrant>
  admitted(const ScopedAgentPrincipal &principal,
           const ScopedAgentGrantHandle &handle,
           const ScopedAgentResource &resource);
  bool validEntry(const Entry &entry) const;
  void withdraw(const QString &providerId);
  ScopedAgentTaskPort &m_tasks;
  ScopedAgentPrivacyPort &m_privacy;
  const QString m_instanceId;
  QString m_authorityFence;
  QHash<QString, Entry> m_providers;
  QHash<QString, ScopedAgentGrant> m_grants;
};
} // namespace QindaTK
