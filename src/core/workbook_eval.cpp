// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_formula.h"

#include <cmath>

namespace QindaTK::Calc {
namespace {
constexpr qsizetype MaxTextLength = 32767;

struct Operand {
    Value value;                    // scalar operands
    const QString *sheet = nullptr; // non-null for references
    qint32 firstRow = 0, firstColumn = 0, lastRow = 0, lastColumn = 0;
    bool isReference() const { return sheet != nullptr; }
    bool isArea() const { return sheet && (firstRow != lastRow || firstColumn != lastColumn); }
};
Operand scalarOperand(Value value) { Operand o; o.value = std::move(value); return o; }
Operand errorOperand(ErrorKind kind) { return scalarOperand(Value::fromError(kind)); }

double roundHalfAwayFromZero(double number, double digitsValue)
{
    const double digits = std::trunc(digitsValue);
    if (digits > 15) return number;
    if (digits < -308) return 0.0;
    const double scale = std::pow(10.0, std::fabs(digits));
    double scaled = digits >= 0 ? number * scale : number / scale;
    // AGENT-NOTE: rounding the scaled value to 15 significant digits first
    // removes binary representation error (2.675 * 100 = 267.49999999999997)
    // so ROUND(2.675, 2) is 2.68, matching decimal half-away-from-zero.
    if (std::isfinite(scaled)) scaled = QString::number(scaled, 'g', 15).toDouble();
    const double rounded = std::round(scaled);
    return digits >= 0 ? rounded / scale : rounded * scale;
}

class Evaluator {
public:
    Evaluator(const Formula &f, const QString &host, const EvalContext &context)
        : m_f(f), m_host(host), m_context(context) {}
    Value result()
    {
        if (m_f.root < 0) return Value::fromError(ErrorKind::Value);
        Value v = scalar(eval(m_f.root));
        return v.isBlank() ? Value::fromNumber(0.0) : v;
    }

private:
    Value scalar(const Operand &o) const
    {
        if (!o.isReference()) return o.value;
        if (o.isArea()) return Value::fromError(ErrorKind::Value);
        return m_context.cell(*o.sheet, o.firstRow, o.firstColumn);
    }
    qint32 argument(const Node &call, qint32 i) const { return m_f.args[size_t(call.a + i)]; }
    Operand eval(qint32 index);
    Operand binary(const Node &n);
    Operand call(const Node &n);
    // Visits every value an aggregate argument contributes. References
    // report stored cells (blank cells are never visited); direct values are
    // reported once. Returns the first error the visitor asks to stop on.
    template <typename Visit> ErrorKind forEachArgument(const Node &n, Visit visit);

    const Formula &m_f;
    const QString &m_host;
    const EvalContext &m_context;
};

Operand Evaluator::eval(qint32 index)
{
    const Node &n = m_f.nodes[size_t(index)];
    switch (n.kind) {
    case Node::Kind::Number: return scalarOperand(Value::fromNumber(n.number));
    case Node::Kind::Text: return scalarOperand(Value::fromText(m_f.strings.at(n.text)));
    case Node::Kind::Boolean: return scalarOperand(Value::fromBool(n.boolean));
    case Node::Kind::Error: return errorOperand(n.error);
    case Node::Kind::Cell:
    case Node::Kind::Range: {
        Operand o;
        o.sheet = n.sheet >= 0 ? &m_f.sheets.at(n.sheet) : &m_host;
        o.firstRow = n.first.row;
        o.firstColumn = n.first.column;
        o.lastRow = n.kind == Node::Kind::Range ? n.last.row : n.first.row;
        o.lastColumn = n.kind == Node::Kind::Range ? n.last.column : n.first.column;
        return o;
    }
    case Node::Kind::Group: return eval(n.a);
    case Node::Kind::Unary: {
        const NumberResult x = toNumber(scalar(eval(n.a)));
        if (x.error != ErrorKind::None) return errorOperand(x.error);
        if (n.op == Op::Neg) return scalarOperand(Value::fromNumber(-x.number));
        if (n.op == Op::Percent) return scalarOperand(Value::fromNumber(x.number / 100.0));
        return scalarOperand(Value::fromNumber(x.number));
    }
    case Node::Kind::Binary: return binary(n);
    case Node::Kind::Call: return call(n);
    }
    return errorOperand(ErrorKind::Value);
}

Operand Evaluator::binary(const Node &n)
{
    const Value left = scalar(eval(n.a));
    const Value right = scalar(eval(n.b));
    if (left.isError()) return scalarOperand(left);
    if (right.isError()) return scalarOperand(right);
    switch (n.op) {
    case Op::Concat: {
        const QString text = toText(left) + toText(right);
        if (text.size() > MaxTextLength) return errorOperand(ErrorKind::Value);
        return scalarOperand(Value::fromText(text));
    }
    case Op::Eq: return scalarOperand(Value::fromBool(compareValues(left, right) == 0));
    case Op::Ne: return scalarOperand(Value::fromBool(compareValues(left, right) != 0));
    case Op::Lt: return scalarOperand(Value::fromBool(compareValues(left, right) < 0));
    case Op::Le: return scalarOperand(Value::fromBool(compareValues(left, right) <= 0));
    case Op::Gt: return scalarOperand(Value::fromBool(compareValues(left, right) > 0));
    case Op::Ge: return scalarOperand(Value::fromBool(compareValues(left, right) >= 0));
    default: break;
    }
    const NumberResult a = toNumber(left), b = toNumber(right);
    if (a.error != ErrorKind::None) return errorOperand(a.error);
    if (b.error != ErrorKind::None) return errorOperand(b.error);
    switch (n.op) {
    case Op::Add: return scalarOperand(Value::fromNumber(a.number + b.number));
    case Op::Sub: return scalarOperand(Value::fromNumber(a.number - b.number));
    case Op::Mul: return scalarOperand(Value::fromNumber(a.number * b.number));
    case Op::Div:
        if (b.number == 0.0) return errorOperand(ErrorKind::Div0);
        return scalarOperand(Value::fromNumber(a.number / b.number));
    case Op::Pow:
        if (a.number == 0.0 && b.number == 0.0) return errorOperand(ErrorKind::Num);
        if (a.number == 0.0 && b.number < 0.0) return errorOperand(ErrorKind::Div0);
        return scalarOperand(Value::fromNumber(std::pow(a.number, b.number)));
    default: break;
    }
    return errorOperand(ErrorKind::Value);
}

template <typename Visit>
ErrorKind Evaluator::forEachArgument(const Node &n, Visit visit)
{
    for (qint32 i = 0; i < n.b; ++i) {
        const Operand o = eval(argument(n, i));
        if (!o.isReference()) {
            const ErrorKind stop = visit(o.value, false);
            if (stop != ErrorKind::None) return stop;
            continue;
        }
        ErrorKind stop = ErrorKind::None;
        m_context.visitRange(*o.sheet, o.firstRow, o.firstColumn, o.lastRow, o.lastColumn,
                             [&](const Value &v) {
                                 stop = visit(v, true);
                                 return stop == ErrorKind::None;
                             });
        if (stop != ErrorKind::None) return stop;
    }
    return ErrorKind::None;
}

Operand Evaluator::call(const Node &n)
{
    switch (n.function) {
    case Function::If: {
        const Value condition = scalar(eval(argument(n, 0)));
        const BoolResult test = toBool(condition);
        if (test.error != ErrorKind::None) return errorOperand(test.error);
        if (test.value) return eval(argument(n, 1));
        return n.b > 2 ? eval(argument(n, 2)) : scalarOperand(Value::fromBool(false));
    }
    case Function::IfError: {
        const Value v = scalar(eval(argument(n, 0)));
        return v.isError() ? scalarOperand(scalar(eval(argument(n, 1)))) : scalarOperand(v);
    }
    case Function::Not: {
        const BoolResult b = toBool(scalar(eval(argument(n, 0))));
        if (b.error != ErrorKind::None) return errorOperand(b.error);
        return scalarOperand(Value::fromBool(!b.value));
    }
    case Function::Abs: {
        const NumberResult x = toNumber(scalar(eval(argument(n, 0))));
        if (x.error != ErrorKind::None) return errorOperand(x.error);
        return scalarOperand(Value::fromNumber(std::fabs(x.number)));
    }
    case Function::Round: {
        const NumberResult x = toNumber(scalar(eval(argument(n, 0))));
        if (x.error != ErrorKind::None) return errorOperand(x.error);
        const NumberResult d = toNumber(scalar(eval(argument(n, 1))));
        if (d.error != ErrorKind::None) return errorOperand(d.error);
        return scalarOperand(Value::fromNumber(roundHalfAwayFromZero(x.number, d.number)));
    }
    case Function::Count: {
        double count = 0;
        forEachArgument(n, [&](const Value &v, bool fromReference) {
            double ignored = 0;
            if (v.isNumber() || (!fromReference && (v.isBoolean()
                                                    || (v.isText() && parseNumber(v.text, &ignored)))))
                ++count;
            return ErrorKind::None;
        });
        return scalarOperand(Value::fromNumber(count));
    }
    case Function::CountA: {
        double count = 0;
        forEachArgument(n, [&](const Value &v, bool) {
            if (!v.isBlank()) ++count;
            return ErrorKind::None;
        });
        return scalarOperand(Value::fromNumber(count));
    }
    case Function::And:
    case Function::Or: {
        const bool isAnd = n.function == Function::And;
        bool seen = false, result = isAnd;
        const ErrorKind error = forEachArgument(n, [&](const Value &v, bool fromReference) {
            if (v.isError()) return v.error;
            if (v.isBlank() || (fromReference && v.isText())) return ErrorKind::None;
            const BoolResult b = toBool(v);
            if (b.error != ErrorKind::None) return b.error;
            seen = true;
            result = isAnd ? (result && b.value) : (result || b.value);
            return ErrorKind::None;
        });
        if (error != ErrorKind::None) return errorOperand(error);
        if (!seen) return errorOperand(ErrorKind::Value);
        return scalarOperand(Value::fromBool(result));
    }
    case Function::Sum:
    case Function::Average:
    case Function::Min:
    case Function::Max: {
        double sum = 0, count = 0;
        double best = n.function == Function::Min ? INFINITY : -INFINITY;
        const ErrorKind error = forEachArgument(n, [&](const Value &v, bool fromReference) {
            if (v.isError()) return v.error;
            double x = 0;
            if (fromReference) {
                if (!v.isNumber()) return ErrorKind::None;
                x = v.number;
            } else {
                const NumberResult r = toNumber(v);
                if (r.error != ErrorKind::None) return r.error;
                x = r.number;
            }
            sum += x;
            ++count;
            best = n.function == Function::Min ? std::min(best, x) : std::max(best, x);
            return ErrorKind::None;
        });
        if (error != ErrorKind::None) return errorOperand(error);
        switch (n.function) {
        case Function::Sum: return scalarOperand(Value::fromNumber(sum));
        case Function::Average:
            if (count == 0) return errorOperand(ErrorKind::Div0);
            return scalarOperand(Value::fromNumber(sum / count));
        default:
            return scalarOperand(Value::fromNumber(count == 0 ? 0.0 : best));
        }
    }
    }
    return errorOperand(ErrorKind::Value);
}
} // namespace

Value evaluateFormula(const Formula &formula, const QString &hostSheetId, const EvalContext &context)
{
    return Evaluator(formula, hostSheetId, context).result();
}

} // namespace QindaTK::Calc
