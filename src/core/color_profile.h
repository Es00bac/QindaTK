// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

namespace QindaTK {

// Immutable, owned RGB ICC data. IDs are SHA-256 hashes of the exact bytes.
// Parsing validates the container/model; transform construction validates
// the executable tags for the requested direction and intent.
class ColorProfile {
public:
    ColorProfile() = default;
    static ColorProfile fromIcc(const QByteArray &bytes, OperationError *error = nullptr);
    static ColorProfile sRgb();
    static ColorProfile linearSRgb();
    bool isValid() const { return bool(m_data); }
    QByteArray iccBytes() const;
    QString id() const;
    // ICC owns the complete transfer function; never apply a second TRC.
    QString transferId() const;
    QString description() const;
private:
    struct Data;
    std::shared_ptr<const Data> m_data;
};

} // namespace QindaTK
