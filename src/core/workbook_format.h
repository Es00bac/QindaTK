// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "format_registry.h"

namespace QindaTK {

// Native workbook provider for FormatRegistry (docs/workbook.md). Import and
// export validate and canonicalize qindatk.workbook version 1 bytes; it
// claims roundtrip only for that format and recognizes no Office, CSV or
// other spreadsheet interchange format.
class WorkbookFormat final : public FormatProvider {
public:
    static constexpr auto FormatId = "qindatk.workbook.v1";
    QVariantMap metadata() const override;
    bool recognizes(const QByteArray &prefix, const QUrl &url) const override;
    FormatPlan preflightImport(const QByteArray &bytes, const QVariantMap &options) const override;
    FormatPayload importDocument(const QByteArray &bytes, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
    FormatPlan preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const override;
    FormatPayload exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
};

} // namespace QindaTK
