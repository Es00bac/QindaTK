// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

#include <QUuid>
#include <algorithm>

namespace QindaTK {
using namespace Calc;

Workbook::Workbook(QObject *parent) : QObject(parent), d(std::make_unique<WorkbookPrivate>(this))
{
    d->documentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    d->createSheet(d->nextSheetId(), QStringLiteral("Sheet1"), 0, DefaultRows, DefaultColumns);
}
Workbook::~Workbook() = default;

QString Workbook::documentId() const { return d->documentId; }
QString Workbook::revision() const { return QString::number(d->revision); }
QString Workbook::resetGeneration() const { return QString::number(d->resetGeneration); }
int Workbook::sheetCount() const { return int(d->sheets.size()); }
bool Workbook::canUndo() const { return !d->txn && d->cursor > 0; }
bool Workbook::canRedo() const { return !d->txn && d->cursor < d->history.size(); }
QString Workbook::undoText() const { return d->cursor > 0 ? d->history[d->cursor - 1].label : QString(); }
QString Workbook::redoText() const
{ return d->cursor < d->history.size() ? d->history[d->cursor].label : QString(); }
bool Workbook::isModified() const { return d->cleanCursor != qint64(d->cursor); }
bool Workbook::transactionActive() const { return d->txn.has_value(); }
QString Workbook::lastError() const { return d->lastError; }
QVariantMap Workbook::lastErrorDetail() const { return d->lastErrorDetail; }
QVariantList Workbook::sheets() const
{
    QVariantList result;
    for (size_t i = 0; i < d->sheets.size(); ++i) {
        const Sheet &s = *d->sheets[i];
        const SheetProperties &p = s.features->properties;
        result.append(QVariantMap{{"id", s.id}, {"name", s.name}, {"index", int(i)},
                                  {"rows", s.rows}, {"columns", s.columns},
                                  {"cells", qint64(s.cells.size())}, {"formulas", s.formulaCount},
                                  {"tabColor", p.tabColor}, {"hidden", p.hidden}, {"protected", p.protectedSheet}});
    }
    return result;
}
QVariantMap Workbook::statistics() const
{
    qint64 cells = 0, formulas = 0;
    for (const auto &s : d->sheets) { cells += qint64(s->cells.size()); formulas += s->formulaCount; }
    return {{"sheets", int(d->sheets.size())}, {"cells", cells}, {"formulas", formulas},
            {"dependencyCells", qint64(d->cellDependents.size())},
            {"dependencyRanges", qint64(d->rangeDependents.size())},
            {"lastDirty", d->stats.dirty}, {"lastEvaluated", d->stats.evaluated},
            {"lastCycleCells", d->stats.cycleCells}, {"lastRecalcMicroseconds", d->stats.microseconds},
            {"totalEvaluations", d->stats.totalEvaluations},
            {"historySteps", qint64(d->history.size())}, {"historyCursor", qint64(d->cursor)}};
}

int WorkbookPrivate::indexOf(const Sheet *s) const
{
    for (size_t i = 0; i < sheets.size(); ++i) if (sheets[i].get() == s) return int(i);
    return -1;
}
QString WorkbookPrivate::nameForId(const QString &id) const
{
    const Sheet *s = sheet(id);
    return s ? s->name : QString();
}
QString WorkbookPrivate::idForName(const QString &name) const
{
    for (const auto &s : sheets)
        if (s->name.compare(name, Qt::CaseInsensitive) == 0) return s->id;
    return {};
}
QString WorkbookPrivate::nameProblem(const QString &name, const QString &ignoreSheetId) const
{
    const QString problem = sheetNameProblem(name);
    if (!problem.isEmpty()) return problem;
    for (const auto &s : sheets)
        if (s->id != ignoreSheetId && s->name.compare(name, Qt::CaseInsensitive) == 0)
            return QStringLiteral("A sheet named '%1' already exists").arg(s->name);
    return {};
}
QString WorkbookPrivate::nextSheetId()
{
    QString id;
    do id = QStringLiteral("sheet-%1").arg(nextSheetNumber++);
    while (byId.contains(id));
    return id;
}
QString WorkbookPrivate::defaultSheetName() const
{
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("Sheet%1").arg(n);
        if (idForName(candidate).isEmpty()) return candidate;
    }
}
Sheet *WorkbookPrivate::createSheet(const QString &id, const QString &name, int index, int rows, int columns)
{
    auto owned = std::make_unique<Sheet>();
    owned->id = id;
    owned->name = name;
    owned->uid = nextUid++;
    owned->rows = rows;
    owned->columns = columns;
    Sheet *s = owned.get();
    const int at = index < 0 || index > int(sheets.size()) ? int(sheets.size()) : index;
    sheets.insert(sheets.begin() + at, std::move(owned));
    byId.insert(s->id, s);
    byUid.insert(s->uid, s);
    return s;
}

bool WorkbookPrivate::parseInput(const QString &hostSheetId, const QVariant &input,
                                 CellContent *content, ParseError *error) const
{
    *content = {};
    if (!input.isValid() || input.isNull()) return true;
    const int type = input.metaType().id();
    if (type == QMetaType::Bool) {
        content->kind = CellContent::Kind::Literal;
        content->literal = Value::fromBool(input.toBool());
        return true;
    }
    if (type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
        || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float) {
        const Value v = Value::fromNumber(input.toDouble());
        if (v.isError()) { *error = {QStringLiteral("Numbers must be finite"), 0, 0}; return false; }
        content->kind = CellContent::Kind::Literal;
        content->literal = v;
        return true;
    }
    if (type != QMetaType::QString) {
        *error = {QStringLiteral("Unsupported cell value type"), 0, 0};
        return false;
    }
    const QString text = input.toString();
    if (text.isEmpty()) return true;
    if (text.startsWith(QLatin1Char('='))) {
        const ParseResult parsed = parseFormula(text, [this](const QString &name) { return idForName(name); },
                                                MaxFormulaLength, [this](const QString &name) {
                                                    const NamedRange *entry = findName(*names, name);
                                                    return entry ? entry->name : QString();
                                                });
        if (!parsed.ok()) { *error = parsed.error; return false; }
        Q_UNUSED(hostSheetId);
        content->kind = CellContent::Kind::Formula;
        content->formula = parsed.formula;
        return true;
    }
    content->kind = CellContent::Kind::Literal;
    double number = 0.0;
    if (text.startsWith(QLatin1Char('\''))) {
        if (text.size() == 1) { *content = {}; return true; }
        content->literal = Value::fromText(text.mid(1));
    } else if (text.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
               || text.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0) {
        content->literal = Value::fromBool(text.size() == 4);
    } else if (const ErrorKind e = errorFromText(text); e != ErrorKind::None) {
        content->literal = Value::fromError(e);
    } else if (parseNumber(text, &number)) {
        content->literal = Value::fromNumber(number);
    } else {
        content->literal = Value::fromText(text);
    }
    if (content->literal.isText() && content->literal.text.size() > Workbook::MaxTextLength) {
        *error = {QStringLiteral("Cell text is longer than %1 characters").arg(Workbook::MaxTextLength),
                  qint32(Workbook::MaxTextLength), qint32(text.size())};
        *content = {};
        return false;
    }
    return true;
}

QString WorkbookPrivate::inputText(const Sheet &s, const CellContent &content) const
{
    Q_UNUSED(s);
    switch (content.kind) {
    case CellContent::Kind::Empty: return {};
    case CellContent::Kind::Formula:
        return renderFormula(*content.formula, [this](const QString &id) { return nameForId(id); });
    case CellContent::Kind::Literal: break;
    }
    const Value &v = content.literal;
    if (v.isNumber()) return numberLiteral(v.number);
    if (!v.isText()) return displayText(v);
    double ignored = 0.0;
    const bool escape = v.text.startsWith(QLatin1Char('=')) || v.text.startsWith(QLatin1Char('\''))
        || v.text.compare(QLatin1StringView("TRUE"), Qt::CaseInsensitive) == 0
        || v.text.compare(QLatin1StringView("FALSE"), Qt::CaseInsensitive) == 0
        || errorFromText(v.text) != ErrorKind::None || parseNumber(v.text, &ignored);
    return escape ? QLatin1Char('\'') + v.text : v.text;
}

bool WorkbookPrivate::fail(const QString &message, const QVariantMap &detail)
{
    lastError = message;
    lastErrorDetail = detail;
    lastErrorDetail.insert(QStringLiteral("message"), message);
    emit q->errorChanged();
    emit q->rejected(message);
    return false;
}
void WorkbookPrivate::clearError()
{
    if (lastError.isEmpty() && lastErrorDetail.isEmpty()) return;
    lastError.clear();
    lastErrorDetail.clear();
    emit q->errorChanged();
}

bool WorkbookPrivate::begin(const QString &label)
{
    if (txn) return fail(QStringLiteral("A workbook transaction is already active"));
    txn.emplace();
    txn->label = label.isEmpty() ? QStringLiteral("Edit") : label;
    return true;
}
bool WorkbookPrivate::record(Mutation m, QString *error)
{
    MutationList inverse;
    if (!apply(m, &inverse, error)) return false;
    txn->inverses.push_back(std::move(inverse));
    return true;
}
void WorkbookPrivate::rollbackTo(size_t mark)
{
    while (txn && txn->inverses.size() > mark) {
        const MutationList group = std::move(txn->inverses.back());
        txn->inverses.pop_back();
        for (const Mutation &m : group) {
            MutationList ignored;
            QString error;
            // AGENT-GUARD: an inverse re-applies a state that existed, so it
            // cannot fail validation; a failure here means a lost invariant.
            if (!apply(m, &ignored, &error)) qWarning("Workbook rollback failed: %s", qPrintable(error));
        }
    }
}
namespace {
MutationList flattenReversed(const std::vector<MutationList> &groups)
{
    MutationList result;
    for (auto it = groups.rbegin(); it != groups.rend(); ++it)
        result.insert(result.end(), it->begin(), it->end());
    return result;
}
}
bool WorkbookPrivate::commit()
{
    if (!txn) return fail(QStringLiteral("No active workbook transaction"));
    Transaction t = std::move(*txn);
    txn.reset();
    const bool any = std::any_of(t.inverses.begin(), t.inverses.end(),
                                 [](const MutationList &group) { return !group.empty(); });
    if (!any) {
        emit q->transactionChanged();
        emit q->historyChanged();
        return true;
    }
    QHash<quint32, Bounds> changed = t.touched;
    recalculate(t.seeds, &changed);
    Step step;
    step.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    step.label = t.label;
    step.undo = flattenReversed(t.inverses);
    history.resize(cursor);
    if (cleanCursor > qint64(cursor)) cleanCursor = -1;
    history.push_back(std::move(step));
    ++cursor;
    if (history.size() > size_t(Workbook::MaxHistorySteps)) {
        history.erase(history.begin());
        --cursor;
        cleanCursor = cleanCursor > 0 ? cleanCursor - 1 : -1;
    }
    ++revision;
    finish(t.label, true, changed, t);
    return true;
}
void WorkbookPrivate::cancel()
{
    if (!txn) return;
    rollbackTo(0);
    const QSet<quint64> seeds = txn->seeds;
    txn.reset();
    if (!seeds.isEmpty()) recalculate(seeds, nullptr); // restores cached results of re-set formulas
    emit q->transactionChanged();
    emit q->historyChanged();
}
bool WorkbookPrivate::replay(bool undoing)
{
    if (txn) return fail(QStringLiteral("Finish the active workbook transaction first"));
    if (undoing ? cursor == 0 : cursor >= history.size())
        return fail(undoing ? QStringLiteral("Nothing to undo") : QStringLiteral("Nothing to redo"));
    Step &step = history[undoing ? cursor - 1 : cursor];
    txn.emplace();
    txn->label = step.label;
    for (const Mutation &m : undoing ? step.undo : step.redo) {
        QString error;
        if (!record(m, &error)) {
            rollbackTo(0);
            const QSet<quint64> seeds = txn->seeds;
            txn.reset();
            if (!seeds.isEmpty()) recalculate(seeds, nullptr);
            return fail(QStringLiteral("History replay failed: %1").arg(error));
        }
    }
    Transaction t = std::move(*txn);
    txn.reset();
    (undoing ? step.redo : step.undo) = flattenReversed(t.inverses);
    QHash<quint32, Bounds> changed = t.touched;
    recalculate(t.seeds, &changed);
    cursor = undoing ? cursor - 1 : cursor + 1;
    ++revision;
    clearError();
    finish(step.label, true, changed, t);
    return true;
}
void WorkbookPrivate::finish(const QString &label, bool emitChanges, QHash<quint32, Bounds> changed,
                             const Transaction &t)
{
    Q_UNUSED(label);
    if (emitChanges) {
        for (const StructuralNotice &n : t.structural) {
            if (n.rows) emit n.insert ? q->rowsInserted(n.sheetId, n.at, n.count) : q->rowsRemoved(n.sheetId, n.at, n.count);
            else emit n.insert ? q->columnsInserted(n.sheetId, n.at, n.count) : q->columnsRemoved(n.sheetId, n.at, n.count);
        }
        if (t.sheetsChanged || !t.propertySheets.isEmpty()) emit q->sheetsChanged();
        for (const QString &id : t.propertySheets) if (byId.contains(id)) emit q->sheetPropertiesChanged(id);
        if (t.namesChanged) emit q->namesChanged();
        for (const auto &s : sheets) {
            const auto it = changed.constFind(s->uid);
            if (it == changed.cend() || it->lastRow < 0) continue;
            emit q->cellsChanged(s->id, it->firstRow, it->firstColumn,
                                 std::min(it->lastRow, s->rows - 1), std::min(it->lastColumn, s->columns - 1));
        }
    }
    emit q->revisionChanged();
    emit q->historyChanged();
    emit q->transactionChanged();
}

bool Workbook::beginTransaction(const QString &label)
{
    if (!d->begin(label)) return false;
    d->clearError();
    emit transactionChanged();
    emit historyChanged();
    return true;
}
bool Workbook::commitTransaction() { return d->commit(); }
bool Workbook::cancelTransaction()
{
    if (!d->txn) return d->fail(QStringLiteral("No active workbook transaction"));
    d->cancel();
    return true;
}
bool Workbook::undo() { return d->replay(true); }
bool Workbook::redo() { return d->replay(false); }
void Workbook::markSaved()
{
    if (d->cleanCursor == qint64(d->cursor)) return;
    d->cleanCursor = qint64(d->cursor);
    emit historyChanged();
}

QString Workbook::addSheet(const QString &name, int index, int rows, int columns)
{
    QString created;
    d->mutate(QStringLiteral("Add sheet"), [&](QString *error, QVariantMap *) {
        auto state = std::make_shared<SheetState>();
        state->name = name.isEmpty() ? d->defaultSheetName() : name;
        state->index = index < 0 || index > int(d->sheets.size()) ? int(d->sheets.size()) : index;
        state->rows = rows;
        state->columns = columns;
        state->id = d->nextSheetId();
        Mutation m;
        m.kind = Mutation::Kind::AddSheet;
        m.state = state;
        if (!d->record(std::move(m), error)) return false;
        created = state->id;
        return true;
    });
    return created;
}
bool Workbook::removeSheet(const QString &sheetId)
{
    const QString label = QStringLiteral("Remove sheet %1").arg(d->nameForId(sheetId));
    return d->mutate(label, [&](QString *error, QVariantMap *) {
        const bool otherVisible = std::any_of(d->sheets.begin(), d->sheets.end(), [&](const auto &s) {
            return s->id != sheetId && !s->features->properties.hidden;
        });
        if (d->sheet(sheetId) && !otherVisible) {
            *error = QStringLiteral("A workbook keeps at least one visible sheet");
            return false;
        }
        Mutation m;
        m.kind = Mutation::Kind::RemoveSheet;
        m.sheetId = sheetId;
        return d->record(std::move(m), error);
    });
}
bool Workbook::renameSheet(const QString &sheetId, const QString &name)
{
    return d->mutate(QStringLiteral("Rename sheet"), [&](QString *error, QVariantMap *) {
        Mutation m;
        m.kind = Mutation::Kind::RenameSheet;
        m.sheetId = sheetId;
        m.name = name;
        return d->record(std::move(m), error);
    });
}
bool Workbook::moveSheet(const QString &sheetId, int index)
{
    return d->mutate(QStringLiteral("Move sheet"), [&](QString *error, QVariantMap *) {
        Mutation m;
        m.kind = Mutation::Kind::MoveSheet;
        m.sheetId = sheetId;
        m.at = index;
        return d->record(std::move(m), error);
    });
}
QString Workbook::sheetIdAt(int index) const
{ return index >= 0 && index < int(d->sheets.size()) ? d->sheets[size_t(index)]->id : QString(); }
int Workbook::sheetIndex(const QString &sheetId) const
{ const Sheet *s = d->sheet(sheetId); return s ? d->indexOf(s) : -1; }
QString Workbook::sheetName(const QString &sheetId) const { return d->nameForId(sheetId); }
QString Workbook::sheetIdForName(const QString &name) const { return d->idForName(name); }
int Workbook::rowCount(const QString &sheetId) const
{ const Sheet *s = d->sheet(sheetId); return s ? s->rows : 0; }
int Workbook::columnCount(const QString &sheetId) const
{ const Sheet *s = d->sheet(sheetId); return s ? s->columns : 0; }

bool Workbook::setCellInput(const QString &sheetId, int row, int column, const QString &input)
{
    return d->mutate(QStringLiteral("Edit %1").arg(cellAddress(row, column)),
                     [&](QString *error, QVariantMap *detail) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (row < 0 || row >= s->rows || column < 0 || column >= s->columns) {
            *error = QStringLiteral("Cell is outside the sheet");
            return false;
        }
        CellContent content;
        ParseError parse;
        if (!d->parseInput(sheetId, input, &content, &parse)) {
            *error = parse.message;
            *detail = {{"sheetId", sheetId}, {"row", row}, {"column", column},
                       {"address", cellAddress(row, column)}, {"start", parse.start}, {"end", parse.end}};
            return false;
        }
        const ValidationRule *rule = nullptr;
        if (const QString problem = d->validationProblem(*s, row, column, content, &rule);
            !problem.isEmpty() && rule->errorStyle == ValidationRule::ErrorStyle::Stop) {
            *error = problem;
            *detail = {{"sheetId", sheetId}, {"row", row}, {"column", column}, {"address", cellAddress(row, column)},
                       {"validation", true}, {"ruleId", rule->id}, {"title", rule->errorTitle}};
            return false;
        }
        Mutation m;
        m.sheetId = sheetId;
        m.cells.push_back({row, column, std::move(content)});
        return d->record(std::move(m), error);
    });
}
QString Workbook::cellInput(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    return s ? d->inputText(*s, d->contentAt(*s, row, column)) : QString();
}
QString Workbook::cellDisplay(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    const Cell *c = s ? d->findCell(*s, row, column) : nullptr;
    return c ? d->displayAt(*s, row, column, c->value) : QString();
}
QVariant Workbook::cellValue(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    const Cell *c = s ? d->findCell(*s, row, column) : nullptr;
    return c ? toVariant(c->value) : QVariant();
}
QString Workbook::cellType(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    const Cell *c = s ? d->findCell(*s, row, column) : nullptr;
    return typeName(c ? c->value.type : Value::Type::Blank);
}
QVariantMap Workbook::cellInfo(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s || row < 0 || column < 0 || row >= s->rows || column >= s->columns) return {};
    const Cell *c = d->findCell(*s, row, column);
    const Value value = c ? c->value : Value::blank();
    return {{"sheetId", sheetId}, {"row", row}, {"column", column}, {"address", cellAddress(row, column)},
            {"input", c ? d->inputText(*s, c->content) : QString()}, {"display", d->displayAt(*s, row, column, value)},
            {"type", typeName(value.type)}, {"value", toVariant(value)},
            {"formula", c && c->content.kind == CellContent::Kind::Formula},
            {"error", value.isError() ? errorText(value.error) : QString()}};
}

} // namespace QindaTK
