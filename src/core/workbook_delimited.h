// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "format_registry.h"

namespace QindaTK {

// Delimited-text interchange for workbooks (docs/workbook.md#interchange):
// "text.csv" (comma, RFC 4180) and "text.tsv" (tab). Import reads UTF-8 text
// into one sheet of a native workbook (typed numbers and TRUE/FALSE; a field
// starting with '=' stays text unless the option formulas is true). Export
// writes one sheet's displayed values and declares what it loses: formulas
// (their values are written), formats, merged areas, rules, names, sheet
// properties and every other sheet.
class WorkbookDelimitedFormat final : public FormatProvider {
public:
    static constexpr auto CsvId = "text.csv";
    static constexpr auto TsvId = "text.tsv";
    explicit WorkbookDelimitedFormat(QChar delimiter);
    QVariantMap metadata() const override;
    bool recognizes(const QByteArray &prefix, const QUrl &url) const override;
    FormatPlan preflightImport(const QByteArray &bytes, const QVariantMap &options) const override;
    FormatPayload importDocument(const QByteArray &bytes, const QVariantMap &options,
                                 const CancellationToken &cancel, const JobProgress &progress) const override;
    FormatPlan preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const override;
    FormatPayload exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                 const CancellationToken &cancel, const JobProgress &progress) const override;

    // The fields of delimited text, row by row (RFC 4180 quoting); false with
    // a message for an unterminated quote or when a limit is exceeded.
    static bool parse(const QString &text, QChar delimiter, QList<QStringList> *rows, QString *error);
    static QString quoted(const QString &field, QChar delimiter);

private:
    QString formatId() const;
    QChar m_delimiter;
};

} // namespace QindaTK
