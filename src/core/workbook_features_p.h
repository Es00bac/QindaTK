// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Private sheet features — cell formats, merged areas, conditional formats,
// data validation and sheet properties — and the workbook name table
// (docs/workbook.md#formats-rules-and-names). Not installed.
//
// AGENT-NOTE: features are immutable once shared. An edit builds a new copy
// and a SetFeatures/SetNames mutation swaps the pointer, so every feature
// change is exactly invertible without per-field undo code.

#include "workbook_formula.h"
#include <QHash>
#include <map>
#include <memory>
#include <vector>

namespace QindaTK::Calc {

inline constexpr int MaxFormatLayers = 4096;   // per sheet
inline constexpr int MaxSheetRules = 256;      // conditional formats or validations per sheet
inline constexpr int MaxRuleAreas = 64;
inline constexpr int MaxMergedAreas = 16384;   // per sheet
inline constexpr int MaxListItems = 1024;
inline constexpr int MaxNames = 4096;
inline constexpr qsizetype MaxRuleText = 1024;  // messages, titles, list items, contains-text

// Inclusive cell rectangle.
struct Area {
    qint32 firstRow = 0, firstColumn = 0, lastRow = 0, lastColumn = 0;
    bool contains(int row, int column) const
    { return row >= firstRow && row <= lastRow && column >= firstColumn && column <= lastColumn; }
    bool contains(const Area &o) const
    { return o.firstRow >= firstRow && o.lastRow <= lastRow && o.firstColumn >= firstColumn && o.lastColumn <= lastColumn; }
    bool intersects(const Area &o) const
    { return !(o.lastRow < firstRow || o.firstRow > lastRow || o.lastColumn < firstColumn || o.firstColumn > lastColumn); }
    qint64 cellCount() const { return qint64(lastRow - firstRow + 1) * (lastColumn - firstColumn + 1); }
    friend bool operator==(const Area &, const Area &) = default;
};
QString areaText(const Area &area);                 // "B2" or "B2:C4"
bool parseArea(QStringView text, Area *area);       // plain A1 or A1:B2
std::vector<Area> subtractArea(const Area &from, const Area &cut); // at most four pieces

// A style patch: only fields in `mask` are set. Colors are "#rrggbb".
struct CellStyle {
    enum Field : quint16 {
        NumberFormat = 1 << 0, Align = 1 << 1, Bold = 1 << 2, Italic = 1 << 3,
        Underline = 1 << 4, Strikeout = 1 << 5, TextColor = 1 << 6, FillColor = 1 << 7
    };
    quint16 mask = 0;
    QString numberFormat;
    QString align;          // left | center | right
    bool bold = false, italic = false, underline = false, strikeout = false;
    QString textColor, fillColor;
    bool isEmpty() const { return mask == 0; }
    // Fields set in `patch` replace ours.
    void apply(const CellStyle &patch);
    // Fields we lack are taken from `lower` (higher-priority rule already applied).
    void fillFrom(const CellStyle &lower);
    friend bool operator==(const CellStyle &, const CellStyle &) = default;
};
struct FormatLayer {
    Area area;
    CellStyle style;
    friend bool operator==(const FormatLayer &, const FormatLayer &) = default;
};

enum class RuleOperator : quint8 { Between, NotBetween, Equal, NotEqual, Greater, Less, GreaterOrEqual, LessOrEqual };

// A rule value: a literal or a formula whose relative references are
// relative to the rule's anchor (the first area's top-left cell).
struct RuleOperand {
    Value literal;
    FormulaPtr formula;
    bool isEmpty() const { return !formula && literal.isBlank(); }
};

struct ConditionalRule {
    enum class Type : quint8 {
        CellValue, Expression, ContainsText, Blank, NotBlank, Errors, NoErrors, ColorScale, DataBar, IconSet
    };
    QString id;
    std::vector<Area> areas;
    Type type = Type::CellValue;
    RuleOperator op = RuleOperator::Greater;
    RuleOperand value1, value2;
    QString text;                          // ContainsText
    CellStyle style;                       // predicate types
    QString minColor, midColor, maxColor;  // ColorScale (midColor optional); DataBar uses maxColor
    bool stopIfTrue = false;
};

struct ValidationRule {
    enum class Type : quint8 { Any, WholeNumber, Decimal, List, TextLength, Custom };
    enum class ErrorStyle : quint8 { Stop, Warning, Information };
    QString id;
    std::vector<Area> areas;
    Type type = Type::Any;
    RuleOperator op = RuleOperator::Between;
    RuleOperand value1, value2;            // bounds; Custom and a List source use value1
    QStringList items;                     // inline List items
    bool allowBlank = true;
    QString inputTitle, inputMessage, errorTitle, errorMessage;
    ErrorStyle errorStyle = ErrorStyle::Stop;
};

struct SheetProperties {
    QString tabColor;
    bool hidden = false;
    bool protectedSheet = false;
    qint32 frozenRows = 0, frozenColumns = 0;
    std::map<qint32, double> columnWidths, rowHeights;
    friend bool operator==(const SheetProperties &, const SheetProperties &) = default;
};

struct SheetFeatures {
    std::vector<FormatLayer> formats;      // later layers win per field
    std::vector<ConditionalRule> conditional; // priority order: first is highest
    std::vector<ValidationRule> validations;  // disjoint areas
    std::vector<Area> merges;              // disjoint, each at least two cells
    SheetProperties properties;
    bool isEmpty() const;
};
using FeaturesPtr = std::shared_ptr<const SheetFeatures>;
const FeaturesPtr &emptyFeatures();

struct NamedRange {
    QString name;
    QString key;       // case-folded name; the table is sorted by it
    QString sheetId;   // empty once its sheet was removed: evaluates to #REF!
    Area area;
};
using NameTable = std::vector<NamedRange>;
using NamesPtr = std::shared_ptr<const NameTable>;
const NamedRange *findName(const NameTable &table, const QString &name);

// Structural edits: areas keep their cells, grow when rows/columns are
// inserted inside them and shrink or disappear when theirs are deleted;
// rule formulas follow the reference rules of rewriteForStructure.
FeaturesPtr transformFeatures(const FeaturesPtr &features, const QString &hostSheetId,
                              const StructuralEdit &edit, bool *changed);
FeaturesPtr invalidateFeatureSheet(const FeaturesPtr &features, const QString &removedSheetId, bool *changed);
NamesPtr transformNames(const NamesPtr &names, const StructuralEdit &edit, bool *changed);
NamesPtr invalidateNameSheet(const NamesPtr &names, const QString &removedSheetId, bool *changed);
// True when any stored formula (cells aside) of these features refers to `sheetId`.
bool featuresReferToSheet(const SheetFeatures &features, const QString &sheetId);

} // namespace QindaTK::Calc
