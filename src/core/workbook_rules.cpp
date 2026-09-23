// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_rules_p.h"
#include "workbook_numfmt.h"

#include <algorithm>

namespace QindaTK::Calc {
namespace {
constexpr int StoredRuleFormulaLength = 8 * MaxFormulaLength;

template <typename Enum>
struct Named { Enum value; const char *name; };
constexpr Named<RuleOperator> operators[] = {
    {RuleOperator::Between, "between"}, {RuleOperator::NotBetween, "notBetween"},
    {RuleOperator::Equal, "equal"}, {RuleOperator::NotEqual, "notEqual"},
    {RuleOperator::Greater, "greater"}, {RuleOperator::Less, "less"},
    {RuleOperator::GreaterOrEqual, "greaterOrEqual"}, {RuleOperator::LessOrEqual, "lessOrEqual"}};
constexpr Named<ConditionalRule::Type> conditionalTypes[] = {
    {ConditionalRule::Type::CellValue, "cellValue"}, {ConditionalRule::Type::Expression, "expression"},
    {ConditionalRule::Type::ContainsText, "containsText"}, {ConditionalRule::Type::Blank, "blank"},
    {ConditionalRule::Type::NotBlank, "notBlank"}, {ConditionalRule::Type::Errors, "errors"},
    {ConditionalRule::Type::NoErrors, "noErrors"}, {ConditionalRule::Type::ColorScale, "colorScale"},
    {ConditionalRule::Type::DataBar, "dataBar"}, {ConditionalRule::Type::IconSet, "iconSet"}};
constexpr Named<ValidationRule::Type> validationTypes[] = {
    {ValidationRule::Type::Any, "any"}, {ValidationRule::Type::WholeNumber, "wholeNumber"},
    {ValidationRule::Type::Decimal, "decimal"}, {ValidationRule::Type::List, "list"},
    {ValidationRule::Type::TextLength, "textLength"}, {ValidationRule::Type::Custom, "custom"}};
constexpr Named<ValidationRule::ErrorStyle> errorStyles[] = {
    {ValidationRule::ErrorStyle::Stop, "stop"}, {ValidationRule::ErrorStyle::Warning, "warning"},
    {ValidationRule::ErrorStyle::Information, "information"}};

template <typename Enum, size_t N>
bool enumFrom(const Named<Enum> (&table)[N], const QVariant &value, Enum *out)
{
    const QString text = value.toString();
    for (const auto &entry : table)
        if (text == QLatin1StringView(entry.name)) { *out = entry.value; return true; }
    return false;
}
template <typename Enum, size_t N>
QString enumName(const Named<Enum> (&table)[N], Enum value)
{
    for (const auto &entry : table)
        if (entry.value == value) return QString::fromLatin1(entry.name);
    return {};
}
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool onlyKeys(const QVariantMap &map, std::initializer_list<const char *> keys, QString *error)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        if (std::none_of(keys.begin(), keys.end(), [&](const char *key) { return it.key() == QLatin1StringView(key); }))
            return fail(error, QStringLiteral("Unknown rule field '%1'").arg(it.key()));
    return true;
}
bool textField(const QVariantMap &map, const char *key, QString *out, QString *error)
{
    const QVariant value = map.value(QLatin1StringView(key));
    if (!value.isValid()) return true;
    if (value.metaType().id() != QMetaType::QString)
        return fail(error, QStringLiteral("'%1' must be text").arg(QLatin1StringView(key)));
    *out = value.toString();
    if (out->size() > MaxRuleText)
        return fail(error, QStringLiteral("'%1' is longer than %2 characters").arg(QLatin1StringView(key)).arg(MaxRuleText));
    return true;
}
bool boolField(const QVariantMap &map, const char *key, bool *out, QString *error)
{
    const QVariant value = map.value(QLatin1StringView(key));
    if (!value.isValid()) return true;
    if (value.metaType().id() != QMetaType::Bool)
        return fail(error, QStringLiteral("'%1' must be true or false").arg(QLatin1StringView(key)));
    *out = value.toBool();
    return true;
}
bool colorField(const QVariantMap &map, const char *key, QString *out, QString *error)
{
    QString text;
    if (!textField(map, key, &text, error)) return false;
    if (text.isEmpty()) return true;
    *out = normalizedColor(text);
    if (out->isEmpty()) return fail(error, QStringLiteral("'%1' must be a #rrggbb color").arg(QLatin1StringView(key)));
    return true;
}
bool operandField(const QVariantMap &map, const char *key, const RuleLookups &lookups, RuleOperand *out, QString *error)
{
    const QVariant value = map.value(QLatin1StringView(key));
    if (!value.isValid()) return true;
    QString text;
    const int type = value.metaType().id();
    if (type == QMetaType::QString) text = value.toString();
    else if (type == QMetaType::Double || type == QMetaType::Int || type == QMetaType::LongLong) text = numberLiteral(value.toDouble());
    else if (type == QMetaType::Bool) text = value.toBool() ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    else return fail(error, QStringLiteral("'%1' must be a value or a formula").arg(QLatin1StringView(key)));
    QString problem;
    if (!operandFromText(text, lookups, out, &problem))
        return fail(error, QStringLiteral("%1: %2").arg(QLatin1StringView(key), problem));
    return true;
}
bool twoValues(RuleOperator op) { return op == RuleOperator::Between || op == RuleOperator::NotBetween; }
bool comparisonOperands(RuleOperator op, const RuleOperand &a, const RuleOperand &b, QString *error)
{
    if (a.isEmpty()) return fail(error, QStringLiteral("value1 is required"));
    if (twoValues(op) && b.isEmpty()) return fail(error, QStringLiteral("value2 is required for this operator"));
    return true;
}
} // namespace

QString operatorName(RuleOperator op) { return enumName(operators, op); }

QString normalizedColor(const QString &text)
{
    QString hex = text.trimmed().toLower();
    if (!hex.startsWith(QLatin1Char('#'))) return {};
    hex = hex.mid(1);
    if (hex.size() == 3) hex = QString(hex[0]) + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
    if (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), [](QChar c) {
            return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); }))
        return {};
    return QLatin1Char('#') + hex;
}

bool styleFromMap(const QVariantMap &map, CellStyle *style, QString *error)
{
    *style = {};
    if (!onlyKeys(map, {"numberFormat", "horizontalAlignment", "bold", "italic", "underline", "strikeout",
                        "textColor", "fillColor"}, error)) return false;
    if (map.contains(QStringLiteral("numberFormat"))) {
        QString format;
        if (!textField(map, "numberFormat", &format, error)) return false;
        if (const QString problem = numberFormatProblem(format); !problem.isEmpty()) return fail(error, problem);
        style->numberFormat = format.compare(QLatin1StringView("General"), Qt::CaseInsensitive) == 0 ? QString() : format;
        style->mask |= CellStyle::NumberFormat;
    }
    if (map.contains(QStringLiteral("horizontalAlignment"))) {
        const QString align = map.value(QStringLiteral("horizontalAlignment")).toString();
        if (!QStringList{"general", "left", "center", "right"}.contains(align))
            return fail(error, QStringLiteral("horizontalAlignment is general, left, center or right"));
        style->align = align == u"general" ? QString() : align;
        style->mask |= CellStyle::Align;
    }
    const std::pair<const char *, std::pair<bool *, CellStyle::Field>> flags[] = {
        {"bold", {&style->bold, CellStyle::Bold}}, {"italic", {&style->italic, CellStyle::Italic}},
        {"underline", {&style->underline, CellStyle::Underline}}, {"strikeout", {&style->strikeout, CellStyle::Strikeout}}};
    for (const auto &[key, target] : flags) {
        if (!map.contains(QLatin1StringView(key))) continue;
        if (!boolField(map, key, target.first, error)) return false;
        style->mask |= target.second;
    }
    for (const auto &[key, target] : {std::pair{"textColor", std::pair{&style->textColor, CellStyle::TextColor}},
                                      std::pair{"fillColor", std::pair{&style->fillColor, CellStyle::FillColor}}}) {
        if (!map.contains(QLatin1StringView(key))) continue;
        if (!colorField(map, key, target.first, error)) return false;
        style->mask |= target.second;
    }
    return true;
}

QVariantMap styleToMap(const CellStyle &style)
{
    QVariantMap map;
    if (style.mask & CellStyle::NumberFormat)
        map.insert(QStringLiteral("numberFormat"), style.numberFormat.isEmpty() ? QStringLiteral("General") : style.numberFormat);
    if (style.mask & CellStyle::Align)
        map.insert(QStringLiteral("horizontalAlignment"), style.align.isEmpty() ? QStringLiteral("general") : style.align);
    if (style.mask & CellStyle::Bold) map.insert(QStringLiteral("bold"), style.bold);
    if (style.mask & CellStyle::Italic) map.insert(QStringLiteral("italic"), style.italic);
    if (style.mask & CellStyle::Underline) map.insert(QStringLiteral("underline"), style.underline);
    if (style.mask & CellStyle::Strikeout) map.insert(QStringLiteral("strikeout"), style.strikeout);
    if (style.mask & CellStyle::TextColor) map.insert(QStringLiteral("textColor"), style.textColor);
    if (style.mask & CellStyle::FillColor) map.insert(QStringLiteral("fillColor"), style.fillColor);
    return map;
}

bool areasFromValue(const QVariant &value, const RuleLookups &lookups, std::vector<Area> *areas, QString *error)
{
    areas->clear();
    const QStringList texts = value.metaType().id() == QMetaType::QString
        ? value.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts) : value.toStringList();
    if (texts.isEmpty()) return fail(error, QStringLiteral("A rule needs at least one range"));
    if (texts.size() > MaxRuleAreas) return fail(error, QStringLiteral("A rule has at most %1 ranges").arg(MaxRuleAreas));
    for (const QString &text : texts) {
        Area area;
        if (!parseArea(text.trimmed(), &area)) return fail(error, QStringLiteral("'%1' is not a range such as A1:B5").arg(text));
        if (area.lastRow >= lookups.rows || area.lastColumn >= lookups.columns)
            return fail(error, QStringLiteral("Range %1 is outside the sheet").arg(text));
        areas->push_back(area);
    }
    return true;
}
QStringList areasToList(const std::vector<Area> &areas)
{
    QStringList list;
    for (const Area &area : areas) list.append(areaText(area));
    return list;
}

bool operandFromText(const QString &text, const RuleLookups &lookups, RuleOperand *operand, QString *error)
{
    *operand = {};
    if (text.isEmpty()) return true;
    if (text.startsWith(QLatin1Char('='))) {
        const ParseResult parsed = parseFormula(text, lookups.sheetIdForName, StoredRuleFormulaLength, lookups.names);
        if (!parsed.ok()) return fail(error, parsed.error.message);
        operand->formula = parsed.formula;
        return true;
    }
    double number = 0.0;
    if (text.startsWith(QLatin1Char('\''))) operand->literal = Value::fromText(text.mid(1));
    else if (text.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
             || text.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0)
        operand->literal = Value::fromBool(text.size() == 4);
    else if (parseNumber(text, &number)) operand->literal = Value::fromNumber(number);
    else operand->literal = Value::fromText(text);
    if (operand->literal.isText() && operand->literal.text.size() > MaxRuleText)
        return fail(error, QStringLiteral("A rule value is longer than %1 characters").arg(MaxRuleText));
    return true;
}
QString operandText(const RuleOperand &operand, const SheetNameLookup &names)
{
    if (operand.formula) return renderFormula(*operand.formula, names);
    const Value &v = operand.literal;
    if (v.isNumber()) return numberLiteral(v.number);
    if (!v.isText()) return displayText(v);
    double ignored = 0.0;
    const bool escape = v.text.startsWith(QLatin1Char('=')) || v.text.startsWith(QLatin1Char('\''))
        || v.text.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
        || v.text.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0 || parseNumber(v.text, &ignored);
    return escape ? QLatin1Char('\'') + v.text : v.text;
}

bool conditionalFromMap(const QVariantMap &map, const RuleLookups &lookups, ConditionalRule *rule, QString *error)
{
    *rule = {};
    if (!onlyKeys(map, {"id", "ranges", "type", "operator", "value1", "value2", "text", "style", "minColor",
                        "midColor", "maxColor", "stopIfTrue"}, error)
        || !textField(map, "id", &rule->id, error)
        || !areasFromValue(map.value(QStringLiteral("ranges")), lookups, &rule->areas, error)) return false;
    if (!enumFrom(conditionalTypes, map.value(QStringLiteral("type")), &rule->type))
        return fail(error, QStringLiteral("Unknown conditional format type '%1'").arg(map.value(QStringLiteral("type")).toString()));
    if (map.contains(QStringLiteral("operator")) && !enumFrom(operators, map.value(QStringLiteral("operator")), &rule->op))
        return fail(error, QStringLiteral("Unknown operator '%1'").arg(map.value(QStringLiteral("operator")).toString()));
    if (!operandField(map, "value1", lookups, &rule->value1, error)
        || !operandField(map, "value2", lookups, &rule->value2, error)
        || !textField(map, "text", &rule->text, error) || !boolField(map, "stopIfTrue", &rule->stopIfTrue, error)
        || !colorField(map, "minColor", &rule->minColor, error) || !colorField(map, "midColor", &rule->midColor, error)
        || !colorField(map, "maxColor", &rule->maxColor, error)
        || !styleFromMap(map.value(QStringLiteral("style")).toMap(), &rule->style, error)) return false;
    switch (rule->type) {
    case ConditionalRule::Type::CellValue:
        if (!comparisonOperands(rule->op, rule->value1, rule->value2, error)) return false;
        break;
    case ConditionalRule::Type::Expression:
        if (!rule->value1.formula) return fail(error, QStringLiteral("An expression rule needs a formula in value1"));
        break;
    case ConditionalRule::Type::ContainsText:
        if (rule->text.isEmpty()) return fail(error, QStringLiteral("A contains-text rule needs text"));
        break;
    case ConditionalRule::Type::ColorScale:
        if (rule->minColor.isEmpty()) rule->minColor = QStringLiteral("#f8696b");
        if (rule->maxColor.isEmpty()) rule->maxColor = QStringLiteral("#63be7b");
        return true;
    case ConditionalRule::Type::DataBar:
        if (rule->maxColor.isEmpty()) rule->maxColor = QStringLiteral("#638ec6");
        return true;
    case ConditionalRule::Type::IconSet: return true;
    default: break;
    }
    if (rule->style.isEmpty()) return fail(error, QStringLiteral("A rule needs a style to apply"));
    return true;
}

QVariantMap conditionalToMap(const ConditionalRule &rule, const SheetNameLookup &names, bool compact)
{
    QVariantMap map{{"id", rule.id}, {"ranges", areasToList(rule.areas)},
                    {"type", enumName(conditionalTypes, rule.type)}};
    const auto put = [&](const char *key, const QVariant &value, bool isDefault) {
        if (!compact || !isDefault) map.insert(QLatin1StringView(key), value);
    };
    const bool compares = rule.type == ConditionalRule::Type::CellValue;
    put("operator", operatorName(rule.op), !compares);
    put("value1", operandText(rule.value1, names), rule.value1.isEmpty());
    put("value2", operandText(rule.value2, names), rule.value2.isEmpty());
    put("text", rule.text, rule.text.isEmpty());
    put("style", styleToMap(rule.style), rule.style.isEmpty());
    put("minColor", rule.minColor, rule.minColor.isEmpty());
    put("midColor", rule.midColor, rule.midColor.isEmpty());
    put("maxColor", rule.maxColor, rule.maxColor.isEmpty());
    put("stopIfTrue", rule.stopIfTrue, !rule.stopIfTrue);
    return map;
}

bool validationFromMap(const QVariantMap &map, const RuleLookups &lookups, ValidationRule *rule, QString *error)
{
    *rule = {};
    if (!onlyKeys(map, {"id", "ranges", "type", "operator", "value1", "value2", "items", "allowBlank", "inputTitle",
                        "inputMessage", "errorStyle", "errorTitle", "errorMessage"}, error)
        || !textField(map, "id", &rule->id, error)
        || !areasFromValue(map.value(QStringLiteral("ranges")), lookups, &rule->areas, error)) return false;
    if (!enumFrom(validationTypes, map.value(QStringLiteral("type")), &rule->type))
        return fail(error, QStringLiteral("Unknown validation type '%1'").arg(map.value(QStringLiteral("type")).toString()));
    if (map.contains(QStringLiteral("operator")) && !enumFrom(operators, map.value(QStringLiteral("operator")), &rule->op))
        return fail(error, QStringLiteral("Unknown operator '%1'").arg(map.value(QStringLiteral("operator")).toString()));
    if (map.contains(QStringLiteral("errorStyle"))
        && !enumFrom(errorStyles, map.value(QStringLiteral("errorStyle")), &rule->errorStyle))
        return fail(error, QStringLiteral("errorStyle is stop, warning or information"));
    if (!operandField(map, "value1", lookups, &rule->value1, error)
        || !operandField(map, "value2", lookups, &rule->value2, error)
        || !boolField(map, "allowBlank", &rule->allowBlank, error)
        || !textField(map, "inputTitle", &rule->inputTitle, error)
        || !textField(map, "inputMessage", &rule->inputMessage, error)
        || !textField(map, "errorTitle", &rule->errorTitle, error)
        || !textField(map, "errorMessage", &rule->errorMessage, error)) return false;
    rule->items = map.value(QStringLiteral("items")).toStringList();
    if (rule->items.size() > MaxListItems) return fail(error, QStringLiteral("A list has at most %1 items").arg(MaxListItems));
    for (const QString &item : std::as_const(rule->items))
        if (item.isEmpty() || item.size() > MaxRuleText) return fail(error, QStringLiteral("List items are 1 to %1 characters").arg(MaxRuleText));
    switch (rule->type) {
    case ValidationRule::Type::WholeNumber:
    case ValidationRule::Type::Decimal:
    case ValidationRule::Type::TextLength:
        if (!comparisonOperands(rule->op, rule->value1, rule->value2, error)) return false;
        for (const RuleOperand *operand : {&rule->value1, &rule->value2})
            if (!operand->formula && !operand->isEmpty() && !operand->literal.isNumber())
                return fail(error, QStringLiteral("Bounds must be numbers or formulas"));
        break;
    case ValidationRule::Type::List:
        if (rule->items.isEmpty() && !rule->value1.formula)
            return fail(error, QStringLiteral("A list needs items or a source range formula in value1"));
        break;
    case ValidationRule::Type::Custom:
        if (!rule->value1.formula) return fail(error, QStringLiteral("A custom rule needs a formula in value1"));
        break;
    case ValidationRule::Type::Any: break;
    }
    return true;
}

QVariantMap validationToMap(const ValidationRule &rule, const SheetNameLookup &names, bool compact)
{
    QVariantMap map{{"id", rule.id}, {"ranges", areasToList(rule.areas)},
                    {"type", enumName(validationTypes, rule.type)}};
    const auto put = [&](const char *key, const QVariant &value, bool isDefault) {
        if (!compact || !isDefault) map.insert(QLatin1StringView(key), value);
    };
    const bool bounded = rule.type == ValidationRule::Type::WholeNumber || rule.type == ValidationRule::Type::Decimal
        || rule.type == ValidationRule::Type::TextLength;
    put("operator", operatorName(rule.op), !bounded);
    put("value1", operandText(rule.value1, names), rule.value1.isEmpty());
    put("value2", operandText(rule.value2, names), rule.value2.isEmpty());
    put("items", rule.items, rule.items.isEmpty());
    put("allowBlank", rule.allowBlank, rule.allowBlank);
    put("inputTitle", rule.inputTitle, rule.inputTitle.isEmpty());
    put("inputMessage", rule.inputMessage, rule.inputMessage.isEmpty());
    put("errorStyle", enumName(errorStyles, rule.errorStyle), rule.errorStyle == ValidationRule::ErrorStyle::Stop);
    put("errorTitle", rule.errorTitle, rule.errorTitle.isEmpty());
    put("errorMessage", rule.errorMessage, rule.errorMessage.isEmpty());
    return map;
}

} // namespace QindaTK::Calc
