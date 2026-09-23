// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
using namespace Calc;
namespace {
QString styleName(ValidationRule::ErrorStyle style)
{
    switch (style) {
    case ValidationRule::ErrorStyle::Stop: return QStringLiteral("stop");
    case ValidationRule::ErrorStyle::Warning: return QStringLiteral("warning");
    case ValidationRule::ErrorStyle::Information: return QStringLiteral("information");
    }
    return {};
}
bool withinBounds(RuleOperator op, double x, double a, double b)
{
    const double lo = std::min(a, b), hi = std::max(a, b);
    switch (op) {
    case RuleOperator::Between: return x >= lo && x <= hi;
    case RuleOperator::NotBetween: return x < lo || x > hi;
    case RuleOperator::Equal: return x == a;
    case RuleOperator::NotEqual: return x != a;
    case RuleOperator::Greater: return x > a;
    case RuleOperator::Less: return x < a;
    case RuleOperator::GreaterOrEqual: return x >= a;
    case RuleOperator::LessOrEqual: return x <= a;
    }
    return false;
}
QString boundsText(RuleOperator op, const Value &a, const Value &b)
{
    const QString x = displayText(a), y = displayText(b);
    switch (op) {
    case RuleOperator::Between: return QStringLiteral(" between %1 and %2").arg(x, y);
    case RuleOperator::NotBetween: return QStringLiteral(" not between %1 and %2").arg(x, y);
    case RuleOperator::Equal: return QStringLiteral(" equal to %1").arg(x);
    case RuleOperator::NotEqual: return QStringLiteral(" other than %1").arg(x);
    case RuleOperator::Greater: return QStringLiteral(" greater than %1").arg(x);
    case RuleOperator::Less: return QStringLiteral(" less than %1").arg(x);
    case RuleOperator::GreaterOrEqual: return QStringLiteral(" of at least %1").arg(x);
    case RuleOperator::LessOrEqual: return QStringLiteral(" of at most %1").arg(x);
    }
    return {};
}
// The workbook as it would be with `value` entered at one cell: rule
// formulas (a custom rule such as =F1>A1) see the candidate, not the old value.
class CandidateContext final : public EvalContext {
public:
    CandidateContext(const WorkbookPrivate &d, const Sheet &s, int row, int column, const Value &value)
        : m_d(d), m_sheet(s), m_position(positionKey(row, column)), m_row(row), m_column(column), m_value(value) {}
    Value cell(const QString &sheetId, int row, int column) const override
    {
        if (sheetId == m_sheet.id && row == m_row && column == m_column) return m_value;
        return m_d.cell(sheetId, row, column);
    }
    void visitRange(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn,
                    const std::function<bool(const Value &)> &visitor) const override
    {
        if (sheetId != m_sheet.id || m_row < firstRow || m_row > lastRow || m_column < firstColumn || m_column > lastColumn) {
            m_d.visitRange(sheetId, firstRow, firstColumn, lastRow, lastColumn, visitor);
            return;
        }
        bool placed = false;
        bool stopped = false;
        m_d.visitCells(m_sheet, firstRow, firstColumn, lastRow, lastColumn, [&](quint64 position, const Value &v) {
            if (!placed && position >= m_position) {
                placed = true;
                if (!m_value.isBlank() && !visitor(m_value)) { stopped = true; return false; }
                if (position == m_position) return true; // the old value is replaced
            }
            if (!visitor(v)) { stopped = true; return false; }
            return true;
        });
        if (!placed && !stopped && !m_value.isBlank()) visitor(m_value);
    }
    bool resolveName(const QString &name, Precedent *target) const override { return m_d.resolveName(name, target); }

private:
    const WorkbookPrivate &m_d;
    const Sheet &m_sheet;
    quint64 m_position;
    int m_row, m_column;
    Value m_value;
};
} // namespace

const ValidationRule *WorkbookPrivate::validationRuleAt(const Sheet &s, int row, int column) const
{
    for (const ValidationRule &rule : s.features->validations)
        for (const Area &area : rule.areas)
            if (area.contains(row, column)) return &rule;
    return nullptr;
}

QString WorkbookPrivate::validationProblem(const Sheet &s, int row, int column, const CellContent &candidate,
                                           const ValidationRule **ruleOut) const
{
    const ValidationRule *rule = validationRuleAt(s, row, column);
    if (ruleOut) *ruleOut = rule;
    if (!rule || rule->type == ValidationRule::Type::Any) return {};
    const Value value = candidate.kind == CellContent::Kind::Formula
        ? evaluateFormula(*candidate.formula, s.id, *this)
        : candidate.kind == CellContent::Kind::Literal ? candidate.literal : Value::blank();
    const auto message = [&](const QString &fallback) {
        return rule->errorMessage.isEmpty() ? fallback : rule->errorMessage;
    };
    if (value.isBlank() || (value.isText() && value.text.isEmpty()))
        return rule->allowBlank ? QString() : message(QStringLiteral("A value is required"));
    const Area &anchor = rule->areas.front();
    const CandidateContext context(*this, s, row, column, value);
    const Value a = operandValue(s, rule->value1, anchor, row, column, &context);
    const Value b = operandValue(s, rule->value2, anchor, row, column, &context);
    switch (rule->type) {
    case ValidationRule::Type::WholeNumber:
    case ValidationRule::Type::Decimal:
    case ValidationRule::Type::TextLength: {
        const bool whole = rule->type == ValidationRule::Type::WholeNumber;
        const bool length = rule->type == ValidationRule::Type::TextLength;
        const QString what = whole ? QStringLiteral("Enter a whole number")
            : length ? QStringLiteral("Enter text with a length") : QStringLiteral("Enter a number");
        const QString fallback = what + boundsText(rule->op, a, b);
        if (!length && (!value.isNumber() || (whole && value.number != std::floor(value.number))))
            return message(fallback);
        const NumberResult x = toNumber(a), y = toNumber(b);
        const bool twoValues = rule->op == RuleOperator::Between || rule->op == RuleOperator::NotBetween;
        if (x.error != ErrorKind::None || (twoValues && y.error != ErrorKind::None)) return message(fallback);
        const double subject = length ? double(displayText(value).size()) : value.number;
        return withinBounds(rule->op, subject, x.number, y.number) ? QString() : message(fallback);
    }
    case ValidationRule::Type::List: {
        QStringList allowed = rule->items;
        if (allowed.isEmpty() && rule->value1.formula && rule->value1.formula->root >= 0) {
            const Formula &f = *rule->value1.formula;
            const Node &root = f.nodes[size_t(f.root)];
            Precedent target;
            bool found = false;
            if (root.kind == Node::Kind::Name) {
                found = resolveName(f.strings.at(root.text), &target) && !target.sheetId.isEmpty();
            } else if (root.kind == Node::Kind::Cell || root.kind == Node::Kind::Range) {
                target.sheetId = root.sheet >= 0 ? f.sheets.at(root.sheet) : s.id;
                target.firstRow = root.first.row;
                target.firstColumn = root.first.column;
                target.lastRow = root.kind == Node::Kind::Range ? root.last.row : root.first.row;
                target.lastColumn = root.kind == Node::Kind::Range ? root.last.column : root.first.column;
                found = true;
            }
            if (found)
                visitRange(target.sheetId, target.firstRow, target.firstColumn, target.lastRow, target.lastColumn,
                           [&](const Value &v) {
                               if (!v.isBlank()) allowed.append(displayText(v));
                               return allowed.size() < MaxListItems;
                           });
        }
        const QString text = displayText(value);
        if (allowed.contains(text, Qt::CaseInsensitive)) return {};
        const QStringList shown = allowed.mid(0, 5);
        return message(QStringLiteral("Choose one of: %1%2").arg(shown.join(QStringLiteral(", ")),
                                                                 allowed.size() > 5 ? QStringLiteral(", …") : QString()));
    }
    case ValidationRule::Type::Custom: {
        const BoolResult test = toBool(a);
        return test.error == ErrorKind::None && test.value ? QString()
                                                           : message(QStringLiteral("The value does not meet the cell's rule"));
    }
    case ValidationRule::Type::Any: break;
    }
    return {};
}

bool WorkbookPrivate::applyInputs(const QString &label, const QString &sheetId, const std::vector<InputEntry> &entries,
                                  const QString &policy)
{
    const bool skipInvalid = policy == u"skipInvalid";
    if (!skipInvalid && policy != u"allOrNothing")
        return fail(QStringLiteral("Paste policy is allOrNothing or skipInvalid"));
    QVariantList invalid;
    int applied = 0;
    const bool ok = mutate(label, [&](QString *error, QVariantMap *detail) {
        Sheet *s = sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!sheetEditable(*s, error)) return false;
        if (entries.size() > size_t(Workbook::MaxTransactionCells)) {
            *error = QStringLiteral("An edit is limited to %1 cells").arg(Workbook::MaxTransactionCells);
            return false;
        }
        Mutation m;
        m.sheetId = sheetId;
        bool blocked = false;
        for (const InputEntry &entry : entries) {
            if (entry.row < 0 || entry.row >= s->rows || entry.column < 0 || entry.column >= s->columns) {
                *error = QStringLiteral("Paste extends outside the sheet");
                return false;
            }
            const QString address = cellAddress(entry.row, entry.column);
            CellContent content;
            ParseError parse;
            QVariantMap problem{{"row", entry.row}, {"column", entry.column}, {"address", address}};
            if (!parseInput(sheetId, entry.input, &content, &parse)) {
                problem.insert({{"message", parse.message}, {"style", QStringLiteral("stop")}, {"syntax", true},
                                {"start", parse.start}, {"end", parse.end}});
                invalid.append(problem);
                blocked = true;
                continue;
            }
            const ValidationRule *rule = nullptr;
            if (const QString message = validationProblem(*s, entry.row, entry.column, content, &rule); !message.isEmpty()) {
                const bool stop = rule->errorStyle == ValidationRule::ErrorStyle::Stop;
                problem.insert({{"message", message}, {"style", styleName(rule->errorStyle)}, {"ruleId", rule->id}});
                invalid.append(problem);
                blocked = blocked || stop;
                if (stop) continue;
            }
            m.cells.push_back({entry.row, entry.column, std::move(content)});
        }
        const auto blocking = [](const QVariant &item) { return item.toMap().value(QStringLiteral("style")).toString() == u"stop"; };
        if (blocked && !skipInvalid) {
            const auto first = std::find_if(invalid.cbegin(), invalid.cend(), blocking);
            const QVariantMap problem = first->toMap();
            const qsizetype count = std::count_if(invalid.cbegin(), invalid.cend(), blocking);
            *error = count == 1 ? QStringLiteral("%1: %2").arg(problem.value(QStringLiteral("address")).toString(),
                                                              problem.value(QStringLiteral("message")).toString())
                                : QStringLiteral("%1 cells are invalid; first %2: %3").arg(count)
                                      .arg(problem.value(QStringLiteral("address")).toString(),
                                           problem.value(QStringLiteral("message")).toString());
            *detail = problem;
            detail->insert(QStringLiteral("invalid"), invalid);
            return false;
        }
        applied = int(m.cells.size());
        return m.cells.empty() || record(std::move(m), error);
    });
    pasteReport = {{"policy", policy}, {"ok", ok}, {"applied", ok ? applied : 0}, {"invalid", invalid}};
    emit q->pasteReported();
    return ok;
}

QVariantMap Workbook::lastPasteReport() const { return d->pasteReport; }

bool Workbook::pasteInputs(const QString &sheetId, int row, int column, const QVariantList &rows, const QString &policy)
{
    std::vector<WorkbookPrivate::InputEntry> entries;
    qsizetype width = -1;
    for (qsizetype r = 0; r < rows.size(); ++r) {
        if (rows[r].metaType().id() != QMetaType::QVariantList)
            return d->fail(QStringLiteral("Paste rows must be value lists"));
        const QVariantList values = rows[r].toList();
        if (width >= 0 && values.size() != width) return d->fail(QStringLiteral("Paste must be rectangular"));
        width = values.size();
        if (qint64(rows.size()) * width > MaxTransactionCells)
            return d->fail(QStringLiteral("Paste exceeds %1 cells").arg(MaxTransactionCells));
        for (qsizetype c = 0; c < values.size(); ++c)
            entries.push_back({row + int(r), column + int(c), values[c]});
    }
    return d->applyInputs(QStringLiteral("Paste cells"), sheetId, entries, policy);
}

bool Workbook::setCellInputs(const QString &sheetId, const QVariantList &cells, const QString &policy, const QString &label)
{
    if (cells.size() > MaxTransactionCells) return d->fail(QStringLiteral("An edit is limited to %1 cells").arg(MaxTransactionCells));
    std::vector<WorkbookPrivate::InputEntry> entries;
    for (const QVariant &item : cells) {
        const QVariantMap cell = item.toMap();
        bool rowOk = false, columnOk = false;
        const int row = cell.value(QStringLiteral("row")).toInt(&rowOk);
        const int column = cell.value(QStringLiteral("column")).toInt(&columnOk);
        if (!rowOk || !columnOk) return d->fail(QStringLiteral("Each cell needs a row and a column"));
        entries.push_back({row, column, cell.value(QStringLiteral("input"))});
    }
    return d->applyInputs(label.isEmpty() ? QStringLiteral("Edit cells") : label, sheetId, entries, policy);
}

QVariantMap Workbook::validateInput(const QString &sheetId, int row, int column, const QString &input) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s || row < 0 || column < 0 || row >= s->rows || column >= s->columns)
        return {{"ok", false}, {"style", QStringLiteral("stop")}, {"message", QStringLiteral("Cell is outside the sheet")}};
    CellContent content;
    ParseError parse;
    if (!d->parseInput(sheetId, input, &content, &parse))
        return {{"ok", false}, {"style", QStringLiteral("stop")}, {"message", parse.message}, {"syntax", true},
                {"start", parse.start}, {"end", parse.end}};
    const ValidationRule *rule = nullptr;
    const QString message = d->validationProblem(*s, row, column, content, &rule);
    if (message.isEmpty()) return {{"ok", true}};
    return {{"ok", false}, {"style", styleName(rule->errorStyle)}, {"title", rule->errorTitle},
            {"message", message}, {"ruleId", rule->id}};
}

QVariantMap Workbook::validationAt(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    const ValidationRule *rule = s ? d->validationRuleAt(*s, row, column) : nullptr;
    return rule ? validationToMap(*rule, [this](const QString &id) { return d->nameForId(id); }, false) : QVariantMap();
}

} // namespace QindaTK
