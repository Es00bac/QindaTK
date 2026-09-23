// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_numfmt.h"
#include "workbook_p.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
using namespace Calc;
namespace {
struct Rgb { int r = 0, g = 0, b = 0; };
Rgb rgb(const QString &color)
{
    const uint value = color.mid(1).toUInt(nullptr, 16);
    return {int((value >> 16) & 0xff), int((value >> 8) & 0xff), int(value & 0xff)};
}
QString mix(const QString &from, const QString &to, double t)
{
    const Rgb a = rgb(from), b = rgb(to);
    const auto channel = [t](int x, int y) { return int(std::lround(x + (y - x) * std::clamp(t, 0.0, 1.0))); };
    return QStringLiteral("#%1%2%3").arg(channel(a.r, b.r), 2, 16, QLatin1Char('0'))
        .arg(channel(a.g, b.g), 2, 16, QLatin1Char('0')).arg(channel(a.b, b.b), 2, 16, QLatin1Char('0'));
}
const Area *areaContaining(const std::vector<Area> &areas, int row, int column)
{
    for (const Area &area : areas)
        if (area.contains(row, column)) return &area;
    return nullptr;
}
bool isBlankLike(const Value &v) { return v.isBlank() || (v.isText() && v.text.isEmpty()); }
bool compare(RuleOperator op, const Value &v, const Value &a, const Value &b)
{
    switch (op) {
    case RuleOperator::Between:
    case RuleOperator::NotBetween: {
        const bool ordered = compareValues(a, b) <= 0;
        const Value &lo = ordered ? a : b, &hi = ordered ? b : a;
        const bool inside = compareValues(v, lo) >= 0 && compareValues(v, hi) <= 0;
        return op == RuleOperator::Between ? inside : !inside;
    }
    case RuleOperator::Equal: return compareValues(v, a) == 0;
    case RuleOperator::NotEqual: return compareValues(v, a) != 0;
    case RuleOperator::Greater: return compareValues(v, a) > 0;
    case RuleOperator::Less: return compareValues(v, a) < 0;
    case RuleOperator::GreaterOrEqual: return compareValues(v, a) >= 0;
    case RuleOperator::LessOrEqual: return compareValues(v, a) <= 0;
    }
    return false;
}
} // namespace

CellStyle WorkbookPrivate::explicitStyle(const Sheet &s, int row, int column) const
{
    CellStyle style;
    for (const FormatLayer &layer : s.features->formats)
        if (layer.area.contains(row, column)) style.apply(layer.style);
    return style;
}

QString WorkbookPrivate::displayAt(const Sheet &s, int row, int column, const Value &value) const
{
    if (!value.isNumber()) return displayText(value);
    const auto &formats = s.features->formats;
    for (auto it = formats.rbegin(); it != formats.rend(); ++it)
        if ((it->style.mask & CellStyle::NumberFormat) && it->area.contains(row, column))
            return formatValue(value, it->style.numberFormat);
    return displayText(value);
}

Value WorkbookPrivate::operandValue(const Sheet &s, const RuleOperand &operand, const Area &anchor,
                                    int row, int column, const EvalContext *context) const
{
    if (!operand.formula) return operand.literal;
    const FormulaPtr moved = offsetFormula(operand.formula, row - anchor.firstRow, column - anchor.firstColumn);
    return evaluateFormula(*moved, s.id, context ? *context : *this);
}

// AGENT-NOTE: rules apply in priority order; the first rule that sets a
// field keeps it, stopIfTrue ends evaluation, scales/bars/icons use the
// numeric minimum and maximum of the rule's own ranges (cached per workbook
// revision) and every conditional field wins over the cell's own formats.
QVariantMap WorkbookPrivate::presentation(const Sheet &s, int row, int column, const SheetFeatures *override) const
{
    const SheetFeatures &features = override ? *override : *s.features;
    const Cell *cell = findCell(s, row, column);
    const Value value = cell ? cell->value : Value::blank();
    CellStyle style;
    for (const FormatLayer &layer : features.formats)
        if (layer.area.contains(row, column)) style.apply(layer.style);
    CellStyle conditional;
    QVariantMap extra;
    const auto statistics = [&](const ConditionalRule &rule) {
        const QString key = s.id + QChar(0x1f) + rule.id;
        if (!override)
            if (const auto it = scaleCache.constFind(key); it != scaleCache.cend() && it->revision == revision) return *it;
        ScaleStats stats;
        stats.revision = revision;
        for (const Area &area : rule.areas)
            visitRange(s.id, area.firstRow, area.firstColumn, area.lastRow, area.lastColumn, [&](const Value &v) {
                if (!v.isNumber()) return true;
                stats.minimum = stats.any ? std::min(stats.minimum, v.number) : v.number;
                stats.maximum = stats.any ? std::max(stats.maximum, v.number) : v.number;
                stats.any = true;
                return true;
            });
        if (!override) scaleCache.insert(key, stats);
        return stats;
    };
    for (const ConditionalRule &rule : features.conditional) {
        const Area *hit = areaContaining(rule.areas, row, column);
        if (!hit) continue;
        const Area &anchor = rule.areas.front();
        bool matched = false;
        switch (rule.type) {
        case ConditionalRule::Type::CellValue: {
            const Value v = value.isBlank() ? Value::fromNumber(0.0) : value;
            const Value a = operandValue(s, rule.value1, anchor, row, column);
            const Value b = operandValue(s, rule.value2, anchor, row, column);
            matched = !v.isError() && !a.isError() && !b.isError() && compare(rule.op, v, a, b);
            break;
        }
        case ConditionalRule::Type::Expression: {
            const BoolResult test = toBool(operandValue(s, rule.value1, anchor, row, column));
            matched = test.error == ErrorKind::None && test.value;
            break;
        }
        case ConditionalRule::Type::ContainsText:
            matched = displayText(value).contains(rule.text, Qt::CaseInsensitive);
            break;
        case ConditionalRule::Type::Blank: matched = isBlankLike(value); break;
        case ConditionalRule::Type::NotBlank: matched = !isBlankLike(value); break;
        case ConditionalRule::Type::Errors: matched = value.isError(); break;
        case ConditionalRule::Type::NoErrors: matched = !value.isError(); break;
        case ConditionalRule::Type::ColorScale:
        case ConditionalRule::Type::DataBar:
        case ConditionalRule::Type::IconSet: {
            if (!value.isNumber()) break;
            const ScaleStats stats = statistics(rule);
            if (!stats.any) break;
            const double span = stats.maximum - stats.minimum;
            const double t = span > 0 ? (value.number - stats.minimum) / span : 1.0;
            if (rule.type == ConditionalRule::Type::ColorScale && !(conditional.mask & CellStyle::FillColor)) {
                conditional.fillColor = rule.midColor.isEmpty() ? mix(rule.minColor, rule.maxColor, t)
                    : t < 0.5 ? mix(rule.minColor, rule.midColor, t * 2) : mix(rule.midColor, rule.maxColor, (t - 0.5) * 2);
                conditional.mask |= CellStyle::FillColor;
            } else if (rule.type == ConditionalRule::Type::DataBar && !extra.contains(QStringLiteral("dataBar"))) {
                extra.insert(QStringLiteral("dataBar"), std::clamp(t, 0.0, 1.0));
                extra.insert(QStringLiteral("dataBarColor"), rule.maxColor);
            } else if (rule.type == ConditionalRule::Type::IconSet && !extra.contains(QStringLiteral("icon"))) {
                const bool neutral = span <= 0;
                const int band = neutral ? 1 : t >= 2.0 / 3.0 ? 2 : t >= 1.0 / 3.0 ? 1 : 0;
                extra.insert(QStringLiteral("icon"), QStringList{"arrow-down", "arrow-right", "arrow-up"}.at(band));
                extra.insert(QStringLiteral("iconTone"), QStringList{"negative", "neutral", "positive"}.at(band));
            }
            break;
        }
        }
        if (matched) conditional.fillFrom(rule.style);
        if (matched && rule.stopIfTrue) break;
    }
    style.apply(conditional);
    QVariantMap result = styleToMap(style);
    result.remove(QStringLiteral("numberFormat"));
    for (const char *key : {"bold", "italic", "underline", "strikeout"})
        if (!result.value(QLatin1StringView(key)).toBool()) result.remove(QLatin1StringView(key));
    for (const char *key : {"textColor", "fillColor"})
        if (result.value(QLatin1StringView(key)).toString().isEmpty()) result.remove(QLatin1StringView(key));
    QString align = style.align;
    if (align.isEmpty())
        align = value.isNumber() ? QStringLiteral("right")
              : (value.isBoolean() || value.isError()) ? QStringLiteral("center") : QStringLiteral("left");
    result.insert(QStringLiteral("horizontalAlignment"), align);
    if (value.isError()) result.insert(QStringLiteral("error"), true);
    result.insert(extra);
    return result;
}

QVariantMap Workbook::cellFormat(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s || row < 0 || column < 0 || row >= s->rows || column >= s->columns) return {};
    return styleToMap(d->explicitStyle(*s, row, column));
}
QVariantMap Workbook::cellPresentation(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s || row < 0 || column < 0 || row >= s->rows || column >= s->columns) return {};
    return d->presentation(*s, row, column);
}
QString Workbook::numberFormatProblem(const QString &format) const { return Calc::numberFormatProblem(format); }

} // namespace QindaTK
