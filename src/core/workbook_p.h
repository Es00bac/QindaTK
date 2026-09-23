// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Private workbook engine state shared by workbook*.cpp. Not installed.

#include "workbook.h"
#include "workbook_features_p.h"
#include "workbook_rules_p.h"
#include <QHash>
#include <QSet>
#include <map>
#include <optional>

namespace QindaTK {
namespace Calc {
struct DecodedWorkbook;

struct CellContent {
    enum class Kind : quint8 { Empty, Literal, Formula };
    Kind kind = Kind::Empty;
    Value literal;
    FormulaPtr formula;
    bool sameAs(const CellContent &other) const;
};
struct Cell {
    CellContent content;
    Value value; // literal copy or last computed formula result
};

// Row-major position key: row in the high 20 bits, column in the low 14.
inline quint64 positionKey(int row, int column) { return (quint64(row) << 14) | quint64(column); }
inline int keyRow(quint64 key) { return int((key >> 14) & 0xfffff); }
inline int keyColumn(quint64 key) { return int(key & 0x3fff); }
// Workbook-wide key: internal sheet uid above the 34 position bits.
inline quint64 globalKey(quint32 uid, quint64 position) { return (quint64(uid) << 34) | position; }
inline quint32 keyUid(quint64 key) { return quint32(key >> 34); }
inline quint64 keyPosition(quint64 key) { return key & ((quint64(1) << 34) - 1); }

struct Sheet {
    QString id;
    QString name;
    quint32 uid = 0; // session-local dependency key; never persisted
    int rows = 0;
    int columns = 0;
    std::map<quint64, Cell> cells; // ordered row-major for deterministic ranges
    int formulaCount = 0;
    FeaturesPtr features = emptyFeatures();
};

struct CellEntry {
    qint32 row = 0;
    qint32 column = 0;
    CellContent content;
};
struct SheetState {
    QString id;
    QString name;
    qint32 index = 0;
    qint32 rows = 0;
    qint32 columns = 0;
    std::vector<CellEntry> cells;
    FeaturesPtr features;
};

// A primitive, exactly invertible change. apply() returns its inverse.
struct Mutation {
    enum class Kind : quint8 {
        SetCells, InsertRows, RemoveRows, InsertColumns, RemoveColumns,
        AddSheet, RemoveSheet, RenameSheet, MoveSheet, SetFeatures, SetNames
    };
    Kind kind = Kind::SetCells;
    QString sheetId;
    qint32 at = 0;    // structural index, or MoveSheet target index
    qint32 count = 0;
    QString name;     // RenameSheet
    std::vector<CellEntry> cells;             // SetCells
    std::shared_ptr<const SheetState> state;  // AddSheet
    FeaturesPtr features;                     // SetFeatures
    NamesPtr names;                           // SetNames
};
using MutationList = std::vector<Mutation>;

struct RangeKey {
    quint32 sheet = 0;
    qint32 firstRow = 0, firstColumn = 0, lastRow = 0, lastColumn = 0;
    friend bool operator==(const RangeKey &, const RangeKey &) = default;
};
inline size_t qHash(const RangeKey &key, size_t seed = 0)
{
    return qHashMulti(seed, key.sheet, key.firstRow, key.firstColumn, key.lastRow, key.lastColumn);
}

struct Bounds {
    int firstRow = 0, firstColumn = 0, lastRow = -1, lastColumn = -1;
    void include(int row, int column);
};
struct StructuralNotice {
    QString sheetId;
    bool rows = true;
    bool insert = true;
    int at = 0;
    int count = 0;
};

} // namespace Calc

struct WorkbookPrivate final : Calc::EvalContext {
    using Cell = Calc::Cell;
    using CellContent = Calc::CellContent;
    using Mutation = Calc::Mutation;
    using MutationList = Calc::MutationList;
    using Sheet = Calc::Sheet;

    struct Transaction {
        QString label;
        std::vector<MutationList> inverses; // one group per applied mutation
        QSet<quint64> seeds;                // global keys needing recalculation
        QHash<quint32, Calc::Bounds> touched;
        std::vector<Calc::StructuralNotice> structural;
        QSet<QString> propertySheets;       // sheet properties changed
        bool sheetsChanged = false;
        bool namesChanged = false;
    };
    struct Step {
        QString id;
        QString label;
        MutationList undo;
        MutationList redo;
    };
    struct RecalcStats {
        qint64 dirty = 0, evaluated = 0, cycleCells = 0, microseconds = 0;
        qint64 totalEvaluations = 0;
    };

    explicit WorkbookPrivate(Workbook *owner) : q(owner) {}

    // EvalContext
    Calc::Value cell(const QString &sheetId, int row, int column) const override;
    void visitRange(const QString &sheetId, int firstRow, int firstColumn, int lastRow,
                    int lastColumn, const std::function<bool(const Calc::Value &)> &visitor) const override;
    bool resolveName(const QString &name, Calc::Precedent *target) const override;
    // Stored cells of a rectangle in row-major order with their position keys.
    void visitCells(const Sheet &s, int firstRow, int firstColumn, int lastRow, int lastColumn,
                    const std::function<bool(quint64, const Calc::Value &)> &visitor) const;

    Sheet *sheet(const QString &id) const { return byId.value(id, nullptr); }
    int indexOf(const Sheet *s) const;
    const Cell *findCell(const Sheet &s, int row, int column) const;
    CellContent contentAt(const Sheet &s, int row, int column) const;
    QString inputText(const Sheet &s, const CellContent &content) const;
    bool parseInput(const QString &hostSheetId, const QVariant &input, CellContent *content,
                    Calc::ParseError *error) const;
    QString nameForId(const QString &id) const;
    QString idForName(const QString &name) const;
    Sheet *createSheet(const QString &id, const QString &name, int index, int rows, int columns);
    QString nextSheetId();
    QString defaultSheetName() const;
    QString nameProblem(const QString &name, const QString &ignoreSheetId) const;

    // Content and dependency index (workbook_recalc.cpp)
    void setContent(Sheet &s, int row, int column, const CellContent &content);
    void registerFormula(quint64 key, const QString &hostId, const Calc::Formula &formula, bool add);
    void rebuildDependencies();
    void dependentsOf(quint64 key, std::vector<quint64> *out) const;
    void recalculate(const QSet<quint64> &seeds, QHash<quint32, Calc::Bounds> *changed);

    // Mutations (workbook_mutations.cpp)
    bool apply(const Mutation &m, MutationList *inverse, QString *error);
    bool applyStructural(const Mutation &m, MutationList *inverse, QString *error);
    bool applySheetChange(const Mutation &m, MutationList *inverse, QString *error);
    bool applyFeatures(const Mutation &m, MutationList *inverse, QString *error);
    void seedFormulas(const Sheet &s);
    void seedNameUsers();
    // Features of every sheet and the name table through a structural edit
    // or a sheet removal; appends the exact restoring mutations to `inverse`.
    void transformSheetFeatures(const Calc::StructuralEdit *edit, const QString &removedSheetId,
                                MutationList *inverse);
    // Feature edits (workbook_sheetops.cpp): records one SetFeatures step.
    bool recordFeatures(Sheet &s, Calc::FeaturesPtr features, QString *error);
    bool sheetEditable(const Sheet &s, QString *error) const;
    bool copyFormats(const Sheet &source, const Calc::Area &block, Sheet &target, const Calc::Area &to,
                     int originRow, int originColumn, bool withMerges, QString *error);
    Calc::RuleLookups lookups(const Sheet &s) const;

    // Presentation (workbook_styles.cpp)
    Calc::CellStyle explicitStyle(const Sheet &s, int row, int column) const;
    QString displayAt(const Sheet &s, int row, int column, const Calc::Value &value) const;
    // `features` overrides the sheet's own (previews); caches only for the sheet's own.
    QVariantMap presentation(const Sheet &s, int row, int column, const Calc::SheetFeatures *features = nullptr) const;
    Calc::Value operandValue(const Sheet &s, const Calc::RuleOperand &operand, const Calc::Area &anchor,
                             int row, int column, const Calc::EvalContext *context = nullptr) const;

    // Validation and input lists (workbook_validation.cpp)
    struct InputEntry { int row = 0; int column = 0; QVariant input; };
    const Calc::ValidationRule *validationRuleAt(const Sheet &s, int row, int column) const;
    // "" when `candidate` satisfies the cell's rule (or it has none).
    QString validationProblem(const Sheet &s, int row, int column, const CellContent &candidate,
                              const Calc::ValidationRule **rule) const;
    bool applyInputs(const QString &label, const QString &sheetId, const std::vector<InputEntry> &entries,
                     const QString &policy);

    // Transactions (workbook.cpp)
    bool fail(const QString &message, const QVariantMap &detail = {});
    void clearError();
    bool begin(const QString &label);
    bool record(Mutation m, QString *error);
    void rollbackTo(size_t mark);
    bool commit();
    void cancel();
    bool replay(bool undo);
    void finish(const QString &label, bool emitChanges, QHash<quint32, Calc::Bounds> changed,
                const Transaction &txn);
    template <typename Body> bool mutate(const QString &label, Body body);
    // Replaces the whole document (restore/reset); clears history.
    void adopt(Calc::DecodedWorkbook &&data);

    Workbook *q;
    QString documentId;
    std::vector<std::unique_ptr<Sheet>> sheets;
    QHash<QString, Sheet *> byId;
    QHash<quint32, Sheet *> byUid;
    Calc::NamesPtr names = std::make_shared<const Calc::NameTable>();
    quint32 nextUid = 1;
    quint64 nextSheetNumber = 1;
    quint64 revision = 0;
    quint64 resetGeneration = 0;

    QHash<quint64, QSet<quint64>> cellDependents;
    QHash<Calc::RangeKey, QSet<quint64>> rangeDependents;
    QHash<quint64, QSet<Calc::RangeKey>> columnRanges; // (uid << 14 | column)
    QHash<quint32, QSet<Calc::RangeKey>> wideRanges;

    std::optional<Transaction> txn;
    std::vector<Step> history;
    size_t cursor = 0;
    qint64 cleanCursor = 0; // -1 once the saved state left the history
    RecalcStats stats;
    QUrl fileUrl;
    QByteArray fileSha256;
    QString lastError;
    QVariantMap lastErrorDetail;
    QVariantMap pasteReport;
    struct ScaleStats { quint64 revision = 0; bool any = false; double minimum = 0, maximum = 0; };
    mutable QHash<QString, ScaleStats> scaleCache; // sheetId U+001F ruleId
};

template <typename Body>
bool WorkbookPrivate::mutate(const QString &label, Body body)
{
    const bool implicit = !txn.has_value();
    if (implicit && !begin(label)) return false;
    const size_t mark = txn->inverses.size();
    QString error;
    QVariantMap detail;
    if (!body(&error, &detail)) {
        rollbackTo(mark);
        if (implicit) cancel();
        return fail(error, detail);
    }
    clearError();
    return implicit ? commit() : true;
}

} // namespace QindaTK
