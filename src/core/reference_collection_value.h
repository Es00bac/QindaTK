// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QJsonArray>
#include <QString>
#include <QVariant>
#include <optional>

namespace QindaTK::ReferenceCollectionValue {

// AGENT-CONTRACT (docs/document-models.md): the native reference codec and
// PropertyCollectionDropTarget share this bounded object-row shape. No
// QObject, pointer or asset data passes through a QVariantList property.
bool unresolvedField(const QString &key);
std::optional<QJsonArray> encode(const QVariant &value);
std::optional<QVariantList> decode(const QJsonArray &array);

} // namespace QindaTK::ReferenceCollectionValue
