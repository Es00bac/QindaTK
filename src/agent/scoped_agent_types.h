// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QString>

namespace QindaTK {
// Copied transport-neutral values. Principal is supplied by authenticated
// composition, never decoded from untrusted request parameters as authority.
struct ScopedAgentPrincipal {
  QString sessionId, taskId, transportId;
  friend bool operator==(const ScopedAgentPrincipal &,
                         const ScopedAgentPrincipal &) = default;
};
struct ScopedAgentResource {
  QString providerId, resourceId;
  quint64 generation = 0;
  friend bool operator==(const ScopedAgentResource &,
                         const ScopedAgentResource &) = default;
};
struct ScopedAgentGrantHandle {
  QString hostInstanceId, id;
  quint64 epoch = 0;
  friend bool operator==(const ScopedAgentGrantHandle &,
                         const ScopedAgentGrantHandle &) = default;
};
enum class ScopedAgentPrivacyState { Unknown, Unlocked, Blocked };
struct ScopedAgentPrivacy {
  QString sessionId;
  quint64 epoch = 0;
  ScopedAgentPrivacyState state = ScopedAgentPrivacyState::Unknown;
  qint64 monotonicMs = -1;
};
enum class ScopedAgentStatus {
  Ready,
  Denied,
  Invalid,
  Unavailable,
  StaleRevision,
  ResourceLimit,
  WrongThread
};
struct ScopedAgentGrant {
  ScopedAgentGrantHandle handle;
  ScopedAgentPrincipal principal;
  ScopedAgentResource resource;
  quint64 privacyEpoch = 0;
  qint64 issuedAtMs = -1, expiresAtMs = -1;
  friend bool operator==(const ScopedAgentGrant &,
                         const ScopedAgentGrant &) = default;
};
struct ScopedAgentGrantResult {
  ScopedAgentStatus status = ScopedAgentStatus::Denied;
  ScopedAgentGrantHandle handle;
};
// Resource owner supplies an atomic, purpose-specific projection and its
// opaque revision. Never return an app-wide snapshot filtered after capture.
struct ScopedAgentSnapshot {
  ScopedAgentStatus status = ScopedAgentStatus::Unavailable;
  QString revision;
  QJsonObject content;
};
struct ScopedAgentReadResult {
  ScopedAgentStatus status = ScopedAgentStatus::Denied;
  QString revision;
  QJsonObject content;
};
} // namespace QindaTK
