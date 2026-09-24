// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Pure, thread-safe native workbook codec (docs/workbook.md). Not installed.

#include "workbook_p.h"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <optional>

namespace QindaTK::Calc {

inline constexpr auto WorkbookFormatName = "qindatk.workbook";
// Version 2 adds names, sheet features and the optional session header;
// files without them stay version 1.
inline constexpr int WorkbookFormatVersion = 2;
inline constexpr qsizetype MaxExtensionBytes = 16 * 1024 * 1024;

struct DecodedSheet {
    QString id;
    QString name;
    int rows = 0;
    int columns = 0;
    std::vector<CellEntry> cells; // row-major
    FeaturesPtr features = emptyFeatures();
};
struct DecodedWorkbook {
    QString documentId;
    quint64 nextSheetNumber = 1;
    // The session header (DocumentCodec captures: docs/workbook.md#sessions);
    // serialize() writes none.
    bool hasHeader = false;
    quint64 revision = 0;
    quint64 resetGeneration = 0;
    QJsonObject extensions;
    std::vector<DecodedSheet> sheets;
    NamesPtr names = std::make_shared<const NameTable>();
    qint64 cellCount = 0;
    qint64 formulaCount = 0;
};

// Validates everything (structure, limits, names, addresses, formulas with
// their spans) before returning; any problem yields no document.
std::optional<DecodedWorkbook> decodeWorkbook(const QByteArray &bytes, QString *error);
// Deterministic compact UTF-8 JSON: sorted keys, row-major cells, canonical
// formula text and shortest round-trip numbers.
QByteArray encodeWorkbook(const DecodedWorkbook &workbook);

// Version 2 parts (workbook_codec_features.cpp).
QJsonObject encodeSheetFeatures(const SheetFeatures &features, const SheetNameLookup &names);
bool decodeSheetFeatures(const QJsonObject &sheet, const RuleLookups &lookups, FeaturesPtr *features, QString *error);
QJsonArray encodeNames(const NameTable &names, const SheetNameLookup &sheetNames);
bool decodeNames(const QJsonValue &value, const SheetIdLookup &sheetIds, const std::function<QPoint(const QString &)> &bounds,
                 NamesPtr *names, QString *error);
inline constexpr const char *SheetFeatureKeys[] = {"formats", "conditionalFormats", "validations", "merges", "tabColor",
                                                   "hidden", "protected", "frozenRows", "frozenColumns",
                                                   "columnWidths", "rowHeights"};

} // namespace QindaTK::Calc
