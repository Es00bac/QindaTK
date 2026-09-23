// SPDX-License-Identifier: LGPL-3.0-or-later
#include "expression_evaluator.h"
#include "locale_service.h"

#include <QLocale>
#include <QtMath>

namespace QindaTK {
namespace {
struct Value { double number = 0; QString dimension; };
struct Fault { QString message; int offset = 0; int length = 1; };
struct Budget { int tokens = 0; int operations = 0; QStringList resolving; };

class Parser {
public:
    Parser(const QString &text, const QLocale &locale, const QVariantMap &symbols,
           const QVariantMap &units, Budget &budget)
        : m_text(text), m_locale(locale), m_symbols(symbols), m_units(units), m_budget(budget) {}

    QVariantMap run() {
        if (m_text.size() > 512) return failure({QStringLiteral("Expression exceeds 512 characters"), 512, 1});
        if (m_text.trimmed().isEmpty()) return failure({QStringLiteral("Expression is empty"), 0, 0});
        Value value;
        if (!sum(value, 0)) return failure(m_fault);
        spaces();
        if (m_pos != m_text.size()) return failure({QStringLiteral("Unexpected input"), m_pos, 1});
        if (!qIsFinite(value.number)) return failure({QStringLiteral("Result is not finite"), m_pos, 0});
        return {{"ok", true}, {"value", value.number}, {"unit", value.dimension},
                {"error", QString()}, {"offset", -1}, {"length", 0}};
    }
private:
    QVariantMap failure(const Fault &fault) const {
        return {{"ok", false}, {"value", QVariant()}, {"unit", QString()},
                {"error", fault.message}, {"offset", fault.offset}, {"length", fault.length}};
    }
    void spaces() { while (m_pos < m_text.size() && m_text.at(m_pos).isSpace()) ++m_pos; }
    bool take(QChar c) { spaces(); if (m_pos < m_text.size() && m_text.at(m_pos) == c) { ++m_pos; return true; } return false; }
    bool fail(const QString &message, int start, int length = 1) {
        m_fault = {message, start, length}; return false;
    }
    bool tick(bool operation = false) {
        if (++m_budget.tokens > 128) return fail(QStringLiteral("Expression exceeds 128 tokens"), m_pos);
        if (operation && ++m_budget.operations > 256) return fail(QStringLiteral("Expression exceeds 256 operations"), m_pos);
        return true;
    }
    bool finite(Value &v, int at) {
        return qIsFinite(v.number) || fail(QStringLiteral("Result is not finite"), at);
    }
    bool compatible(const Value &a, const Value &b, int at) {
        return a.dimension == b.dimension || fail(QStringLiteral("Incompatible units"), at);
    }
    bool sum(Value &out, int depth) {
        if (!product(out, depth)) return false;
        while (true) {
            spaces(); const int at = m_pos;
            if (at >= m_text.size() || (m_text.at(at) != '+' && m_text.at(at) != '-')) return true;
            const QChar op = m_text.at(m_pos++);
            if (!tick(true)) return false;
            Value rhs; if (!product(rhs, depth) || !compatible(out, rhs, at)) return false;
            out.number = op == '+' ? out.number + rhs.number : out.number - rhs.number;
            if (!finite(out, at)) return false;
        }
    }
    bool product(Value &out, int depth) {
        if (!unary(out, depth)) return false;
        while (true) {
            spaces(); const int at = m_pos;
            if (at >= m_text.size() || (m_text.at(at) != '*' && m_text.at(at) != '/')) return true;
            const QChar op = m_text.at(m_pos++);
            if (!tick(true)) return false;
            Value rhs; if (!unary(rhs, depth)) return false;
            if (op == '/') {
                if (rhs.number == 0) return fail(QStringLiteral("Division by zero"), at);
                if (!rhs.dimension.isEmpty()) return fail(QStringLiteral("Cannot divide by a unit"), at);
                out.number /= rhs.number;
            } else {
                if (!out.dimension.isEmpty() && !rhs.dimension.isEmpty())
                    return fail(QStringLiteral("Cannot multiply two units"), at);
                out.number *= rhs.number;
                if (out.dimension.isEmpty()) out.dimension = rhs.dimension;
            }
            if (!finite(out, at)) return false;
        }
    }
    bool unary(Value &out, int depth) {
        spaces(); const int at = m_pos;
        if (take('+')) { if (!tick(true)) return false; return unary(out, depth); }
        if (take('-')) { if (!tick(true) || !unary(out, depth)) return false; out.number = -out.number; return true; }
        if (take('(')) {
            if (depth >= 16) return fail(QStringLiteral("Expression nesting exceeds 16"), at);
            if (!tick() || !sum(out, depth + 1)) return false;
            return take(')') || fail(QStringLiteral("Expected closing parenthesis"), m_pos);
        }
        return atom(out, depth);
    }
    QString identifier() {
        spaces(); const int start = m_pos;
        if (m_pos >= m_text.size() || !(m_text.at(m_pos).isLetter() || m_text.at(m_pos) == '_')) return {};
        ++m_pos;
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (!c.isLetterOrNumber() && c != '_' && c != '.') break;
            ++m_pos;
        }
        return m_text.mid(start, m_pos - start);
    }
    bool atom(Value &out, int depth) {
        spaces(); const int at = m_pos;
        if (at >= m_text.size()) return fail(QStringLiteral("Expected a value"), at, 0);
        const QChar c = m_text.at(at);
        const QString decimalText(m_locale.decimalPoint());
        const QChar decimal = decimalText.isEmpty() ? QChar('.') : decimalText.at(0);
        if (c.isDigit() || c == decimal) {
            if (!tick()) return false;
            bool digit = false; bool separator = false;
            while (m_pos < m_text.size()) {
                const QChar n = m_text.at(m_pos);
                if (n.isDigit()) { digit = true; ++m_pos; }
                else if (n == decimal && !separator) { separator = true; ++m_pos; }
                else break;
            }
            if (!digit) return fail(QStringLiteral("Expected digits"), at);
            if (m_pos < m_text.size() && (m_text.at(m_pos) == 'e' || m_text.at(m_pos) == 'E')) {
                const int exp = m_pos++;
                if (m_pos < m_text.size() && (m_text.at(m_pos) == '+' || m_text.at(m_pos) == '-')) ++m_pos;
                const int digitAt = m_pos;
                while (m_pos < m_text.size() && m_text.at(m_pos).isDigit()) ++m_pos;
                if (digitAt == m_pos) return fail(QStringLiteral("Invalid exponent"), exp, m_pos - exp);
            }
            bool ok = false;
            out.number = m_locale.toDouble(m_text.mid(at, m_pos - at), &ok);
            if (!ok || !qIsFinite(out.number)) return fail(QStringLiteral("Invalid number"), at, m_pos - at);
            const int numberEnd = m_pos;
            const QString unit = identifier();
            if (!unit.isEmpty()) {
                if (!m_units.contains(unit)) return fail(QStringLiteral("Unknown unit: %1").arg(unit), numberEnd, unit.size());
                const QVariantMap definition = m_units.value(unit).toMap();
                bool scaleOk = false; const double scale = definition.value(QStringLiteral("scale")).toDouble(&scaleOk);
                out.dimension = definition.value(QStringLiteral("dimension")).toString();
                if (!scaleOk || !qIsFinite(scale) || scale <= 0 || out.dimension.isEmpty())
                    return fail(QStringLiteral("Invalid unit definition"), numberEnd, unit.size());
                out.number *= scale;
                return finite(out, at);
            }
            return true;
        }
        const QString name = identifier();
        if (name.isEmpty()) return fail(QStringLiteral("Expected a value"), at);
        if (!tick()) return false;
        if (take('(')) return function(name, out, depth, at);
        if (!m_symbols.contains(name)) return fail(QStringLiteral("Unknown symbol: %1").arg(name), at, name.size());
        if (m_budget.resolving.contains(name)) return fail(QStringLiteral("Cyclic symbol: %1").arg(name), at, name.size());
        if (m_budget.resolving.size() >= 16) return fail(QStringLiteral("Symbol nesting exceeds 16"), at, name.size());
        m_budget.resolving << name;
        const QVariant item = m_symbols.value(name);
        bool ok = false;
        if (item.metaType().id() == QMetaType::QString) {
            Parser nested(item.toString(), m_locale, m_symbols, m_units, m_budget);
            const QVariantMap result = nested.run();
            if (result.value(QStringLiteral("ok")).toBool()) {
                out.number = result.value(QStringLiteral("value")).toDouble();
                out.dimension = result.value(QStringLiteral("unit")).toString(); ok = true;
            } else m_fault = {result.value(QStringLiteral("error")).toString(), at, int(name.size())};
        } else if (item.metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap map = item.toMap();
            out.number = map.value(QStringLiteral("value")).toDouble(&ok);
            const QString unit = map.value(QStringLiteral("unit")).toString();
            if (ok && !unit.isEmpty()) {
                const QVariantMap definition = m_units.value(unit).toMap();
                bool scaleOk = false; const double scale = definition.value(QStringLiteral("scale")).toDouble(&scaleOk);
                out.dimension = definition.value(QStringLiteral("dimension")).toString();
                ok = scaleOk && qIsFinite(scale) && scale > 0 && !out.dimension.isEmpty();
                out.number *= scale;
            }
        } else out.number = item.toDouble(&ok);
        m_budget.resolving.removeLast();
        if (!ok || !qIsFinite(out.number)) return m_fault.message.isEmpty()
            ? fail(QStringLiteral("Invalid symbol: %1").arg(name), at, name.size()) : false;
        return true;
    }
    bool function(const QString &name, Value &out, int depth, int at) {
        if (depth >= 16) return fail(QStringLiteral("Expression nesting exceeds 16"), at, name.size());
        const int count = name == "abs" ? 1 : (name == "min" || name == "max") ? 2 : name == "clamp" ? 3 : 0;
        if (!count) return fail(QStringLiteral("Function is not permitted: %1").arg(name), at, name.size());
        QVector<Value> args;
        for (int index = 0; index < count; ++index) {
            Value arg; if (!sum(arg, depth + 1)) return false;
            args << arg;
            if (index < count - 1) {
                if (!take(';') && !(QString(m_locale.decimalPoint()) != QStringLiteral(",") && take(',')))
                    return fail(QStringLiteral("Expected argument separator"), m_pos);
            }
        }
        if (!take(')')) return fail(QStringLiteral("Expected closing parenthesis"), m_pos);
        if (!tick(true)) return false;
        for (const Value &arg : args) if (!compatible(args.first(), arg, at)) return false;
        out = args.first();
        if (name == "abs") out.number = qAbs(out.number);
        else if (name == "min") out.number = qMin(out.number, args.at(1).number);
        else if (name == "max") out.number = qMax(out.number, args.at(1).number);
        else out.number = qBound(args.at(1).number, out.number, args.at(2).number);
        return true;
    }
    const QString &m_text;
    const QLocale &m_locale;
    const QVariantMap &m_symbols;
    const QVariantMap &m_units;
    Budget &m_budget;
    int m_pos = 0;
    Fault m_fault;
};
} // namespace

ExpressionEvaluator::ExpressionEvaluator(QObject *parent) : QObject(parent) {}
void ExpressionEvaluator::setLocaleService(LocaleService *locale) { if (m_localeService == locale) return; m_localeService = locale; emit changed(); }
void ExpressionEvaluator::setSymbols(const QVariantMap &symbols) { if (m_symbols == symbols) return; m_symbols = symbols; emit changed(); }
void ExpressionEvaluator::setUnits(const QVariantMap &units) { if (m_units == units) return; m_units = units; emit changed(); }
QVariantMap ExpressionEvaluator::evaluate(const QString &expression) const {
    Budget budget;
    const QLocale locale(m_localeService ? m_localeService->localeName() : QLocale::system().name());
    Parser parser(expression, locale, m_symbols, m_units, budget);
    return parser.run();
}
} // namespace QindaTK
