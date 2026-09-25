// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notebook_document_p.h"

#include <QRegularExpression>

namespace QindaTK {
using namespace Notebook;

namespace {
// Keeps the formatting of unchanged text: the common prefix and suffix keep
// their runs and the replaced middle takes the format before it.
NotebookText::Runs diffed(const NotebookText::Runs &runs, const QString &text)
{
    const QString &old = runs.text;
    int prefix = 0;
    while (prefix < old.size() && prefix < text.size() && old.at(prefix) == text.at(prefix)) ++prefix;
    int suffix = 0;
    while (suffix < old.size() - prefix && suffix < text.size() - prefix
           && old.at(old.size() - 1 - suffix) == text.at(text.size() - 1 - suffix)) ++suffix;
    return NotebookText::replace(runs, prefix, int(old.size()) - prefix - suffix,
                                 text.mid(prefix, text.size() - prefix - suffix));
}
bool validType(const QString &type)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9-]{0,63}$"));
    return pattern.match(type).hasMatch();
}
void dropEvaluation(QJsonObject *data)
{
    for (const char *key : {"output", "result", "error", "status", "evaluatedRevision"})
        data->remove(QLatin1String(key));
}
// Moves the subtree rooted at `blockId` to child `index` of `parentId` in
// `targetPage` ("" parent = top level). False when the target is invalid.
bool moveSubtree(State *state, int sourcePage, int sourceIndex, int targetPage,
                 const QString &parentId, int index, QString *problem)
{
    auto &source = state->pages[sourcePage].blocks;
    const QVector<Block> original = source;
    const int end = NotebookTree::subtreeEnd(source, sourceIndex);
    for (int i = sourceIndex; i < end; ++i)
        if (source.at(i).id == parentId) {
            *problem = QStringLiteral("A block cannot move into itself");
            return false;
        }
    QVector<Block> segment = source.mid(sourceIndex, end - sourceIndex);
    if (targetPage != sourcePage
        && state->pages.at(targetPage).blocks.size() + segment.size() > Limits::MaxBlocksPerPage) {
        *problem = QStringLiteral("A page holds at most 10,000 blocks");
        return false;
    }
    source.remove(sourceIndex, end - sourceIndex);
    auto &target = state->pages[targetPage].blocks;
    const int position = NotebookTree::insertionPoint(target, parentId, index);
    if (position < 0) {
        *problem = QStringLiteral("Unknown target parent %1").arg(parentId);
        return false;
    }
    segment[0].parentId = parentId;
    for (int i = 0; i < segment.size(); ++i) target.insert(position + i, segment.at(i));
    NotebookTree::repair(&target, original);
    if (targetPage != sourcePage) NotebookTree::repair(&state->pages[sourcePage].blocks, original);
    return true;
}
} // namespace

QString NotebookDocument::insertBlock(const QString &pageId, const QString &afterBlockId,
                                      const QString &type, const QVariantMap &data)
{
    const int pageAt = pageIndex(pageId);
    if (pageAt < 0) { fail(QStringLiteral("Unknown page %1").arg(pageId)); return {}; }
    if (!NotebookText::isKnownType(type)) { fail(QStringLiteral("Unsupported block type %1").arg(type)); return {}; }
    if (m_state.pages.at(pageAt).blocks.size() >= Limits::MaxBlocksPerPage || blockCount() >= Limits::MaxBlocks) {
        fail(QStringLiteral("The page or notebook block limit is reached"));
        return {};
    }
    QString problem;
    const QJsonObject normalized = NotebookText::normalized(type, QJsonObject::fromVariantMap(data), &problem);
    if (!problem.isEmpty()) { fail(problem); return {}; }
    State next = m_state;
    auto &list = next.pages[pageAt].blocks;
    Block block{NotebookTree::newId(), type, {}, normalized, false};
    int position = 0;
    if (!afterBlockId.isEmpty()) {
        const int after = NotebookTree::indexOf(list, afterBlockId);
        if (after < 0) { fail(QStringLiteral("Block %1 is not on page %2").arg(afterBlockId, pageId)); return {}; }
        block.parentId = list.at(after).parentId;
        position = NotebookTree::subtreeEnd(list, after);
    }
    list.insert(position, block);
    return commit(next, tr("Insert %1 block").arg(type)) ? block.id : QString();
}

bool NotebookDocument::setBlockData(const QString &blockId, const QVariantMap &data)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    QJsonObject incoming = QJsonObject::fromVariantMap(data);
    // A caller that edits `text` and passes the old runs back keeps the
    // formatting of the unchanged text.
    if (NotebookText::hasRuns(current.type) && !NotebookText::validateRuns(incoming).isEmpty()
        && incoming.value(QStringLiteral("runs")) == current.data.value(QStringLiteral("runs"))) {
        NotebookText::write(&incoming, diffed(NotebookText::read(current.data),
                                              incoming.value(QStringLiteral("text")).toString()));
    }
    QString problem;
    const QJsonObject normalized = NotebookText::normalized(current.type, incoming, &problem);
    if (!problem.isEmpty()) return fail(problem);
    State next = m_state;
    next.pages[at.page].blocks[at.block].data = normalized;
    return commit(next, tr("Edit block"), QStringLiteral("data:") + blockId);
}

bool NotebookDocument::setBlockText(const QString &blockId, const QString &text)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    if (!NotebookText::isTextType(current.type)) return fail(QStringLiteral("Block %1 holds no text").arg(blockId));
    if (text.size() > Limits::MaxTextLength) return fail(QStringLiteral("Block text exceeds 1 Mi characters"));
    State next = m_state;
    QJsonObject &data = next.pages[at.page].blocks[at.block].data;
    NotebookText::write(&data, diffed(NotebookText::read(data), text));
    if (!NotebookText::hasRuns(current.type)) data.remove(QStringLiteral("runs"));
    return commit(next, tr("Edit text"), QStringLiteral("text:") + blockId);
}

bool NotebookDocument::replaceText(const QString &blockId, int offset, int removed, const QString &text)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    if (!NotebookText::isTextType(current.type)) return fail(QStringLiteral("Block %1 holds no text").arg(blockId));
    const int length = int(current.data.value(QStringLiteral("text")).toString().size());
    if (offset < 0 || removed < 0 || offset + removed > length)
        return fail(QStringLiteral("Text range is outside block %1").arg(blockId));
    if (length - removed + text.size() > Limits::MaxTextLength)
        return fail(QStringLiteral("Block text exceeds 1 Mi characters"));
    State next = m_state;
    QJsonObject &data = next.pages[at.page].blocks[at.block].data;
    NotebookText::write(&data, NotebookText::replace(NotebookText::read(data), offset, removed, text));
    if (!NotebookText::hasRuns(current.type)) data.remove(QStringLiteral("runs"));
    return commit(next, tr("Edit text"), QStringLiteral("text:") + blockId);
}

bool NotebookDocument::formatText(const QString &blockId, int offset, int length, const QVariantMap &patch)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    if (!NotebookText::hasRuns(current.type)) return fail(QStringLiteral("Block %1 has no inline formatting").arg(blockId));
    const int total = int(current.data.value(QStringLiteral("text")).toString().size());
    if (offset < 0 || length <= 0 || offset + length > total)
        return fail(QStringLiteral("Format range is outside block %1").arg(blockId));
    State next = m_state;
    QJsonObject &data = next.pages[at.page].blocks[at.block].data;
    NotebookText::write(&data, NotebookText::format(NotebookText::read(data), offset, length,
                                                    QJsonObject::fromVariantMap(patch)));
    return commit(next, tr("Format text"));
}

bool NotebookDocument::setBlockType(const QString &blockId, const QString &type)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    if (current.type == type) return true;
    if (!NotebookText::isTextType(current.type) || !NotebookText::isTextType(type))
        return fail(QStringLiteral("Only text blocks change type"));
    QJsonObject data{{QStringLiteral("text"), current.data.value(QStringLiteral("text"))}};
    if (NotebookText::hasRuns(type) && current.data.contains(QStringLiteral("runs")))
        data.insert(QStringLiteral("runs"), current.data.value(QStringLiteral("runs")));
    QString problem;
    const QJsonObject normalized = NotebookText::normalized(type, data, &problem);
    if (!problem.isEmpty()) return fail(problem);
    State next = m_state;
    next.pages[at.page].blocks[at.block].type = type;
    next.pages[at.page].blocks[at.block].data = normalized;
    return commit(next, tr("Change block type"));
}

bool NotebookDocument::setBlockLocked(const QString &blockId, bool locked)
{
    const Location at = locate(blockId);
    if (!at.valid()) return fail(QStringLiteral("Unknown block %1").arg(blockId));
    State next = m_state;
    next.pages[at.page].blocks[at.block].locked = locked;
    return commit(next, locked ? tr("Lock block") : tr("Unlock block"));
}

bool NotebookDocument::moveBlock(const QString &blockId, const QString &targetPageId,
                                 const QString &targetParentId, int targetIndex)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const int targetPage = pageIndex(targetPageId);
    if (targetPage < 0) return fail(QStringLiteral("Unknown page %1").arg(targetPageId));
    State next = m_state;
    QString problem;
    if (!moveSubtree(&next, at.page, at.block, targetPage, targetParentId, targetIndex, &problem))
        return fail(problem);
    return commit(next, tr("Move block"));
}

bool NotebookDocument::moveBlockBy(const QString &blockId, int delta)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const auto &list = m_state.pages.at(at.page).blocks;
    const QString parent = list.at(at.block).parentId;
    const QVector<int> siblings = NotebookTree::children(list, parent);
    const int position = int(siblings.indexOf(at.block));
    const int target = qBound(0, position + delta, int(siblings.size()) - 1);
    if (target == position) return true;
    State next = m_state;
    QString problem;
    if (!moveSubtree(&next, at.page, at.block, at.page, parent, target, &problem)) return fail(problem);
    return commit(next, tr("Move block"));
}

bool NotebookDocument::indentBlock(const QString &blockId)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const auto &list = m_state.pages.at(at.page).blocks;
    const QVector<int> siblings = NotebookTree::children(list, list.at(at.block).parentId);
    const int position = int(siblings.indexOf(at.block));
    if (position <= 0) return fail(QStringLiteral("The first block of a level cannot be indented"));
    if (NotebookTree::depths(list).at(at.block) >= 15) return fail(QStringLiteral("Blocks nest at most 16 levels"));
    State next = m_state;
    QString problem;
    if (!moveSubtree(&next, at.page, at.block, at.page, list.at(siblings.at(position - 1)).id, -1, &problem))
        return fail(problem);
    return commit(next, tr("Indent block"));
}

bool NotebookDocument::outdentBlock(const QString &blockId)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const auto &list = m_state.pages.at(at.page).blocks;
    const QString parentId = list.at(at.block).parentId;
    if (parentId.isEmpty()) return fail(QStringLiteral("A top-level block cannot be outdented"));
    const int parentAt = NotebookTree::indexOf(list, parentId);
    const QString grand = list.at(parentAt).parentId;
    const int parentPosition = int(NotebookTree::children(list, grand).indexOf(parentAt));
    State next = m_state;
    QString problem;
    if (!moveSubtree(&next, at.page, at.block, at.page, grand, parentPosition + 1, &problem)) return fail(problem);
    return commit(next, tr("Outdent block"));
}

QString NotebookDocument::splitBlock(const QString &blockId, int offset)
{
    Location at;
    if (!editableBlock(blockId, &at)) return {};
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    if (!NotebookText::isTextType(current.type)) { fail(QStringLiteral("Only text blocks split")); return {}; }
    if (m_state.pages.at(at.page).blocks.size() >= Limits::MaxBlocksPerPage) {
        fail(QStringLiteral("A page holds at most 10,000 blocks"));
        return {};
    }
    const NotebookText::Runs runs = NotebookText::read(current.data);
    offset = NotebookText::snap(runs.text, offset);
    State next = m_state;
    auto &list = next.pages[at.page].blocks;
    Block tail{NotebookTree::newId(), current.type == QLatin1String("heading") ? QStringLiteral("text") : current.type,
               current.parentId, {}, false};
    if (tail.type == current.type) {
        tail.data = current.data;
        dropEvaluation(&tail.data);
        tail.data.remove(QStringLiteral("checked"));
    }
    NotebookText::write(&tail.data, NotebookText::slice(runs, offset, int(runs.text.size()) - offset));
    NotebookText::write(&list[at.block].data, NotebookText::slice(runs, 0, offset));
    if (!NotebookText::hasRuns(tail.type)) tail.data.remove(QStringLiteral("runs"));
    QString problem;
    tail.data = NotebookText::normalized(tail.type, tail.data, &problem);
    // The tail continues the text visually, so the children follow it.
    for (Block &block : list)
        if (block.parentId == blockId) block.parentId = tail.id;
    list.insert(at.block + 1, tail);
    return commit(next, tr("Split block")) ? tail.id : QString();
}

int NotebookDocument::mergeBlocks(const QString &firstBlockId, const QString &secondBlockId)
{
    Location first, second;
    if (!editableBlock(firstBlockId, &first) || !editableBlock(secondBlockId, &second)) return -1;
    if (first.page != second.page || second.block != first.block + 1) {
        fail(QStringLiteral("Only adjacent blocks merge"));
        return -1;
    }
    const auto &list = m_state.pages.at(first.page).blocks;
    const Block &a = list.at(first.block);
    const Block &b = list.at(second.block);
    if (!NotebookText::isTextType(a.type) || !NotebookText::isTextType(b.type)) {
        fail(QStringLiteral("Only text blocks merge"));
        return -1;
    }
    const NotebookText::Runs head = NotebookText::read(a.data);
    if (head.text.size() + b.data.value(QStringLiteral("text")).toString().size() > Limits::MaxTextLength) {
        fail(QStringLiteral("Block text exceeds 1 Mi characters"));
        return -1;
    }
    State next = m_state;
    auto &blocks = next.pages[first.page].blocks;
    QJsonObject &data = blocks[first.block].data;
    NotebookText::write(&data, NotebookText::concat(head, NotebookText::read(b.data)));
    if (!NotebookText::hasRuns(a.type)) data.remove(QStringLiteral("runs"));
    for (Block &block : blocks)
        if (block.parentId == secondBlockId) block.parentId = firstBlockId;
    blocks.removeAt(second.block);
    NotebookTree::repair(&blocks, list);
    return commit(next, tr("Merge blocks")) ? int(head.text.size()) : -1;
}

bool NotebookDocument::removeBlock(const QString &blockId)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    State next = m_state;
    auto &list = next.pages[at.page].blocks;
    const int end = NotebookTree::subtreeEnd(list, at.block);
    list.remove(at.block, end - at.block);
    return commit(next, tr("Delete block"));
}

namespace {
bool tableBlock(const Block &block) { return block.type == QLatin1String("table"); }
} // namespace

bool NotebookDocument::setTableCell(const QString &blockId, int row, int column, const QString &text)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    QJsonArray rows = current.data.value(QStringLiteral("rows")).toArray();
    if (!tableBlock(current) || row < 0 || row >= rows.size() || column < 0
        || column >= current.data.value(QStringLiteral("columns")).toArray().size())
        return fail(QStringLiteral("Cell is outside table %1").arg(blockId));
    QJsonArray cells = rows.at(row).toArray();
    cells[column] = text.left(65536);
    rows[row] = cells;
    State next = m_state;
    next.pages[at.page].blocks[at.block].data.insert(QStringLiteral("rows"), rows);
    return commit(next, tr("Edit table cell"), QStringLiteral("cell:%1:%2:%3").arg(blockId).arg(row).arg(column));
}

bool NotebookDocument::insertTableRow(const QString &blockId, int index)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    QJsonArray rows = current.data.value(QStringLiteral("rows")).toArray();
    QJsonArray ids = current.data.value(QStringLiteral("rowIds")).toArray();
    const int columns = int(current.data.value(QStringLiteral("columns")).toArray().size());
    if (!tableBlock(current) || rows.size() >= 10000) return fail(QStringLiteral("Cannot add a row to %1").arg(blockId));
    if (index < 0 || index > rows.size()) index = int(rows.size());
    QJsonArray empty;
    for (int c = 0; c < columns; ++c) empty.append(QString());
    rows.insert(index, empty);
    ids.insert(index, NotebookTree::newId());
    State next = m_state;
    QJsonObject &data = next.pages[at.page].blocks[at.block].data;
    data.insert(QStringLiteral("rows"), rows);
    data.insert(QStringLiteral("rowIds"), ids);
    return commit(next, tr("Insert table row"));
}

bool NotebookDocument::removeTableRow(const QString &blockId, int index)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    QJsonArray rows = current.data.value(QStringLiteral("rows")).toArray();
    QJsonArray ids = current.data.value(QStringLiteral("rowIds")).toArray();
    if (!tableBlock(current) || index < 0 || index >= rows.size())
        return fail(QStringLiteral("Row is outside table %1").arg(blockId));
    rows.removeAt(index);
    ids.removeAt(index);
    State next = m_state;
    QJsonObject &data = next.pages[at.page].blocks[at.block].data;
    data.insert(QStringLiteral("rows"), rows);
    data.insert(QStringLiteral("rowIds"), ids);
    return commit(next, tr("Delete table row"));
}

bool NotebookDocument::insertTableColumn(const QString &blockId, int index, const QString &name)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    QJsonArray columns = current.data.value(QStringLiteral("columns")).toArray();
    if (!tableBlock(current) || columns.size() >= 256) return fail(QStringLiteral("Cannot add a column to %1").arg(blockId));
    if (index < 0 || index > columns.size()) index = int(columns.size());
    columns.insert(index, name.isEmpty() ? tr("Column %1").arg(columns.size() + 1) : name.left(256));
    QJsonArray rows = current.data.value(QStringLiteral("rows")).toArray();
    for (int r = 0; r < rows.size(); ++r) {
        QJsonArray cells = rows.at(r).toArray();
        cells.insert(index, QString());
        rows[r] = cells;
    }
    State next = m_state;
    QJsonObject &data = next.pages[at.page].blocks[at.block].data;
    data.insert(QStringLiteral("columns"), columns);
    data.insert(QStringLiteral("rows"), rows);
    return commit(next, tr("Insert table column"));
}

bool NotebookDocument::removeTableColumn(const QString &blockId, int index)
{
    Location at;
    if (!editableBlock(blockId, &at)) return false;
    const Block &current = m_state.pages.at(at.page).blocks.at(at.block);
    QJsonArray columns = current.data.value(QStringLiteral("columns")).toArray();
    if (!tableBlock(current) || index < 0 || index >= columns.size() || columns.size() <= 1)
        return fail(QStringLiteral("Cannot remove that column of %1").arg(blockId));
    columns.removeAt(index);
    QJsonArray rows = current.data.value(QStringLiteral("rows")).toArray();
    for (int r = 0; r < rows.size(); ++r) {
        QJsonArray cells = rows.at(r).toArray();
        cells.removeAt(index);
        rows[r] = cells;
    }
    State next = m_state;
    QJsonObject &data = next.pages[at.page].blocks[at.block].data;
    data.insert(QStringLiteral("columns"), columns);
    data.insert(QStringLiteral("rows"), rows);
    return commit(next, tr("Delete table column"));
}

} // namespace QindaTK
