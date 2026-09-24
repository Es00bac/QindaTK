// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_formula.h"

#include <algorithm>

namespace QindaTK::Calc {
namespace {
bool isAsciiLetter(QChar c) { return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'); }
bool isAsciiDigit(QChar c) { return c >= u'0' && c <= u'9'; }

const char *opText(Op op)
{
    switch (op) {
    case Op::Add: return "+";
    case Op::Sub: return "-";
    case Op::Mul: return "*";
    case Op::Div: return "/";
    case Op::Pow: return "^";
    case Op::Concat: return "&";
    case Op::Eq: return "=";
    case Op::Ne: return "<>";
    case Op::Lt: return "<";
    case Op::Le: return "<=";
    case Op::Gt: return ">";
    case Op::Ge: return ">=";
    case Op::Neg: return "-";
    case Op::Plus: return "+";
    case Op::Percent: return "%";
    }
    return "";
}

QString wholeText(const Node &n)
{
    if (n.whole == Node::Whole::Columns)
        return (n.first.columnAbsolute ? QStringLiteral("$") : QString()) + columnName(n.first.column) + QLatin1Char(':')
            + (n.last.columnAbsolute ? QStringLiteral("$") : QString()) + columnName(n.last.column);
    return (n.first.rowAbsolute ? QStringLiteral("$") : QString()) + QString::number(n.first.row + 1) + QLatin1Char(':')
        + (n.last.rowAbsolute ? QStringLiteral("$") : QString()) + QString::number(n.last.row + 1);
}

void render(const Formula &f, qint32 index, const SheetNameLookup &names, QString &out)
{
    const Node &n = f.nodes[size_t(index)];
    auto sheetPrefix = [&]() -> bool {
        if (n.sheet < 0) return true;
        const QString name = names ? names(f.sheets.at(n.sheet)) : QString();
        if (name.isEmpty()) return false;
        out += quoteSheetName(name) + QLatin1Char('!');
        return true;
    };
    switch (n.kind) {
    case Node::Kind::Number: out += numberLiteral(n.number); break;
    case Node::Kind::Text: {
        QString body = f.strings.at(n.text);
        body.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        out += QLatin1Char('"') + body + QLatin1Char('"');
        break;
    }
    case Node::Kind::Boolean: out += n.boolean ? QStringLiteral("TRUE") : QStringLiteral("FALSE"); break;
    case Node::Kind::Error: out += errorText(n.error); break;
    case Node::Kind::Cell:
        if (sheetPrefix()) out += refPointText(n.first);
        else out += errorText(ErrorKind::Ref);
        break;
    case Node::Kind::Range:
        if (!sheetPrefix()) out += errorText(ErrorKind::Ref);
        else if (n.whole != Node::Whole::None) out += wholeText(n);
        else out += refPointText(n.first) + QLatin1Char(':') + refPointText(n.last);
        break;
    case Node::Kind::Name: out += f.strings.at(n.text); break;
    case Node::Kind::Unary:
        if (n.op == Op::Percent) { render(f, n.a, names, out); out += QLatin1Char('%'); }
        else { out += QLatin1StringView(opText(n.op)); render(f, n.a, names, out); }
        break;
    case Node::Kind::Binary:
        render(f, n.a, names, out);
        out += QLatin1StringView(opText(n.op));
        render(f, n.b, names, out);
        break;
    case Node::Kind::Call:
        out += QLatin1StringView(functionSpec(n.function).name) + QLatin1Char('(');
        for (qint32 i = 0; i < n.b; ++i) {
            if (i) out += QLatin1Char(',');
            render(f, f.args[size_t(n.a + i)], names, out);
        }
        out += QLatin1Char(')');
        break;
    case Node::Kind::Group:
        out += QLatin1Char('(');
        render(f, n.a, names, out);
        out += QLatin1Char(')');
        break;
    }
}

bool isReference(const Node &n) { return n.kind == Node::Kind::Cell || n.kind == Node::Kind::Range; }

void invalidate(Node &n)
{
    n.kind = Node::Kind::Error;
    n.error = ErrorKind::Ref;
    n.sheet = -1;
}

// Drops sheet IDs no longer used by any reference node.
void compactSheets(Formula &f)
{
    QStringList used;
    for (Node &n : f.nodes) {
        if (!isReference(n) || n.sheet < 0) continue;
        const QString id = f.sheets.at(n.sheet);
        qsizetype index = used.indexOf(id);
        if (index < 0) { used.append(id); index = used.size() - 1; }
        n.sheet = qint32(index);
    }
    f.sheets = used;
}

void normalizeAxis(RefPoint &first, RefPoint &last, bool rows)
{
    if (rows && first.row > last.row) {
        std::swap(first.row, last.row);
        std::swap(first.rowAbsolute, last.rowAbsolute);
    }
    if (!rows && first.column > last.column) {
        std::swap(first.column, last.column);
        std::swap(first.columnAbsolute, last.columnAbsolute);
    }
}

bool offsetPoint(RefPoint &p, int rowOffset, int columnOffset)
{
    const qint64 row = p.rowAbsolute ? p.row : qint64(p.row) + rowOffset;
    const qint64 column = p.columnAbsolute ? p.column : qint64(p.column) + columnOffset;
    if (row < 0 || row >= MaxRows || column < 0 || column >= MaxColumns) return false;
    p.row = qint32(row);
    p.column = qint32(column);
    return true;
}

} // namespace

bool shiftInterval(qint32 &lo, qint32 &hi, const StructuralEdit &edit, bool *affected)
{
    const qint64 limit = edit.rows ? MaxRows : MaxColumns;
    const qint64 at = edit.at, count = edit.count;
    if (edit.insert) {
        const qint64 newLo = lo >= at ? lo + count : lo;
        const qint64 newHi = hi >= at ? hi + count : hi;
        if (newHi >= limit) { *affected = true; return false; }
        lo = qint32(newLo); hi = qint32(newHi);
        return true;
    }
    const qint64 end = at + count;
    if (lo >= at && hi < end) { *affected = true; return false; }
    if (!(hi < at || lo >= end)) *affected = true;
    const qint64 newLo = lo < at ? lo : (lo >= end ? lo - count : at);
    const qint64 newHi = hi < at ? hi : (hi >= end ? hi - count : at - 1);
    lo = qint32(newLo); hi = qint32(newHi);
    return true;
}

bool Formula::sameAs(const Formula &other) const
{
    if (root != other.root || nodes.size() != other.nodes.size() || args != other.args
        || strings != other.strings || sheets != other.sheets) return false;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node &x = nodes[i], &y = other.nodes[i];
        if (x.kind != y.kind || x.op != y.op || x.function != y.function || x.boolean != y.boolean
            || x.error != y.error || x.sheet != y.sheet || x.a != y.a || x.b != y.b || x.text != y.text
            || x.whole != y.whole
            || x.number != y.number || !(x.first == y.first) || !(x.last == y.last)) return false;
    }
    return true;
}

QString renderFormula(const Formula &formula, const SheetNameLookup &sheetNameForId)
{
    QString out(QLatin1Char('='));
    if (formula.root >= 0) render(formula, formula.root, sheetNameForId, out);
    return out;
}

QString columnName(int column)
{
    QString name;
    if (column < 0) return name;
    qint64 n = qint64(column) + 1;
    while (n > 0) {
        const int digit = int((n - 1) % 26);
        name.prepend(QChar(u'A' + digit));
        n = (n - 1) / 26;
    }
    return name;
}
int columnFromName(QStringView letters)
{
    if (letters.isEmpty() || letters.size() > 3) return -1;
    qint64 value = 0;
    for (QChar c : letters) {
        if (!isAsciiLetter(c)) return -1;
        value = value * 26 + (c.toUpper().unicode() - u'A' + 1);
    }
    return value - 1 < MaxColumns ? int(value - 1) : -1;
}
QString cellAddress(int row, int column) { return columnName(column) + QString::number(row + 1); }
QString refPointText(const RefPoint &p)
{
    return (p.columnAbsolute ? QStringLiteral("$") : QString()) + columnName(p.column)
        + (p.rowAbsolute ? QStringLiteral("$") : QString()) + QString::number(p.row + 1);
}
bool parseCellAddress(QStringView text, RefPoint *point)
{
    qsizetype i = 0;
    const qsizetype n = text.size();
    RefPoint result;
    if (i < n && text[i] == u'$') { result.columnAbsolute = true; ++i; }
    const qsizetype lettersStart = i;
    while (i < n && isAsciiLetter(text[i])) ++i;
    const int column = columnFromName(text.mid(lettersStart, i - lettersStart));
    if (column < 0) return false;
    if (i < n && text[i] == u'$') { result.rowAbsolute = true; ++i; }
    const qsizetype digitsStart = i;
    while (i < n && isAsciiDigit(text[i])) ++i;
    if (i != n || i == digitsStart || i - digitsStart > 7) return false;
    const qint64 row = text.mid(digitsStart).toLongLong();
    if (row < 1 || row > MaxRows) return false;
    result.row = qint32(row - 1);
    result.column = column;
    if (point) *point = result;
    return true;
}
QString quoteSheetName(const QString &name)
{
    bool plain = !name.isEmpty() && (isAsciiLetter(name.front()) || name.front() == u'_');
    for (QChar c : name)
        if (!(isAsciiLetter(c) || isAsciiDigit(c) || c == u'_' || c == u'.')) plain = false;
    if (plain && (parseCellAddress(name, nullptr)
                  || name.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
                  || name.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0))
        plain = false;
    if (plain) return name;
    QString escaped = name;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}
QString sheetNameProblem(const QString &name)
{
    if (name.trimmed().isEmpty()) return QStringLiteral("A sheet name cannot be empty");
    if (name.size() > 31) return QStringLiteral("A sheet name is at most 31 characters");
    for (QChar c : name) {
        if (QStringLiteral("[]:*?/\\").contains(c))
            return QStringLiteral("A sheet name cannot contain [ ] : * ? / \\");
        if (c.unicode() < 0x20) return QStringLiteral("A sheet name cannot contain control characters");
    }
    if (name.startsWith(QLatin1Char('\'')) || name.endsWith(QLatin1Char('\'')))
        return QStringLiteral("A sheet name cannot start or end with an apostrophe");
    return {};
}

void collectPrecedents(const Formula &formula, const QString &hostSheetId,
                       std::vector<Precedent> *out, const NameResolver &names)
{
    for (const Node &n : formula.nodes) {
        if (n.kind == Node::Kind::Name) {
            Precedent target;
            if (names && names(formula.strings.at(n.text), &target) && !target.sheetId.isEmpty())
                out->push_back(target);
            continue;
        }
        if (!isReference(n)) continue;
        Precedent p;
        p.sheetId = n.sheet >= 0 ? formula.sheets.at(n.sheet) : hostSheetId;
        p.firstRow = n.first.row;
        p.firstColumn = n.first.column;
        p.lastRow = n.kind == Node::Kind::Range ? n.last.row : n.first.row;
        p.lastColumn = n.kind == Node::Kind::Range ? n.last.column : n.first.column;
        p.range = n.kind == Node::Kind::Range;
        out->push_back(p);
    }
}

FormulaPtr offsetFormula(const FormulaPtr &formula, int rowOffset, int columnOffset)
{
    if (!formula || (rowOffset == 0 && columnOffset == 0)) return formula;
    auto copy = std::make_shared<Formula>(*formula);
    bool changed = false, invalidated = false;
    for (Node &n : copy->nodes) {
        if (!isReference(n)) continue;
        const RefPoint first = n.first, last = n.last;
        // A whole-column range never moves along rows (and whole rows along columns).
        const int rows = n.whole == Node::Whole::Columns ? 0 : rowOffset;
        const int columns = n.whole == Node::Whole::Rows ? 0 : columnOffset;
        bool ok = offsetPoint(n.first, rows, columns);
        if (n.kind == Node::Kind::Range) {
            ok = offsetPoint(n.last, rows, columns) && ok;
            normalizeAxis(n.first, n.last, true);
            normalizeAxis(n.first, n.last, false);
        }
        if (!ok) { invalidate(n); invalidated = true; changed = true; }
        else if (!(first == n.first) || !(last == n.last)) changed = true;
    }
    if (!changed) return formula;
    if (invalidated) compactSheets(*copy);
    return copy;
}

Rewrite rewriteForStructure(const FormulaPtr &formula, const QString &hostSheetId,
                            const StructuralEdit &edit)
{
    Rewrite result{formula, false, false};
    if (!formula || edit.count <= 0) return result;
    std::shared_ptr<Formula> copy;
    for (size_t i = 0; i < formula->nodes.size(); ++i) {
        const Node &source = formula->nodes[i];
        if (!isReference(source)) continue;
        const QString &target = source.sheet >= 0 ? formula->sheets.at(source.sheet) : hostSheetId;
        if (target != edit.sheetId) continue;
        if ((source.whole == Node::Whole::Columns && edit.rows) || (source.whole == Node::Whole::Rows && !edit.rows))
            continue;
        Node n = source;
        const bool range = n.kind == Node::Kind::Range;
        qint32 &lo = edit.rows ? n.first.row : n.first.column;
        qint32 &hi = edit.rows ? (range ? n.last.row : n.first.row) : (range ? n.last.column : n.first.column);
        bool affected = false;
        bool valid = true;
        if (range) {
            valid = shiftInterval(lo, hi, edit, &affected);
        } else {
            qint32 value = lo, copyHi = lo;
            valid = shiftInterval(value, copyHi, edit, &affected);
            lo = value;
        }
        if (!valid) invalidate(n);
        const bool nodeChanged = !valid || !(n.first == source.first) || !(n.last == source.last);
        if (!nodeChanged) continue;
        if (!copy) copy = std::make_shared<Formula>(*formula);
        copy->nodes[i] = n;
        result.changed = true;
        result.valueAffected = result.valueAffected || affected;
    }
    if (copy) {
        compactSheets(*copy);
        result.formula = copy;
    }
    return result;
}

Rewrite invalidateSheet(const FormulaPtr &formula, const QString &removedSheetId)
{
    Rewrite result{formula, false, false};
    if (!formula || !formula->sheets.contains(removedSheetId)) return result;
    auto copy = std::make_shared<Formula>(*formula);
    for (Node &n : copy->nodes)
        if (isReference(n) && n.sheet >= 0 && copy->sheets.at(n.sheet) == removedSheetId) invalidate(n);
    compactSheets(*copy);
    result.formula = copy;
    result.changed = result.valueAffected = true;
    return result;
}

bool looksLikeCellAddress(QStringView text)
{
    qsizetype i = text.startsWith(u'$') ? 1 : 0;
    const qsizetype lettersStart = i;
    while (i < text.size() && isAsciiLetter(text[i])) ++i;
    if (i == lettersStart || i - lettersStart > 3) return false;
    if (i < text.size() && text[i] == u'$') ++i;
    const qsizetype digitsStart = i;
    while (i < text.size() && isAsciiDigit(text[i])) ++i;
    return i == text.size() && i > digitsStart;
}

QString nameProblem(const QString &name)
{
    if (name.isEmpty() || name.size() > 255) return QStringLiteral("A name has 1 to 255 characters");
    if (!(isAsciiLetter(name.front()) || name.front() == u'_'))
        return QStringLiteral("A name starts with a letter or '_'");
    for (QChar c : name)
        if (!(isAsciiLetter(c) || isAsciiDigit(c) || c == u'_' || c == u'.'))
            return QStringLiteral("A name contains only letters, digits, '_' and '.'");
    const bool rc = name.size() == 1 && QStringLiteral("rRcC").contains(name.front());
    bool r1c1 = name.size() > 1 && (name.front() == u'R' || name.front() == u'r');
    if (r1c1) {
        const qsizetype c = name.indexOf(QLatin1Char('C'), 1, Qt::CaseInsensitive);
        r1c1 = c > 0 && std::all_of(name.begin() + 1, name.begin() + c, isAsciiDigit)
            && std::all_of(name.begin() + c + 1, name.end(), isAsciiDigit);
    }
    if (rc || r1c1 || looksLikeCellAddress(name))
        return QStringLiteral("A name cannot look like a cell reference");
    if (name.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0 || functionByName(name))
        return QStringLiteral("'%1' is reserved").arg(name);
    return {};
}

QStringList formulaNames(const Formula &formula)
{
    QStringList result;
    for (const Node &n : formula.nodes)
        if (n.kind == Node::Kind::Name && !result.contains(formula.strings.at(n.text), Qt::CaseInsensitive))
            result.append(formula.strings.at(n.text));
    return result;
}

} // namespace QindaTK::Calc
