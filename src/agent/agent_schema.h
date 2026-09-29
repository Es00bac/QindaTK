// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
namespace QindaTK {
// Deliberate JSON Schema subset documented in docs/agent-interface.md.
// Unknown keywords reject a schema; they are never silently ignored.
QString validateAgentJson(const QJsonValue &value, const QJsonObject &schema,
                          const QString &path = QStringLiteral("arguments"), int depth = 0);
}
