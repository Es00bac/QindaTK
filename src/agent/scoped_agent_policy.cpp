// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scoped_agent_policy.h"
#include <QJsonArray>
#include <QJsonValue>
#include <cmath>

namespace QindaTK {
namespace {
bool identifier(const QString &s) {
  if (s.isEmpty() || s.size() > 128)
    return false;
  for (const QChar c : s) {
    const auto n = c.unicode();
    if (!((n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') ||
          (n >= '0' && n <= '9') || n == '-' || n == '_' || n == '.' ||
          n == ':'))
      return false;
  }
  return true;
}
bool charge(qsizetype count, qsizetype &remaining) {
  if (count < 0 || count > remaining)
    return false;
  remaining -= count;
  return true;
}
bool stringBudget(const QString &s, qsizetype &remaining) {
  // Six bytes per UTF-16 code unit bounds JSON escaping without first
  // allocating an unbounded serialized copy supplied by a provider.
  return s.size() <= scopedAgentMaximumSnapshotBytes / 6 &&
         charge(s.size() * 6 + 2, remaining);
}
bool jsonBudget(const QJsonValue &v, int depth, int &nodes,
                qsizetype &remaining) {
  if (depth > 16 || --nodes < 0 || !charge(2, remaining))
    return false;
  if (v.isString())
    return stringBudget(v.toString(), remaining);
  if (v.isDouble())
    return std::isfinite(v.toDouble()) && charge(32, remaining);
  if (v.isBool() || v.isNull())
    return charge(5, remaining);
  if (v.isArray()) {
    for (const auto item : v.toArray())
      if (!jsonBudget(item, depth + 1, nodes, remaining))
        return false;
    return true;
  }
  if (v.isObject()) {
    const auto object = v.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
      if (!stringBudget(it.key(), remaining) ||
          !jsonBudget(it.value(), depth + 1, nodes, remaining))
        return false;
    return true;
  }
  return false;
}
} // namespace
bool validScopedAgentPrincipal(const ScopedAgentPrincipal &p) {
  return identifier(p.sessionId) && identifier(p.taskId) &&
         identifier(p.transportId);
}
bool validScopedAgentResource(const ScopedAgentResource &r) {
  return identifier(r.providerId) && identifier(r.resourceId) &&
         r.generation != 0;
}
bool validScopedAgentHandle(const ScopedAgentGrantHandle &h) {
  return identifier(h.hostInstanceId) && identifier(h.id) && h.epoch != 0;
}
bool validScopedAgentRevision(const QString &s) {
  if (s.isEmpty() || s.size() > 256 || s.toUtf8().size() > 256)
    return false;
  for (const auto c : s)
    if (c.isNull() || c.isSurrogate() || c.unicode() < 0x20 ||
        c.unicode() == 0x7f)
      return false;
  return true;
}
bool admitsScopedAgentRead(const ScopedAgentGrant &g,
                           const ScopedAgentPrincipal &p,
                           const ScopedAgentResource &r,
                           const ScopedAgentPrivacy &privacy) {
  return validScopedAgentPrincipal(p) && validScopedAgentResource(r) &&
         validScopedAgentHandle(g.handle) && g.principal == p &&
         g.resource == r &&
         privacy.state == ScopedAgentPrivacyState::Unlocked &&
         privacy.sessionId == p.sessionId && privacy.epoch != 0 &&
         privacy.epoch == g.privacyEpoch && g.issuedAtMs >= 0 &&
         g.expiresAtMs > g.issuedAtMs &&
         g.expiresAtMs - g.issuedAtMs <= scopedAgentMaximumGrantMs &&
         privacy.monotonicMs >= g.issuedAtMs &&
         privacy.monotonicMs < g.expiresAtMs;
}
bool boundedScopedAgentContent(const QJsonObject &content) {
  int nodes = 16384;
  qsizetype remaining = scopedAgentMaximumSnapshotBytes;
  return jsonBudget(content, 0, nodes, remaining);
}
} // namespace QindaTK
