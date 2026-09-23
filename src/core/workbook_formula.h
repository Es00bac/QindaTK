// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "workbook_value.h"
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

// Formula grammar, AST, rendering and reference algebra for the workbook
// engine. The grammar and function list are documented in docs/workbook.md.
namespace QindaTK::Calc {

inline constexpr int MaxRows = 1048576;          // 2^20, A1..A1048576
inline constexpr int MaxColumns = 16384;         // 2^14, A..XFD
inline constexpr int MaxFormulaLength = 8192;    // UTF-16 code units, including '='
inline constexpr int MaxFormulaNesting = 64;     // parentheses, calls and unary prefixes
inline constexpr int MaxFormulaDepth = 1024;     // AST height (bounds evaluator recursion)
inline constexpr int MaxFormulaNodes = 4096;
inline constexpr int MaxFunctionArguments = 255;

enum class Function : quint8 { Sum, Average, Min, Max, Count, CountA, If, IfError, And, Or, Not, Abs, Round };
struct FunctionSpec {
    Function id;
    const char *name;
    int minimumArguments;
    int maximumArguments;
    const char *signature;
};
const FunctionSpec *functionByName(QStringView name);
const FunctionSpec &functionSpec(Function id);
QStringList functionNames();

enum class Op : quint8 { Add, Sub, Mul, Div, Pow, Concat, Eq, Ne, Lt, Le, Gt, Ge, Neg, Plus, Percent };

// A reference stores its absolute target coordinates; the anchor flags only
// decide what copy/fill offsets move and how `$` renders.
struct RefPoint {
    qint32 row = 0;
    qint32 column = 0;
    bool rowAbsolute = false;
    bool columnAbsolute = false;
    friend bool operator==(const RefPoint &, const RefPoint &) = default;
};

struct Node {
    enum class Kind : quint8 { Number, Text, Boolean, Error, Cell, Range, Unary, Binary, Call, Group };
    Kind kind = Kind::Number;
    Op op = Op::Add;
    Function function = Function::Sum;
    bool boolean = false;
    ErrorKind error = ErrorKind::None;
    qint32 sheet = -1;   // index in Formula::sheets; -1 = the formula's own sheet
    qint32 a = -1;       // Unary/Group child, Binary left, Call first index in Formula::args
    qint32 b = -1;       // Binary right, Call argument count
    qint32 text = -1;    // index in Formula::strings
    double number = 0.0;
    RefPoint first;      // Cell target or Range top-left (normalized per axis)
    RefPoint last;       // Range bottom-right
    qint32 start = 0;    // source span in the parsed text; stale after rewrites
    qint32 end = 0;
};

// Immutable once shared: rewrites produce a new Formula.
struct Formula {
    std::vector<Node> nodes;
    std::vector<qint32> args;
    QStringList strings;
    QStringList sheets;  // stable sheet IDs referenced explicitly
    qint32 root = -1;
    // Structural equality; spans are ignored.
    bool sameAs(const Formula &other) const;
};
using FormulaPtr = std::shared_ptr<const Formula>;

struct ParseError {
    QString message;
    qint32 start = 0;
    qint32 end = 0;
};
struct ParseResult {
    FormulaPtr formula;
    ParseError error;
    bool ok() const { return formula != nullptr; }
};
// Resolves a user-facing sheet name to its stable ID ("" when unknown).
using SheetIdLookup = std::function<QString(const QString &name)>;
using SheetNameLookup = std::function<QString(const QString &sheetId)>;

// `text` must begin with '='. Errors carry a UTF-16 span into `text`.
// Stored formulas may render longer than typed ones after reference
// rewrites, so the native codec passes a larger `maximumLength`.
ParseResult parseFormula(const QString &text, const SheetIdLookup &sheetIdForName,
                         int maximumLength = MaxFormulaLength);
// Canonical text: upper-case names/references, no whitespace, explicit
// parentheses preserved, shortest round-trip numbers, quoted sheet names.
QString renderFormula(const Formula &formula, const SheetNameLookup &sheetNameForId);

QString columnName(int column);
int columnFromName(QStringView letters);
QString cellAddress(int row, int column);
QString refPointText(const RefPoint &point);
// Plain or anchored A1 cell address within engine bounds.
bool parseCellAddress(QStringView text, RefPoint *point);
QString quoteSheetName(const QString &name);
// Sheet names: 1-31 code units, none of []:*?/\ and no leading/trailing '.
QString sheetNameProblem(const QString &name);

// Referenced area of one AST reference, already resolved to a sheet ID.
struct Precedent {
    QString sheetId;
    qint32 firstRow = 0, firstColumn = 0, lastRow = 0, lastColumn = 0;
    bool range = false;
};
void collectPrecedents(const Formula &formula, const QString &hostSheetId,
                       std::vector<Precedent> *out);

// Copy/fill: relative parts move by the offset; absolute parts stay.
// A reference moved outside the engine grid becomes #REF!.
FormulaPtr offsetFormula(const FormulaPtr &formula, int rowOffset, int columnOffset);

struct StructuralEdit {
    QString sheetId;
    bool rows = true;      // rows or columns
    bool insert = true;    // insertion or deletion
    int at = 0;            // first inserted/deleted index
    int count = 0;
};
struct Rewrite {
    FormulaPtr formula;          // unchanged pointer when nothing changed
    bool changed = false;
    bool valueAffected = false;  // an invalidated reference or a shrunken range
};
// Documented rule: every reference into the edited sheet keeps its target
// cell; deleting a single target invalidates it (#REF!); a range loses the
// deleted band and is invalidated only when the whole range is deleted.
Rewrite rewriteForStructure(const FormulaPtr &formula, const QString &hostSheetId,
                            const StructuralEdit &edit);
// Every reference to a removed sheet becomes #REF!.
Rewrite invalidateSheet(const FormulaPtr &formula, const QString &removedSheetId);

// Read access for evaluation. visitRange reports the non-blank cells of the
// rectangle in row-major order and stops when the visitor returns false.
class EvalContext {
public:
    virtual ~EvalContext() = default;
    virtual Value cell(const QString &sheetId, int row, int column) const = 0;
    virtual void visitRange(const QString &sheetId, int firstRow, int firstColumn,
                            int lastRow, int lastColumn,
                            const std::function<bool(const Value &)> &visitor) const = 0;
};
// Deterministic evaluation. A blank result is returned as the number 0.
Value evaluateFormula(const Formula &formula, const QString &hostSheetId,
                      const EvalContext &context);

} // namespace QindaTK::Calc
