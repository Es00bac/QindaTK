// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

#include <QElapsedTimer>
#include <algorithm>

namespace QindaTK {
using namespace Calc;
namespace {
// AGENT-NOTE: a range touching more than this many columns is indexed per
// sheet instead of per column, bounding index memory for wide ranges.
constexpr int WideRangeColumns = 32;
constexpr qint64 ProbeArea = 4096;
quint64 columnBucket(quint32 uid, int column) { return (quint64(uid) << 14) | quint64(column); }
}

bool CellContent::sameAs(const CellContent &other) const
{
    if (kind != other.kind) return false;
    if (kind == Kind::Literal) return literal == other.literal;
    if (kind == Kind::Formula) return formula == other.formula || (formula && other.formula && formula->sameAs(*other.formula));
    return true;
}
void Bounds::include(int row, int column)
{
    if (lastRow < 0) { firstRow = lastRow = row; firstColumn = lastColumn = column; return; }
    firstRow = std::min(firstRow, row); lastRow = std::max(lastRow, row);
    firstColumn = std::min(firstColumn, column); lastColumn = std::max(lastColumn, column);
}

Value WorkbookPrivate::cell(const QString &sheetId, int row, int column) const
{
    const Sheet *s = sheet(sheetId);
    if (!s) return Value::fromError(ErrorKind::Ref);
    const Cell *c = findCell(*s, row, column);
    return c ? c->value : Value::blank();
}

void WorkbookPrivate::visitRange(const QString &sheetId, int firstRow, int firstColumn, int lastRow,
                                 int lastColumn, const std::function<bool(const Value &)> &visitor) const
{
    const Sheet *s = sheet(sheetId);
    if (!s) { visitor(Value::fromError(ErrorKind::Ref)); return; }
    visitCells(*s, firstRow, firstColumn, lastRow, lastColumn, [&](quint64, const Value &v) { return visitor(v); });
}

void WorkbookPrivate::visitCells(const Sheet &s, int firstRow, int firstColumn, int lastRow, int lastColumn,
                                 const std::function<bool(quint64, const Value &)> &visitor) const
{
    const qint64 area = qint64(lastRow - firstRow + 1) * (lastColumn - firstColumn + 1);
    if (area <= ProbeArea) {
        for (int r = firstRow; r <= lastRow; ++r)
            for (int c = firstColumn; c <= lastColumn; ++c)
                if (const Cell *found = findCell(s, r, c); found && !visitor(positionKey(r, c), found->value)) return;
        return;
    }
    auto it = s.cells.lower_bound(positionKey(firstRow, firstColumn));
    const auto end = s.cells.upper_bound(positionKey(lastRow, lastColumn));
    for (; it != end; ++it) {
        const int c = keyColumn(it->first);
        if (c < firstColumn || c > lastColumn) continue;
        if (!visitor(it->first, it->second.value)) return;
    }
}

const Cell *WorkbookPrivate::findCell(const Sheet &s, int row, int column) const
{
    const auto it = s.cells.find(positionKey(row, column));
    return it == s.cells.end() ? nullptr : &it->second;
}

bool WorkbookPrivate::resolveName(const QString &name, Precedent *target) const
{
    const NamedRange *entry = findName(*names, name);
    if (!entry) return false;
    target->sheetId = entry->sheetId;
    target->firstRow = entry->area.firstRow;
    target->firstColumn = entry->area.firstColumn;
    target->lastRow = entry->area.lastRow;
    target->lastColumn = entry->area.lastColumn;
    target->range = entry->area.cellCount() > 1;
    return true;
}

void WorkbookPrivate::registerFormula(quint64 key, const QString &hostId, const Formula &formula, bool add)
{
    // AGENT-GUARD: names resolve against the current table, so every name
    // table change must rebuild the index (applyFeatures, structural edits,
    // sheet removal) or removals would miss what an older table registered.
    std::vector<Precedent> precedents;
    collectPrecedents(formula, hostId, &precedents,
                      [this](const QString &name, Precedent *target) { return resolveName(name, target); });
    for (const Precedent &p : precedents) {
        const Sheet *target = sheet(p.sheetId);
        if (!target) continue;
        if (!p.range) {
            const quint64 cellKey = globalKey(target->uid, positionKey(p.firstRow, p.firstColumn));
            if (add) cellDependents[cellKey].insert(key);
            else if (auto it = cellDependents.find(cellKey); it != cellDependents.end()) {
                it->remove(key);
                if (it->isEmpty()) cellDependents.erase(it);
            }
            continue;
        }
        const RangeKey range{target->uid, p.firstRow, p.firstColumn, p.lastRow, p.lastColumn};
        const bool wide = p.lastColumn - p.firstColumn + 1 > WideRangeColumns;
        if (add) {
            QSet<quint64> &formulas = rangeDependents[range];
            if (formulas.isEmpty()) {
                if (wide) wideRanges[range.sheet].insert(range);
                else for (int c = p.firstColumn; c <= p.lastColumn; ++c)
                    columnRanges[columnBucket(range.sheet, c)].insert(range);
            }
            formulas.insert(key);
            continue;
        }
        auto it = rangeDependents.find(range);
        if (it == rangeDependents.end()) continue;
        it->remove(key);
        if (!it->isEmpty()) continue;
        rangeDependents.erase(it);
        if (wide) {
            wideRanges[range.sheet].remove(range);
            if (wideRanges[range.sheet].isEmpty()) wideRanges.remove(range.sheet);
        } else {
            for (int c = p.firstColumn; c <= p.lastColumn; ++c) {
                const quint64 bucket = columnBucket(range.sheet, c);
                columnRanges[bucket].remove(range);
                if (columnRanges[bucket].isEmpty()) columnRanges.remove(bucket);
            }
        }
    }
}

void WorkbookPrivate::rebuildDependencies()
{
    cellDependents.clear();
    rangeDependents.clear();
    columnRanges.clear();
    wideRanges.clear();
    for (const auto &s : sheets)
        for (const auto &[position, c] : s->cells)
            if (c.content.kind == CellContent::Kind::Formula)
                registerFormula(globalKey(s->uid, position), s->id, *c.content.formula, true);
}

void WorkbookPrivate::setContent(Sheet &s, int row, int column, const CellContent &content)
{
    const quint64 position = positionKey(row, column);
    const quint64 key = globalKey(s.uid, position);
    auto it = s.cells.find(position);
    if (it != s.cells.end() && it->second.content.kind == CellContent::Kind::Formula) {
        registerFormula(key, s.id, *it->second.content.formula, false);
        --s.formulaCount;
    }
    if (content.kind == CellContent::Kind::Empty) {
        if (it != s.cells.end()) s.cells.erase(it);
    } else {
        Cell &target = it != s.cells.end() ? it->second : s.cells[position];
        target.content = content;
        target.value = content.kind == CellContent::Kind::Literal ? content.literal : Value::blank();
        if (content.kind == CellContent::Kind::Formula) {
            registerFormula(key, s.id, *content.formula, true);
            ++s.formulaCount;
        }
    }
    if (txn) {
        txn->seeds.insert(key);
        txn->touched[s.uid].include(row, column);
    }
}

void WorkbookPrivate::dependentsOf(quint64 key, std::vector<quint64> *out) const
{
    const quint32 uid = keyUid(key);
    const quint64 position = keyPosition(key);
    const int row = keyRow(position), column = keyColumn(position);
    if (const auto it = cellDependents.constFind(key); it != cellDependents.cend())
        out->insert(out->end(), it->cbegin(), it->cend());
    auto scan = [&](const QSet<RangeKey> &ranges) {
        for (const RangeKey &range : ranges)
            if (row >= range.firstRow && row <= range.lastRow && column >= range.firstColumn
                && column <= range.lastColumn) {
                if (const auto found = rangeDependents.constFind(range); found != rangeDependents.cend())
                    out->insert(out->end(), found->cbegin(), found->cend());
            }
    };
    if (const auto it = columnRanges.constFind(columnBucket(uid, column)); it != columnRanges.cend()) scan(*it);
    if (const auto it = wideRanges.constFind(uid); it != wideRanges.cend()) scan(*it);
}

// AGENT-NOTE: recalculation is incremental and deterministic. The dirty set
// is every formula reachable from the seeds through the dependency index;
// Kahn's algorithm orders it, iterative Tarjan marks strongly connected
// components (and self references) #CYCLE!, then the remaining cells that
// depend on a cycle evaluate in order and propagate its error.
void WorkbookPrivate::recalculate(const QSet<quint64> &seeds, QHash<quint32, Bounds> *changed)
{
    QElapsedTimer timer;
    timer.start();
    QHash<quint64, qint32> index;
    std::vector<quint64> nodes, queue;
    std::vector<std::vector<qint32>> edges;
    auto isFormula = [this](quint64 key) {
        const Sheet *s = byUid.value(keyUid(key), nullptr);
        if (!s) return false;
        const auto it = s->cells.find(keyPosition(key));
        return it != s->cells.end() && it->second.content.kind == CellContent::Kind::Formula;
    };
    auto addNode = [&](quint64 key) {
        if (const auto it = index.constFind(key); it != index.cend()) return *it;
        const qint32 id = qint32(nodes.size());
        index.insert(key, id);
        nodes.push_back(key);
        edges.emplace_back();
        queue.push_back(key);
        return id;
    };
    std::vector<quint64> ordered(seeds.cbegin(), seeds.cend());
    std::sort(ordered.begin(), ordered.end());
    for (quint64 key : ordered) {
        if (!byUid.contains(keyUid(key))) continue;
        if (isFormula(key)) addNode(key);
        else queue.push_back(key);
    }
    std::vector<quint64> dependents;
    for (size_t head = 0; head < queue.size(); ++head) {
        const quint64 position = queue[head];
        dependents.clear();
        dependentsOf(position, &dependents);
        std::sort(dependents.begin(), dependents.end());
        dependents.erase(std::unique(dependents.begin(), dependents.end()), dependents.end());
        const qint32 from = index.value(position, -1);
        for (quint64 dependent : dependents) {
            const qint32 to = addNode(dependent);
            if (from >= 0) edges[size_t(from)].push_back(to);
        }
    }

    auto evaluate = [&](qint32 id, bool cyclic) {
        const quint64 key = nodes[size_t(id)];
        Sheet *s = byUid.value(keyUid(key), nullptr);
        const auto it = s ? s->cells.find(keyPosition(key)) : std::map<quint64, Cell>::iterator();
        if (!s || it == s->cells.end() || it->second.content.kind != CellContent::Kind::Formula) return;
        const Value next = cyclic ? Value::fromError(ErrorKind::Cycle)
                                  : evaluateFormula(*it->second.content.formula, s->id, *this);
        ++stats.evaluated;
        if (next == it->second.value) return;
        it->second.value = next;
        if (changed) (*changed)[s->uid].include(keyRow(it->first), keyColumn(it->first));
    };
    auto kahn = [&](const std::vector<char> &active) {
        std::vector<qint32> indegree(nodes.size(), 0), ready;
        for (size_t from = 0; from < nodes.size(); ++from)
            if (active[from]) for (qint32 to : edges[from]) if (active[size_t(to)]) ++indegree[size_t(to)];
        for (size_t i = 0; i < nodes.size(); ++i) if (active[i] && indegree[i] == 0) ready.push_back(qint32(i));
        std::vector<char> done(nodes.size(), 0);
        for (size_t head = 0; head < ready.size(); ++head) {
            const qint32 id = ready[head];
            evaluate(id, false);
            done[size_t(id)] = 1;
            for (qint32 to : edges[size_t(id)])
                if (active[size_t(to)] && --indegree[size_t(to)] == 0) ready.push_back(to);
        }
        return done;
    };

    stats.dirty = qint64(nodes.size());
    stats.evaluated = 0;
    stats.cycleCells = 0;
    std::vector<char> active(nodes.size(), 1);
    const std::vector<char> done = kahn(active);
    std::vector<char> remaining(nodes.size(), 0);
    bool anyRemaining = false;
    for (size_t i = 0; i < nodes.size(); ++i) if (!done[i]) { remaining[i] = 1; anyRemaining = true; }
    if (anyRemaining) {
        // Iterative Tarjan over the unresolved subgraph.
        std::vector<qint32> order(nodes.size(), -1), low(nodes.size(), 0), stack;
        std::vector<char> onStack(nodes.size(), 0), cyclic(nodes.size(), 0);
        struct Frame { qint32 node; size_t edge; };
        std::vector<Frame> frames;
        qint32 counter = 0;
        for (size_t root = 0; root < nodes.size(); ++root) {
            if (!remaining[root] || order[root] >= 0) continue;
            order[root] = low[root] = counter++;
            stack.push_back(qint32(root)); onStack[root] = 1;
            frames.push_back({qint32(root), 0});
            while (!frames.empty()) {
                const qint32 v = frames.back().node;
                if (frames.back().edge < edges[size_t(v)].size()) {
                    const qint32 w = edges[size_t(v)][frames.back().edge++];
                    if (!remaining[size_t(w)]) continue;
                    if (order[size_t(w)] < 0) {
                        order[size_t(w)] = low[size_t(w)] = counter++;
                        stack.push_back(w); onStack[size_t(w)] = 1;
                        frames.push_back({w, 0});
                    } else if (onStack[size_t(w)]) {
                        low[size_t(v)] = std::min(low[size_t(v)], order[size_t(w)]);
                    }
                    continue;
                }
                if (low[size_t(v)] == order[size_t(v)]) {
                    std::vector<qint32> component;
                    qint32 w = -1;
                    do {
                        w = stack.back(); stack.pop_back(); onStack[size_t(w)] = 0;
                        component.push_back(w);
                    } while (w != v);
                    const auto &out = edges[size_t(v)];
                    const bool selfLoop = std::find(out.begin(), out.end(), v) != out.end();
                    if (component.size() > 1 || selfLoop)
                        for (qint32 member : component) cyclic[size_t(member)] = 1;
                }
                frames.pop_back();
                if (!frames.empty()) {
                    const qint32 u = frames.back().node;
                    low[size_t(u)] = std::min(low[size_t(u)], low[size_t(v)]);
                }
            }
        }
        std::vector<char> downstream(nodes.size(), 0);
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (!remaining[i]) continue;
            if (cyclic[i]) { evaluate(qint32(i), true); ++stats.cycleCells; }
            else downstream[i] = 1;
        }
        kahn(downstream);
    }
    stats.totalEvaluations += stats.evaluated;
    stats.microseconds = timer.nsecsElapsed() / 1000;
}

} // namespace QindaTK
