// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Pure, thread-safe native workbook codec (docs/workbook.md). Not installed.

#include "workbook_p.h"
#include <QByteArray>
#include <optional>

namespace QindaTK::Calc {

inline constexpr auto WorkbookFormatName = "qindatk.workbook";
inline constexpr int WorkbookFormatVersion = 1;

struct DecodedSheet {
    QString id;
    QString name;
    int rows = 0;
    int columns = 0;
    std::vector<CellEntry> cells; // row-major
};
struct DecodedWorkbook {
    QString documentId;
    quint64 nextSheetNumber = 1;
    std::vector<DecodedSheet> sheets;
    qint64 cellCount = 0;
    qint64 formulaCount = 0;
};

// Validates everything (structure, limits, names, addresses, formulas with
// their spans) before returning; any problem yields no document.
std::optional<DecodedWorkbook> decodeWorkbook(const QByteArray &bytes, QString *error);
// Deterministic compact UTF-8 JSON: sorted keys, row-major cells, canonical
// formula text and shortest round-trip numbers.
QByteArray encodeWorkbook(const DecodedWorkbook &workbook);

} // namespace QindaTK::Calc
