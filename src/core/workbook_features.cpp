// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_features_p.h"

#include <algorithm>

namespace QindaTK::Calc {
namespace {
// Where a coordinate along the edited axis lands; a deleted one lands on
// the edit position (used to re-anchor rule formulas).
qint32 mapCoordinate(qint32 value, const StructuralEdit &edit)
{
    if (edit.insert) return value >= edit.at ? value + edit.count : value;
    if (value < edit.at) return value;
    return value >= edit.at + edit.count ? value - edit.count : edit.at;
}
bool shiftArea(Area &area, const StructuralEdit &edit, bool *moved)
{
    const Area before = area;
    bool affected = false;
    qint32 &lo = edit.rows ? area.firstRow : area.firstColumn;
    qint32 &hi = edit.rows ? area.lastRow : area.lastColumn;
    if (!shiftInterval(lo, hi, edit, &affected)) { *moved = true; return false; }
    if (!(before == area)) *moved = true;
    return true;
}
template <typename Rule>
bool shiftRuleAreas(Rule &rule, const StructuralEdit &edit, bool *moved)
{
    if (rule.areas.empty()) return false;
    const Area anchor = rule.areas.front();
    qint32 oldRow = anchor.firstRow, oldColumn = anchor.firstColumn;
    (edit.rows ? oldRow : oldColumn) = mapCoordinate(edit.rows ? oldRow : oldColumn, edit);
    std::vector<Area> kept;
    for (Area area : rule.areas)
        if (shiftArea(area, edit, moved)) kept.push_back(area);
    rule.areas = std::move(kept);
    if (rule.areas.empty()) return false;
    const qint32 rowDelta = rule.areas.front().firstRow - oldRow;
    const qint32 columnDelta = rule.areas.front().firstColumn - oldColumn;
    if (rowDelta != 0 || columnDelta != 0)
        for (RuleOperand *operand : {&rule.value1, &rule.value2})
            if (operand->formula) operand->formula = offsetFormula(operand->formula, rowDelta, columnDelta);
    return true;
}
template <typename Rule, typename Rewriter>
void rewriteRuleFormulas(std::vector<Rule> &rules, Rewriter rewrite, bool *changed)
{
    for (Rule &rule : rules)
        for (RuleOperand *operand : {&rule.value1, &rule.value2}) {
            if (!operand->formula) continue;
            const Rewrite rw = rewrite(operand->formula);
            if (rw.changed) { operand->formula = rw.formula; *changed = true; }
        }
}
void shiftSizes(std::map<qint32, double> &sizes, const StructuralEdit &edit, bool *changed)
{
    std::map<qint32, double> moved;
    for (const auto &[index, size] : sizes) {
        const bool deleted = !edit.insert && index >= edit.at && index < edit.at + edit.count;
        const qint32 target = deleted ? -1 : mapCoordinate(index, edit);
        if (target != index) *changed = true;
        if (target >= 0) moved.emplace(target, size);
    }
    sizes = std::move(moved);
}
} // namespace

QString areaText(const Area &area)
{
    const QString first = cellAddress(area.firstRow, area.firstColumn);
    if (area.firstRow == area.lastRow && area.firstColumn == area.lastColumn) return first;
    return first + QLatin1Char(':') + cellAddress(area.lastRow, area.lastColumn);
}
bool parseArea(QStringView text, Area *area)
{
    const qsizetype colon = text.indexOf(u':');
    RefPoint a, b;
    if (!parseCellAddress(colon < 0 ? text : text.left(colon), &a)
        || (colon >= 0 && !parseCellAddress(text.mid(colon + 1), &b))) return false;
    if (colon < 0) b = a;
    if (a.rowAbsolute || a.columnAbsolute || b.rowAbsolute || b.columnAbsolute) return false;
    *area = {std::min(a.row, b.row), std::min(a.column, b.column), std::max(a.row, b.row),
             std::max(a.column, b.column)};
    return true;
}
std::vector<Area> subtractArea(const Area &from, const Area &cut)
{
    if (!from.intersects(cut)) return {from};
    std::vector<Area> pieces;
    if (from.firstRow < cut.firstRow)
        pieces.push_back({from.firstRow, from.firstColumn, cut.firstRow - 1, from.lastColumn});
    if (from.lastRow > cut.lastRow)
        pieces.push_back({cut.lastRow + 1, from.firstColumn, from.lastRow, from.lastColumn});
    const qint32 top = std::max(from.firstRow, cut.firstRow), bottom = std::min(from.lastRow, cut.lastRow);
    if (from.firstColumn < cut.firstColumn)
        pieces.push_back({top, from.firstColumn, bottom, cut.firstColumn - 1});
    if (from.lastColumn > cut.lastColumn)
        pieces.push_back({top, cut.lastColumn + 1, bottom, from.lastColumn});
    return pieces;
}

void CellStyle::apply(const CellStyle &patch)
{
    if (patch.mask & NumberFormat) numberFormat = patch.numberFormat;
    if (patch.mask & Align) align = patch.align;
    if (patch.mask & Bold) bold = patch.bold;
    if (patch.mask & Italic) italic = patch.italic;
    if (patch.mask & Underline) underline = patch.underline;
    if (patch.mask & Strikeout) strikeout = patch.strikeout;
    if (patch.mask & TextColor) textColor = patch.textColor;
    if (patch.mask & FillColor) fillColor = patch.fillColor;
    mask |= patch.mask;
}
void CellStyle::fillFrom(const CellStyle &lower)
{
    CellStyle missing = lower;
    missing.mask = quint16(lower.mask & ~mask);
    apply(missing);
}

bool SheetFeatures::isEmpty() const
{
    return formats.empty() && conditional.empty() && validations.empty() && merges.empty()
        && properties == SheetProperties{};
}
const FeaturesPtr &emptyFeatures()
{
    static const FeaturesPtr empty = std::make_shared<const SheetFeatures>();
    return empty;
}

const NamedRange *findName(const NameTable &table, const QString &name)
{
    const QString key = name.toCaseFolded();
    const auto it = std::lower_bound(table.begin(), table.end(), key,
                                     [](const NamedRange &entry, const QString &k) { return entry.key < k; });
    return it != table.end() && it->key == key ? &*it : nullptr;
}

FeaturesPtr transformFeatures(const FeaturesPtr &features, const QString &hostSheetId,
                              const StructuralEdit &edit, bool *changed)
{
    *changed = false;
    if (!features || features->isEmpty()) return features;
    auto copy = std::make_shared<SheetFeatures>(*features);
    rewriteRuleFormulas(copy->conditional, [&](const FormulaPtr &f) { return rewriteForStructure(f, hostSheetId, edit); }, changed);
    rewriteRuleFormulas(copy->validations, [&](const FormulaPtr &f) { return rewriteForStructure(f, hostSheetId, edit); }, changed);
    if (hostSheetId == edit.sheetId) {
        std::erase_if(copy->formats, [&](FormatLayer &layer) { return !shiftArea(layer.area, edit, changed); });
        std::erase_if(copy->merges, [&](Area &area) {
            return !shiftArea(area, edit, changed) || area.cellCount() < 2;
        });
        std::erase_if(copy->conditional, [&](ConditionalRule &rule) { return !shiftRuleAreas(rule, edit, changed); });
        std::erase_if(copy->validations, [&](ValidationRule &rule) { return !shiftRuleAreas(rule, edit, changed); });
        shiftSizes(edit.rows ? copy->properties.rowHeights : copy->properties.columnWidths, edit, changed);
    }
    return *changed ? FeaturesPtr(copy) : features;
}

FeaturesPtr invalidateFeatureSheet(const FeaturesPtr &features, const QString &removedSheetId, bool *changed)
{
    *changed = false;
    if (!features || !featuresReferToSheet(*features, removedSheetId)) return features;
    auto copy = std::make_shared<SheetFeatures>(*features);
    rewriteRuleFormulas(copy->conditional, [&](const FormulaPtr &f) { return invalidateSheet(f, removedSheetId); }, changed);
    rewriteRuleFormulas(copy->validations, [&](const FormulaPtr &f) { return invalidateSheet(f, removedSheetId); }, changed);
    return copy;
}

NamesPtr transformNames(const NamesPtr &names, const StructuralEdit &edit, bool *changed)
{
    *changed = false;
    if (!names) return names;
    auto copy = std::make_shared<NameTable>(*names);
    for (NamedRange &entry : *copy) {
        if (entry.sheetId != edit.sheetId) continue;
        if (!shiftArea(entry.area, edit, changed)) entry.sheetId.clear();
    }
    return *changed ? NamesPtr(copy) : names;
}

NamesPtr invalidateNameSheet(const NamesPtr &names, const QString &removedSheetId, bool *changed)
{
    *changed = false;
    if (!names) return names;
    auto copy = std::make_shared<NameTable>(*names);
    for (NamedRange &entry : *copy)
        if (entry.sheetId == removedSheetId) { entry.sheetId.clear(); *changed = true; }
    return *changed ? NamesPtr(copy) : names;
}

bool featuresReferToSheet(const SheetFeatures &features, const QString &sheetId)
{
    const auto refers = [&](const auto &rules) {
        return std::any_of(rules.begin(), rules.end(), [&](const auto &rule) {
            return (rule.value1.formula && rule.value1.formula->sheets.contains(sheetId))
                || (rule.value2.formula && rule.value2.formula->sheets.contains(sheetId));
        });
    };
    return refers(features.conditional) || refers(features.validations);
}

} // namespace QindaTK::Calc
