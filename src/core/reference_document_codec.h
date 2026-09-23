// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "property_document.h"
#include <QJsonObject>
#include <memory>
#include <optional>

namespace QindaTK {

struct DecodedReferenceDocument {
    std::unique_ptr<PropertyDocument> document;
    QJsonObject extensions;
    bool migratedFromV1 = false;
};

// Native reference format only: domain engines own their own schemas and
// serializers. The captured payload and header are immutable worker inputs.
class ReferenceDocumentCodec {
public:
    static constexpr int CurrentVersion = 2;
    static constexpr qsizetype MaximumBytes = 64 * 1024 * 1024;
    static SnapshotPtr capture(const PropertyDocument &document,
                               const QJsonObject &extensions = {},
                               OperationError *error = nullptr);
    static std::optional<DecodedReferenceDocument> decode(
        const QByteArray &bytes, OperationError *error = nullptr);
};

} // namespace QindaTK
