// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <memory>

namespace QindaTK {

struct StateReadResult {
    QByteArray bytes;
    OperationError error;
    explicit operator bool() const { return !error; }
};

// Settings and workspace use separate paths and schemas; this is only their
// small atomic byte-store seam. It never reads or writes document content.
class StateStore {
public:
    virtual ~StateStore() = default;
    virtual StateReadResult read(const QString &path, qsizetype maximumBytes) = 0;
    virtual OperationError write(const QString &path, const QByteArray &bytes) = 0;
};

class AtomicStateStore final : public StateStore {
public:
    StateReadResult read(const QString &path, qsizetype maximumBytes) override;
    OperationError write(const QString &path, const QByteArray &bytes) override;
};

using StateStorePtr = std::shared_ptr<StateStore>;

} // namespace QindaTK
