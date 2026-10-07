// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scoped_agent_host.h"
#include "scoped_agent_policy.h"
#include <QThread>
#include <QUuid>
#include <limits>
#include <utility>

namespace QindaTK {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
ScopedAgentReadResult refused(ScopedAgentStatus status) {
  return {status, {}, {}};
}
} // namespace
ScopedAgentHost::ScopedAgentHost(ScopedAgentTaskPort &tasks,
                                 ScopedAgentPrivacyPort &privacy,
                                 QObject *parent)
    : QObject(parent), m_tasks(tasks), m_privacy(privacy),
      m_instanceId(freshId()), m_authorityFence(freshId()) {}
bool ScopedAgentHost::ownerThread() const {
  return QThread::currentThread() == thread();
}
bool ScopedAgentHost::addProvider(AgentProvider *p,
                                  ScopedAgentResourcePort *r) {
  if (!ownerThread() || !p || !r || p->thread() != thread() ||
      r->thread() != thread() || m_providers.contains(p->id()) ||
      m_providers.size() >= 128)
    return false;
  m_providers.insert(p->id(), {p, r});
  connect(p, &QObject::destroyed, this, [this, id = p->id()] { withdraw(id); });
  connect(r, &QObject::destroyed, this, [this, id = p->id()] { withdraw(id); });
  return true;
}
bool ScopedAgentHost::validEntry(const Entry &e) const {
  return e.identity && e.resources && e.identity->thread() == thread() &&
         e.resources->thread() == thread();
}
void ScopedAgentHost::withdraw(const QString &providerId) {
  m_authorityFence = freshId();
  m_providers.remove(providerId);
  for (auto it = m_grants.begin(); it != m_grants.end();)
    if (it->resource.providerId == providerId)
      it = m_grants.erase(it);
    else
      ++it;
}
void ScopedAgentHost::refreshAdmission() {
  if (!ownerThread())
    return;
  // Copy IDs so erasing invalid records does not invalidate iteration.
  const auto ids = m_grants.keys();
  for (const auto &id : ids) {
    const auto g = m_grants.value(id);
    if (!admitted(g.principal, g.handle, g.resource))
      m_grants.remove(id);
  }
}
std::optional<ScopedAgentGrant>
ScopedAgentHost::admitted(const ScopedAgentPrincipal &principal,
                          const ScopedAgentGrantHandle &handle,
                          const ScopedAgentResource &resource) {
  if (!ownerThread() || !validScopedAgentPrincipal(principal) ||
      !validScopedAgentResource(resource) || !validScopedAgentHandle(handle) ||
      handle.hostInstanceId != m_instanceId || !m_tasks.isCurrent(principal))
    return {};
  const auto privacy = m_privacy.current();
  const auto it = m_grants.constFind(handle.id);
  if (it == m_grants.constEnd())
    return {};
  const auto g = *it;
  if (g.handle != handle ||
      !admitsScopedAgentRead(g, principal, resource, privacy))
    return {};
  // No provider lookup occurs before current task + exact grant admission.
  return g;
}
ScopedAgentGrantResult
ScopedAgentHost::grantRead(const ScopedAgentPrincipal &principal,
                           const ScopedAgentResource &resource,
                           qint64 lifetimeMs) {
  if (!ownerThread())
    return {ScopedAgentStatus::WrongThread, {}};
  if (!validScopedAgentPrincipal(principal) ||
      !validScopedAgentResource(resource) || lifetimeMs <= 0 ||
      lifetimeMs > scopedAgentMaximumGrantMs)
    return {ScopedAgentStatus::Invalid, {}};
  refreshAdmission();
  if (!m_tasks.isCurrent(principal))
    return {};
  const auto privacy = m_privacy.current();
  if (privacy.state != ScopedAgentPrivacyState::Unlocked ||
      privacy.sessionId != principal.sessionId || privacy.epoch == 0 ||
      privacy.monotonicMs < 0 ||
      privacy.monotonicMs > std::numeric_limits<qint64>::max() - lifetimeMs)
    return {};
  const auto e = m_providers.value(resource.providerId);
  if (!validEntry(e))
    return {};
  const auto fence = m_authorityFence;
  const auto revision = e.resources->currentRevision(resource);
  const auto after = m_privacy.current();
  if (!validEntry(e) || fence != m_authorityFence ||
      !validScopedAgentRevision(revision) || !m_tasks.isCurrent(principal) ||
      after.state != ScopedAgentPrivacyState::Unlocked ||
      after.sessionId != privacy.sessionId || after.epoch != privacy.epoch ||
      after.monotonicMs < privacy.monotonicMs ||
      after.monotonicMs >= privacy.monotonicMs + lifetimeMs)
    return {};
  qsizetype taskCount = 0;
  for (const auto &g : std::as_const(m_grants))
    if (g.principal == principal)
      ++taskCount;
  if (m_grants.size() >= 256 || taskCount >= 64)
    return {ScopedAgentStatus::ResourceLimit, {}};
  ScopedAgentGrant g{{m_instanceId, freshId(), 1},
                     principal,
                     resource,
                     privacy.epoch,
                     privacy.monotonicMs,
                     privacy.monotonicMs + lifetimeMs};
  m_grants.insert(g.handle.id, g);
  return {ScopedAgentStatus::Ready, g.handle};
}
bool ScopedAgentHost::revoke(const ScopedAgentGrantHandle &handle) {
  if (!ownerThread() || handle.hostInstanceId != m_instanceId)
    return false;
  const auto it = m_grants.find(handle.id);
  if (it == m_grants.end() || it->handle != handle)
    return false;
  m_authorityFence = freshId();
  m_grants.erase(it);
  return true;
}
void ScopedAgentHost::revokeAll() {
  if (ownerThread()) {
    m_authorityFence = freshId();
    m_grants.clear();
  }
}
bool ScopedAgentHost::disclosureAllowed(const ScopedAgentPrincipal &principal,
                                        const ScopedAgentGrantHandle &handle,
                                        const ScopedAgentResource &resource,
                                        const QString &revision) {
  if (!validScopedAgentRevision(revision) ||
      !admitted(principal, handle, resource))
    return false;
  const auto e = m_providers.value(resource.providerId);
  if (!validEntry(e))
    return false;
  const auto current = e.resources->currentRevision(resource);
  return validEntry(e) && current == revision &&
         admitted(principal, handle, resource).has_value();
}
ScopedAgentReadResult ScopedAgentHost::read(
    const ScopedAgentPrincipal &principal, const ScopedAgentGrantHandle &handle,
    const ScopedAgentResource &resource, const QString &expectedRevision) {
  if (!ownerThread())
    return refused(ScopedAgentStatus::WrongThread);
  if (!admitted(principal, handle, resource))
    return refused(ScopedAgentStatus::Denied);
  if (!expectedRevision.isEmpty() &&
      !validScopedAgentRevision(expectedRevision))
    return refused(ScopedAgentStatus::Invalid);
  const auto e = m_providers.value(resource.providerId);
  if (!validEntry(e))
    return refused(ScopedAgentStatus::Denied);
  const auto before = e.resources->currentRevision(resource);
  if (!validEntry(e) || !admitted(principal, handle, resource))
    return refused(ScopedAgentStatus::Denied);
  if (!validScopedAgentRevision(before))
    return refused(ScopedAgentStatus::Unavailable);
  if (!expectedRevision.isEmpty() && before != expectedRevision)
    return refused(ScopedAgentStatus::StaleRevision);
  // AGENT-GUARD: only the app's scoped resource port can capture payload.
  // AgentProvider::state() is deliberately never called by this host.
  const auto snapshot = e.resources->capture(resource, before);
  if (!validEntry(e) || !admitted(principal, handle, resource))
    return refused(ScopedAgentStatus::Denied);
  if (snapshot.status != ScopedAgentStatus::Ready)
    return refused(snapshot.status == ScopedAgentStatus::StaleRevision
                       ? ScopedAgentStatus::StaleRevision
                       : ScopedAgentStatus::Unavailable);
  if (snapshot.revision != before ||
      !disclosureAllowed(principal, handle, resource, before))
    return refused(ScopedAgentStatus::StaleRevision);
  if (!boundedScopedAgentContent(snapshot.content))
    return refused(ScopedAgentStatus::ResourceLimit);
  // Resource/authority checks can themselves call application code: check
  // again after validation and require the same exact current revision.
  if (!disclosureAllowed(principal, handle, resource, before))
    return refused(ScopedAgentStatus::Denied);
  return {ScopedAgentStatus::Ready, snapshot.revision, snapshot.content};
}
} // namespace QindaTK
