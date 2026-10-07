// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "scoped_agent_types.h"

namespace QindaTK {
inline constexpr qint64 scopedAgentMaximumGrantMs = 8 * 60 * 60 * 1000;
inline constexpr qsizetype scopedAgentMaximumSnapshotBytes = 1024 * 1024;
// Pure value validation: no transport, provider, clock, grant creation or I/O.
// These functions establish structural/current-value policy only. A caller's
// trusted ports must independently establish all supplied authority facts.
bool validScopedAgentPrincipal(const ScopedAgentPrincipal &principal);
bool validScopedAgentResource(const ScopedAgentResource &resource);
bool validScopedAgentHandle(const ScopedAgentGrantHandle &handle);
bool validScopedAgentRevision(const QString &revision);
bool admitsScopedAgentRead(const ScopedAgentGrant &grant,
                           const ScopedAgentPrincipal &principal,
                           const ScopedAgentResource &resource,
                           const ScopedAgentPrivacy &privacy);
// Conservative pre-serialization ceiling, depth 16 and at most 16384 JSON
// values; rejects oversized output whole. It does not redact provider data.
bool boundedScopedAgentContent(const QJsonObject &content);
} // namespace QindaTK
