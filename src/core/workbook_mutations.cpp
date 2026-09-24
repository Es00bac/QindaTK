// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

#include <algorithm>

namespace QindaTK {
using namespace Calc;
namespace {
bool setError(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool formulaSheetsExist(const WorkbookPrivate &d, const CellContent &content)
{
    if (content.kind != CellContent::Kind::Formula || !content.formula) return true;
    for (const QString &id : content.formula->sheets)
        if (!d.byId.contains(id)) return false;
    return true;
}
bool usesNames(const Formula &formula)
{
    return std::any_of(formula.nodes.begin(), formula.nodes.end(),
                       [](const Node &n) { return n.kind == Node::Kind::Name; });
}
void includeFeatureAreas(Bounds &bounds, const SheetFeatures &features)
{
    const auto add = [&](const Area &a) { bounds.include(a.firstRow, a.firstColumn); bounds.include(a.lastRow, a.lastColumn); };
    for (const FormatLayer &layer : features.formats) add(layer.area);
    for (const Area &area : features.merges) add(area);
    for (const ConditionalRule &rule : features.conditional) for (const Area &a : rule.areas) add(a);
    for (const ValidationRule &rule : features.validations) for (const Area &a : rule.areas) add(a);
}
} // namespace

void WorkbookPrivate::seedNameUsers()
{
    if (!txn) return;
    for (const auto &s : sheets)
        for (const auto &[position, c] : s->cells)
            if (c.content.kind == CellContent::Kind::Formula && usesNames(*c.content.formula))
                txn->seeds.insert(globalKey(s->uid, position));
}

bool WorkbookPrivate::applyFeatures(const Mutation &m, MutationList *inverse, QString *error)
{
    if (m.kind == Mutation::Kind::SetNames) {
        if (!m.names) return setError(error, QStringLiteral("Missing name table"));
        Mutation undo;
        undo.kind = Mutation::Kind::SetNames;
        undo.names = names;
        names = m.names;
        rebuildDependencies();
        seedNameUsers();
        if (txn) txn->namesChanged = true;
        inverse->push_back(std::move(undo));
        return true;
    }
    Sheet *s = sheet(m.sheetId);
    if (!s) return setError(error, QStringLiteral("Unknown sheet"));
    if (!m.features) return setError(error, QStringLiteral("Missing sheet features"));
    if (txn) {
        includeFeatureAreas(txn->touched[s->uid], *s->features);
        includeFeatureAreas(txn->touched[s->uid], *m.features);
        if (!(s->features->properties == m.features->properties)) txn->propertySheets.insert(s->id);
    }
    Mutation undo;
    undo.kind = Mutation::Kind::SetFeatures;
    undo.sheetId = s->id;
    undo.features = s->features;
    s->features = m.features;
    inverse->push_back(std::move(undo));
    return true;
}

void WorkbookPrivate::transformSheetFeatures(const StructuralEdit *edit, const QString &removedSheetId,
                                             MutationList *inverse)
{
    for (const auto &s : sheets) {
        bool changed = false;
        const FeaturesPtr next = edit ? transformFeatures(s->features, s->id, *edit, &changed)
                                      : invalidateFeatureSheet(s->features, removedSheetId, &changed);
        if (!changed) continue;
        Mutation restore;
        restore.kind = Mutation::Kind::SetFeatures;
        restore.sheetId = s->id;
        restore.features = s->features;
        if (txn && !(s->features->properties == next->properties)) txn->propertySheets.insert(s->id);
        s->features = next;
        inverse->push_back(std::move(restore));
    }
    bool changed = false;
    const NamesPtr next = edit ? transformNames(names, *edit, &changed)
                               : invalidateNameSheet(names, removedSheetId, &changed);
    if (!changed) return;
    Mutation restore;
    restore.kind = Mutation::Kind::SetNames;
    restore.names = names;
    names = next;
    if (txn) txn->namesChanged = true;
    inverse->push_back(std::move(restore));
}

CellContent WorkbookPrivate::contentAt(const Sheet &s, int row, int column) const
{
    const Cell *c = findCell(s, row, column);
    return c ? c->content : CellContent{};
}

void WorkbookPrivate::seedFormulas(const Sheet &s)
{
    if (!txn) return;
    for (const auto &[position, c] : s.cells)
        if (c.content.kind == CellContent::Kind::Formula) txn->seeds.insert(globalKey(s.uid, position));
}

bool WorkbookPrivate::apply(const Mutation &m, MutationList *inverse, QString *error)
{
    switch (m.kind) {
    case Mutation::Kind::SetCells: {
        Sheet *s = sheet(m.sheetId);
        if (!s) return setError(error, QStringLiteral("Unknown sheet"));
        for (const CellEntry &e : m.cells) {
            if (e.row < 0 || e.row >= s->rows || e.column < 0 || e.column >= s->columns)
                return setError(error, QStringLiteral("Cell %1 is outside the sheet").arg(cellAddress(e.row, e.column)));
            if (!formulaSheetsExist(*this, e.content))
                return setError(error, QStringLiteral("A formula refers to a sheet that does not exist"));
        }
        Mutation undo;
        undo.kind = Mutation::Kind::SetCells;
        undo.sheetId = m.sheetId;
        for (const CellEntry &e : m.cells) {
            CellContent old = contentAt(*s, e.row, e.column);
            if (old.sameAs(e.content)) continue;
            undo.cells.push_back({e.row, e.column, std::move(old)});
            setContent(*s, e.row, e.column, e.content);
        }
        if (!undo.cells.empty()) {
            std::reverse(undo.cells.begin(), undo.cells.end()); // replays duplicates correctly
            inverse->push_back(std::move(undo));
        }
        return true;
    }
    case Mutation::Kind::InsertRows:
    case Mutation::Kind::RemoveRows:
    case Mutation::Kind::InsertColumns:
    case Mutation::Kind::RemoveColumns:
        return applyStructural(m, inverse, error);
    case Mutation::Kind::SetFeatures:
    case Mutation::Kind::SetNames:
        return applyFeatures(m, inverse, error);
    default:
        return applySheetChange(m, inverse, error);
    }
}

// AGENT-GUARD: references are rewritten at their original positions before
// cells move, so every recorded pre-image is keyed by the coordinate it will
// occupy again after the opposite structural mutation runs. Reordering these
// steps makes undo restore formulas into the wrong cells.
bool WorkbookPrivate::applyStructural(const Mutation &m, MutationList *inverse, QString *error)
{
    Sheet *s = sheet(m.sheetId);
    if (!s) return setError(error, QStringLiteral("Unknown sheet"));
    const bool rows = m.kind == Mutation::Kind::InsertRows || m.kind == Mutation::Kind::RemoveRows;
    const bool insert = m.kind == Mutation::Kind::InsertRows || m.kind == Mutation::Kind::InsertColumns;
    const int extent = rows ? s->rows : s->columns;
    const int limit = rows ? MaxRows : MaxColumns;
    const QString axis = rows ? QStringLiteral("rows") : QStringLiteral("columns");
    if (m.count < 1 || m.at < 0) return setError(error, QStringLiteral("Invalid %1 range").arg(axis));
    if (insert && (m.at > extent || qint64(extent) + m.count > limit))
        return setError(error, QStringLiteral("A sheet holds at most %1 %2").arg(limit).arg(axis));
    if (!insert && (qint64(m.at) + m.count > extent || extent - m.count < 1))
        return setError(error, QStringLiteral("Cannot remove those %1; a sheet keeps at least one").arg(axis));

    const StructuralEdit edit{s->id, rows, insert, m.at, m.count};
    const qint64 bandEnd = qint64(m.at) + m.count;
    auto inBand = [&](int coordinate) { return !insert && coordinate >= m.at && coordinate < bandEnd; };
    auto mapCoordinate = [&](int coordinate) -> int {
        if (insert) return coordinate >= m.at ? coordinate + m.count : coordinate;
        if (coordinate < m.at) return coordinate;
        return coordinate >= bandEnd ? coordinate - m.count : -1;
    };
    auto mapPosition = [&](quint64 position) -> qint64 {
        const int r = keyRow(position), c = keyColumn(position);
        const int mapped = mapCoordinate(rows ? r : c);
        if (mapped < 0) return -1;
        return qint64(rows ? positionKey(mapped, c) : positionKey(r, mapped));
    };

    Mutation restoreHere;
    restoreHere.kind = Mutation::Kind::SetCells;
    restoreHere.sheetId = s->id;
    MutationList restoreOthers;
    QSet<quint64> affected;
    for (const auto &other : sheets) {
        Mutation pre;
        pre.kind = Mutation::Kind::SetCells;
        pre.sheetId = other->id;
        for (auto &[position, c] : other->cells) {
            if (c.content.kind != CellContent::Kind::Formula) continue;
            const int r = keyRow(position), col = keyColumn(position);
            if (other.get() == s && inBand(rows ? r : col)) continue;
            const Rewrite rw = rewriteForStructure(c.content.formula, other->id, edit);
            if (!rw.changed) continue;
            pre.cells.push_back({r, col, c.content});
            c.content.formula = rw.formula;
            if (rw.valueAffected) affected.insert(globalKey(other->uid, position));
        }
        if (pre.cells.empty()) continue;
        if (other.get() == s) restoreHere.cells = std::move(pre.cells);
        else restoreOthers.push_back(std::move(pre));
    }

    std::map<quint64, Cell> moved;
    for (auto &[position, c] : s->cells) {
        const qint64 target = mapPosition(position);
        if (target < 0) {
            restoreHere.cells.push_back({keyRow(position), keyColumn(position), c.content});
            continue;
        }
        moved.emplace_hint(moved.end(), quint64(target), std::move(c));
    }
    s->cells = std::move(moved);
    s->formulaCount = 0;
    for (const auto &[position, c] : s->cells)
        if (c.content.kind == CellContent::Kind::Formula) ++s->formulaCount;
    (rows ? s->rows : s->columns) += insert ? m.count : -m.count;
    MutationList restoreFeatures;
    transformSheetFeatures(&edit, {}, &restoreFeatures);
    rebuildDependencies();

    if (txn) {
        QSet<quint64> seeds;
        auto keep = [&](quint64 key) {
            if (keyUid(key) != s->uid) { seeds.insert(key); return; }
            const qint64 target = mapPosition(keyPosition(key));
            if (target >= 0) seeds.insert(globalKey(s->uid, quint64(target)));
        };
        for (quint64 key : std::as_const(txn->seeds)) keep(key);
        for (quint64 key : std::as_const(affected)) keep(key);
        txn->seeds = std::move(seeds);
        if (!restoreFeatures.empty()) seedNameUsers(); // a name may have moved
        Bounds all;
        all.include(0, 0);
        all.include(s->rows - 1, s->columns - 1);
        txn->touched[s->uid] = all;
        txn->structural.push_back({s->id, rows, insert, m.at, m.count});
    }

    Mutation opposite;
    opposite.kind = rows ? (insert ? Mutation::Kind::RemoveRows : Mutation::Kind::InsertRows)
                         : (insert ? Mutation::Kind::RemoveColumns : Mutation::Kind::InsertColumns);
    opposite.sheetId = s->id;
    opposite.at = m.at;
    opposite.count = m.count;
    inverse->push_back(std::move(opposite));
    if (!restoreHere.cells.empty()) inverse->push_back(std::move(restoreHere));
    for (Mutation &restore : restoreOthers) inverse->push_back(std::move(restore));
    for (Mutation &restore : restoreFeatures) inverse->push_back(std::move(restore));
    return true;
}

bool WorkbookPrivate::applySheetChange(const Mutation &m, MutationList *inverse, QString *error)
{
    if (m.kind == Mutation::Kind::AddSheet) {
        if (!m.state) return setError(error, QStringLiteral("Missing sheet state"));
        const SheetState &state = *m.state;
        if (byId.contains(state.id)) return setError(error, QStringLiteral("Sheet ID is already in use"));
        if (int(sheets.size()) >= Workbook::MaxSheets)
            return setError(error, QStringLiteral("A workbook holds at most %1 sheets").arg(Workbook::MaxSheets));
        const QString problem = nameProblem(state.name, {});
        if (!problem.isEmpty()) return setError(error, problem);
        if (state.rows < 1 || state.rows > MaxRows || state.columns < 1 || state.columns > MaxColumns)
            return setError(error, QStringLiteral("Invalid sheet dimensions"));
        Sheet *s = createSheet(state.id, state.name, state.index, state.rows, state.columns);
        for (const CellEntry &e : state.cells) {
            if (e.row < 0 || e.row >= s->rows || e.column < 0 || e.column >= s->columns
                || !formulaSheetsExist(*this, e.content)) {
                // Undo the partial sheet before reporting; the caller rolls back the rest.
                QString ignored;
                MutationList discard;
                Mutation remove;
                remove.kind = Mutation::Kind::RemoveSheet;
                remove.sheetId = s->id;
                if (sheets.size() > 1) applySheetChange(remove, &discard, &ignored);
                return setError(error, QStringLiteral("Invalid cell in restored sheet"));
            }
            setContent(*s, e.row, e.column, e.content);
        }
        s->features = state.features ? state.features : emptyFeatures();
        seedFormulas(*s);
        if (txn) txn->sheetsChanged = true;
        Mutation undo;
        undo.kind = Mutation::Kind::RemoveSheet;
        undo.sheetId = s->id;
        inverse->push_back(std::move(undo));
        return true;
    }

    Sheet *s = sheet(m.sheetId);
    if (!s) return setError(error, QStringLiteral("Unknown sheet"));
    const int from = indexOf(s);
    if (m.kind == Mutation::Kind::RenameSheet) {
        const QString problem = nameProblem(m.name, s->id);
        if (!problem.isEmpty()) return setError(error, problem);
        if (s->name == m.name) return true;
        Mutation undo;
        undo.kind = Mutation::Kind::RenameSheet;
        undo.sheetId = s->id;
        undo.name = s->name;
        s->name = m.name;
        if (txn) txn->sheetsChanged = true;
        inverse->push_back(std::move(undo));
        return true;
    }
    if (m.kind == Mutation::Kind::MoveSheet) {
        if (m.at < 0 || m.at >= int(sheets.size())) return setError(error, QStringLiteral("Invalid sheet position"));
        if (m.at == from) return true;
        auto owned = std::move(sheets[size_t(from)]);
        sheets.erase(sheets.begin() + from);
        sheets.insert(sheets.begin() + m.at, std::move(owned));
        if (txn) txn->sheetsChanged = true;
        Mutation undo;
        undo.kind = Mutation::Kind::MoveSheet;
        undo.sheetId = s->id;
        undo.at = from;
        inverse->push_back(std::move(undo));
        return true;
    }
    if (m.kind != Mutation::Kind::RemoveSheet) return setError(error, QStringLiteral("Unknown mutation"));
    if (sheets.size() <= 1) return setError(error, QStringLiteral("A workbook keeps at least one sheet"));

    auto state = std::make_shared<SheetState>();
    state->id = s->id;
    state->name = s->name;
    state->index = from;
    state->rows = s->rows;
    state->columns = s->columns;
    state->features = s->features;
    state->cells.reserve(s->cells.size());
    for (const auto &[position, c] : s->cells)
        state->cells.push_back({keyRow(position), keyColumn(position), c.content});
    MutationList restores;
    for (const auto &other : sheets) {
        if (other.get() == s) continue;
        Mutation pre;
        pre.kind = Mutation::Kind::SetCells;
        pre.sheetId = other->id;
        for (auto &[position, c] : other->cells) {
            if (c.content.kind != CellContent::Kind::Formula) continue;
            const Rewrite rw = invalidateSheet(c.content.formula, s->id);
            if (!rw.changed) continue;
            pre.cells.push_back({keyRow(position), keyColumn(position), c.content});
            c.content.formula = rw.formula;
            if (txn) {
                txn->seeds.insert(globalKey(other->uid, position));
                txn->touched[other->uid].include(keyRow(position), keyColumn(position));
            }
        }
        if (!pre.cells.empty()) restores.push_back(std::move(pre));
    }
    const quint32 uid = s->uid;
    const QString removedId = s->id;
    byId.remove(s->id);
    byUid.remove(uid);
    sheets.erase(sheets.begin() + from);
    MutationList restoreFeatures;
    transformSheetFeatures(nullptr, removedId, &restoreFeatures);
    rebuildDependencies();
    if (txn) {
        QSet<quint64> seeds;
        for (quint64 key : std::as_const(txn->seeds)) if (keyUid(key) != uid) seeds.insert(key);
        txn->seeds = std::move(seeds);
        txn->touched.remove(uid);
        txn->sheetsChanged = true;
        txn->propertySheets.remove(removedId);
        if (!restoreFeatures.empty()) seedNameUsers();
    }
    Mutation add;
    add.kind = Mutation::Kind::AddSheet;
    add.state = state;
    inverse->push_back(std::move(add));
    for (Mutation &restore : restores) inverse->push_back(std::move(restore));
    for (Mutation &restore : restoreFeatures) inverse->push_back(std::move(restore));
    return true;
}

// ---- undo data estimates ------------------------------------------------------

namespace {
qint64 contentBytes(const CellContent &content)
{
    qint64 bytes = qint64(sizeof(CellEntry));
    if (content.literal.isText()) bytes += qint64(content.literal.text.size()) * qint64(sizeof(QChar));
    // Formulas can be shared; counting each use keeps the estimate conservative.
    if (content.formula) bytes += qint64(sizeof(Formula)) + qint64(content.formula->nodes.size()) * qint64(sizeof(Node));
    return bytes;
}
qint64 featureBytes(const SheetFeatures &f)
{
    qint64 bytes = qint64(sizeof(SheetFeatures)) + qint64(f.formats.size()) * qint64(sizeof(FormatLayer))
        + qint64(f.merges.size()) * qint64(sizeof(Area))
        + qint64(f.properties.columnWidths.size() + f.properties.rowHeights.size()) * 48;
    for (const ConditionalRule &rule : f.conditional)
        bytes += qint64(sizeof(ConditionalRule)) + qint64(rule.areas.size()) * qint64(sizeof(Area));
    for (const ValidationRule &rule : f.validations) {
        bytes += qint64(sizeof(ValidationRule)) + qint64(rule.areas.size()) * qint64(sizeof(Area));
        for (const QString &item : rule.items) bytes += qint64(sizeof(QString)) + qint64(item.size()) * qint64(sizeof(QChar));
    }
    return bytes;
}
} // namespace

qint64 Calc::retainedBytes(const MutationList &list, bool sharedState)
{
    qint64 bytes = 0;
    for (const Mutation &m : list) {
        bytes += qint64(sizeof(Mutation));
        for (const CellEntry &entry : m.cells) bytes += contentBytes(entry.content);
        if (sharedState) continue;
        if (m.state) {
            bytes += qint64(sizeof(SheetState));
            for (const CellEntry &entry : m.state->cells) bytes += contentBytes(entry.content);
            if (m.state->features) bytes += featureBytes(*m.state->features);
        }
        if (m.features) bytes += featureBytes(*m.features);
        if (m.names) bytes += qint64(m.names->size()) * qint64(sizeof(NamedRange) + 64);
    }
    return bytes;
}

} // namespace QindaTK
