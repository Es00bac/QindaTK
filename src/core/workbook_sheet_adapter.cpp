// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_sheet_adapter.h"

namespace QindaTK {

class WorkbookSheetAdapter::Source final : public GridCellSource {
public:
    explicit Source(WorkbookSheetAdapter *adapter) : GridCellSource(adapter), m_adapter(adapter) {}
    QVariant value(const QString &rowId, const QString &columnId) const override
    { return m_adapter->displayForIds(rowId, columnId); }
    QString validate(const QString &rowId, const QString &columnId, const QVariant &candidate) const override
    { return m_adapter->validateCandidate(rowId, columnId, candidate); }
private:
    WorkbookSheetAdapter *m_adapter;
};

WorkbookSheetAdapter::WorkbookSheetAdapter(QObject *parent)
    : QObject(parent), m_grid(new EditableGridModel(this)), m_source(new Source(this))
{
    m_grid->setExternalUndo(true);
    m_grid->setSource(m_source);
    connect(m_grid, &EditableGridModel::committed, this, &WorkbookSheetAdapter::absorb);
    connect(m_grid, &EditableGridModel::rejected, this, [this](const QString &reason) {
        if (m_absorbing) return;
        setError(reason, m_validationDetail);
        m_validationDetail.clear();
    });
}
WorkbookSheetAdapter::~WorkbookSheetAdapter()
{
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
}

void WorkbookSheetAdapter::setWorkbook(Workbook *workbook)
{
    if (m_workbook == workbook) return;
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
    m_connections.clear();
    m_workbook = workbook;
    if (m_workbook) {
        auto structural = [this](bool rows, bool insert) {
            return [this, rows, insert](const QString &sheetId, int at, int count) {
                if (sheetId != m_sheetId) return;
                moveIds(rows ? &m_rowIds : &m_columnIds, insert, at, count, rows);
                applyIds();
            };
        };
        m_connections << connect(m_workbook, &Workbook::rowsInserted, this, structural(true, true))
                      << connect(m_workbook, &Workbook::rowsRemoved, this, structural(true, false))
                      << connect(m_workbook, &Workbook::columnsInserted, this, structural(false, true))
                      << connect(m_workbook, &Workbook::columnsRemoved, this, structural(false, false))
                      << connect(m_workbook, &Workbook::cellsChanged, this,
                                 [this](const QString &sheetId, int r1, int c1, int r2, int c2) {
                                     if (sheetId != m_sheetId || rowCount() == 0 || columnCount() == 0) return;
                                     r2 = std::min(r2, rowCount() - 1);
                                     c2 = std::min(c2, columnCount() - 1);
                                     if (r1 > r2 || c1 > c2) return;
                                     // AGENT-NOTE: EditableGridModel has no public "source changed"
                                     // request; its public Qt signals are emitted so every grid view
                                     // re-reads only the cells it shows (docs/workbook.md gap G-2).
                                     emit m_grid->dataChanged(m_grid->index(r1, c1), m_grid->index(r2, c2));
                                     emit m_grid->cellsChanged();
                                     emit cellsChanged(r1, c1, r2, c2);
                                 })
                      << connect(m_workbook, &Workbook::sheetsChanged, this, [this]() {
                                     if (m_workbook->sheetIndex(m_sheetId) < 0) setSheetId(m_workbook->sheetIdAt(0));
                                     else emit sheetNameChanged();
                                 })
                      << connect(m_workbook, &Workbook::resetOccurred, this, [this]() {
                                     if (m_workbook->sheetIndex(m_sheetId) < 0) m_sheetId = m_workbook->sheetIdAt(0);
                                     rebuildIds();
                                     emit sheetIdChanged();
                                     emit sheetNameChanged();
                                 })
                      << connect(m_workbook, &QObject::destroyed, this, [this]() { setWorkbook(nullptr); });
        if (m_workbook->sheetIndex(m_sheetId) < 0) m_sheetId = m_workbook->sheetIdAt(0);
    }
    rebuildIds();
    emit workbookChanged();
    emit sheetIdChanged();
    emit sheetNameChanged();
}

void WorkbookSheetAdapter::setSheetId(const QString &sheetId)
{
    // An empty request keeps the shown sheet (the first one by default).
    if (m_sheetId == sheetId || (sheetId.isEmpty() && m_workbook)) return;
    if (m_workbook && m_workbook->sheetIndex(sheetId) < 0) {
        setError(QStringLiteral("Unknown sheet"), {{"sheetId", sheetId}});
        return;
    }
    m_sheetId = sheetId;
    rebuildIds();
    emit sheetIdChanged();
    emit sheetNameChanged();
}
QString WorkbookSheetAdapter::sheetName() const
{ return m_workbook ? m_workbook->sheetName(m_sheetId) : QString(); }
QString WorkbookSheetAdapter::copiedRange() const
{
    if (!m_workbook || m_copy.sheetId.isEmpty()) return {};
    return m_workbook->referenceText(m_sheetId, m_copy.sheetId, m_copy.row, m_copy.column,
                                     m_copy.row + m_copy.rows - 1, m_copy.column + m_copy.columns - 1);
}

void WorkbookSheetAdapter::rebuildIds()
{
    const int rows = m_workbook ? m_workbook->rowCount(m_sheetId) : 0;
    const int columns = m_workbook ? m_workbook->columnCount(m_sheetId) : 0;
    m_rowIds.clear();
    m_columnIds.clear();
    m_rowIds.reserve(rows);
    m_columnIds.reserve(columns);
    for (int i = 0; i < rows; ++i) m_rowIds.append(QStringLiteral("r:%1").arg(i + 1));
    for (int i = 0; i < columns; ++i) m_columnIds.append(QStringLiteral("c:%1").arg(i + 1));
    m_nextRowId = quint64(rows) + 1;
    m_nextColumnId = quint64(columns) + 1;
    applyIds();
    refreshAll();
}
void WorkbookSheetAdapter::applyIds()
{
    m_grid->setRowIds(m_rowIds);
    m_grid->setColumnIds(m_columnIds);
    emit dimensionsChanged();
}
void WorkbookSheetAdapter::moveIds(QStringList *ids, bool insert, int at, int count, bool rows)
{
    if (at < 0 || count < 1 || at > ids->size()) return;
    if (insert) {
        QStringList fresh;
        fresh.reserve(count);
        for (int i = 0; i < count; ++i)
            fresh.append(rows ? QStringLiteral("r:%1").arg(m_nextRowId++) : QStringLiteral("c:%1").arg(m_nextColumnId++));
        for (int i = 0; i < count; ++i) ids->insert(at + i, fresh.at(i));
    } else {
        ids->remove(at, std::min<qsizetype>(count, ids->size() - at));
    }
}
void WorkbookSheetAdapter::refreshAll()
{
    if (rowCount() == 0 || columnCount() == 0) return;
    emit m_grid->dataChanged(m_grid->index(0, 0), m_grid->index(rowCount() - 1, columnCount() - 1));
    emit m_grid->cellsChanged();
    emit cellsChanged(0, 0, rowCount() - 1, columnCount() - 1);
}

QVariant WorkbookSheetAdapter::displayForIds(const QString &rowId, const QString &columnId) const
{
    ++m_sourceReads;
    if (!m_workbook) return {};
    const int row = m_grid->rowIndex(rowId), column = m_grid->columnIndex(columnId);
    if (row < 0 || column < 0) return {};
    const QString text = m_workbook->cellDisplay(m_sheetId, row, column);
    return text.isEmpty() ? QVariant() : QVariant(text);
}
QString WorkbookSheetAdapter::validateCandidate(const QString &rowId, const QString &columnId,
                                                const QVariant &candidate) const
{
    if (m_absorbing || !m_workbook || candidate.metaType().id() != QMetaType::QString) return {};
    const QVariantMap check = m_workbook->checkInput(m_sheetId, candidate.toString());
    if (check.value(QStringLiteral("ok")).toBool()) return {};
    const int row = m_grid->rowIndex(rowId), column = m_grid->columnIndex(columnId);
    const QString where = address(row, column);
    const QString message = check.value(QStringLiteral("message")).toString();
    m_validationDetail = {{"sheetId", m_sheetId}, {"row", row}, {"column", column}, {"address", where},
                          {"input", candidate.toString()}, {"message", message},
                          {"start", check.value(QStringLiteral("start"))}, {"end", check.value(QStringLiteral("end"))}};
    return QStringLiteral("%1: %2").arg(where, message);
}

// AGENT-GUARD: this slot runs inside EditableGridModel::commitEdit(). It must
// apply the change set to the Workbook and then revert the grid's override
// layer with the exact payload; skipping the revert leaves stale typed text
// shadowing computed values, and applying twice duplicates history.
void WorkbookSheetAdapter::absorb(const QString &transactionId, const QString &label,
                                  const QVariantList &changes)
{
    bool ok = m_workbook != nullptr;
    if (ok) {
        const bool own = !m_workbook->transactionActive();
        if (own) ok = m_workbook->beginTransaction(label);
        for (const QVariant &item : changes) {
            if (!ok) break;
            const QVariantMap change = item.toMap();
            const int row = m_grid->rowIndex(change.value(QStringLiteral("rowId")).toString());
            const int column = m_grid->columnIndex(change.value(QStringLiteral("columnId")).toString());
            const QVariant after = change.value(QStringLiteral("after"));
            ok = row >= 0 && column >= 0
                && m_workbook->setCellInput(m_sheetId, row, column, after.isValid() ? after.toString() : QString());
        }
        if (own && ok) ok = m_workbook->commitTransaction();
        else if (own && m_workbook->transactionActive()) m_workbook->cancelTransaction();
    }
    m_absorbing = true;
    const bool reverted = m_grid->applyHistoryChanges(transactionId, changes, true, m_grid->revision());
    m_grid->releaseHistoryTransaction(transactionId);
    if (!reverted) {
        // Defensive: a fresh source drops every override without touching the Workbook.
        m_grid->setSource(nullptr);
        m_grid->setSource(m_source);
    }
    m_absorbing = false;
    report(ok);
}

bool WorkbookSheetAdapter::report(bool ok)
{
    if (ok) {
        if (!m_lastError.isEmpty()) setError({}, {});
        return true;
    }
    setError(m_workbook ? m_workbook->lastError() : QStringLiteral("No workbook"),
             m_workbook ? m_workbook->lastErrorDetail() : QVariantMap());
    return false;
}
void WorkbookSheetAdapter::setError(const QString &message, const QVariantMap &detail)
{
    m_lastError = message;
    m_lastErrorDetail = detail;
    emit errorChanged();
}

QString WorkbookSheetAdapter::address(int row, int column) const
{ return m_workbook ? m_workbook->address(row, column) : Calc::cellAddress(row, column); }
QString WorkbookSheetAdapter::inputAt(int row, int column) const
{ return m_workbook ? m_workbook->cellInput(m_sheetId, row, column) : QString(); }
QString WorkbookSheetAdapter::displayAt(int row, int column) const
{ return m_workbook ? m_workbook->cellDisplay(m_sheetId, row, column) : QString(); }
QVariantMap WorkbookSheetAdapter::cellInfo(int row, int column) const
{ return m_workbook ? m_workbook->cellInfo(m_sheetId, row, column) : QVariantMap(); }
QVariantMap WorkbookSheetAdapter::checkInput(const QString &input) const
{ return m_workbook ? m_workbook->checkInput(m_sheetId, input) : QVariantMap{{"ok", false}}; }
bool WorkbookSheetAdapter::commitInput(int row, int column, const QString &input)
{ return report(m_workbook && m_workbook->setCellInput(m_sheetId, row, column, input)); }

bool WorkbookSheetAdapter::fillDown(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    const int top = std::min(firstRow, lastRow), bottom = std::max(firstRow, lastRow);
    const int left = std::min(firstColumn, lastColumn), width = std::abs(lastColumn - firstColumn) + 1;
    if (!m_workbook) return report(false);
    if (bottom > top)
        return report(m_workbook->fill(m_sheetId, top, left, 1, width, top + 1, left, bottom - top, width));
    if (top == 0) { setError(QStringLiteral("Nothing above to fill down from"), {}); return false; }
    return report(m_workbook->fill(m_sheetId, top - 1, left, 1, width, top, left, 1, width));
}
bool WorkbookSheetAdapter::fillRight(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    const int left = std::min(firstColumn, lastColumn), right = std::max(firstColumn, lastColumn);
    const int top = std::min(firstRow, lastRow), height = std::abs(lastRow - firstRow) + 1;
    if (!m_workbook) return report(false);
    if (right > left)
        return report(m_workbook->fill(m_sheetId, top, left, height, 1, top, left + 1, height, right - left));
    if (left == 0) { setError(QStringLiteral("Nothing to the left to fill right from"), {}); return false; }
    return report(m_workbook->fill(m_sheetId, top, left - 1, height, 1, top, left, height, 1));
}
bool WorkbookSheetAdapter::fillRange(int firstRow, int firstColumn, int rowCount, int columnCount,
                                     int targetRow, int targetColumn, int targetRowCount, int targetColumnCount)
{
    return report(m_workbook && m_workbook->fill(m_sheetId, firstRow, firstColumn, rowCount, columnCount,
                                                 targetRow, targetColumn, targetRowCount, targetColumnCount));
}
bool WorkbookSheetAdapter::copyRange(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    if (!m_workbook || firstRow < 0 || firstColumn < 0 || lastRow >= rowCount() || lastColumn >= columnCount()
        || lastRow < 0 || lastColumn < 0) {
        setError(QStringLiteral("Copy range is outside the sheet"), {});
        return false;
    }
    m_copy = {m_sheetId, std::min(firstRow, lastRow), std::min(firstColumn, lastColumn),
              std::abs(lastRow - firstRow) + 1, std::abs(lastColumn - firstColumn) + 1};
    emit copiedRangeChanged();
    return report(true);
}
bool WorkbookSheetAdapter::pasteCopied(int row, int column)
{
    if (!m_workbook || m_copy.sheetId.isEmpty()) {
        setError(QStringLiteral("Nothing has been copied"), {});
        return false;
    }
    return report(m_workbook->copyCells(m_copy.sheetId, m_copy.row, m_copy.column, m_copy.rows,
                                        m_copy.columns, m_sheetId, row, column));
}
bool WorkbookSheetAdapter::clearRange(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    return report(m_workbook && m_workbook->clearCells(m_sheetId, std::min(firstRow, lastRow),
                                                       std::min(firstColumn, lastColumn),
                                                       std::abs(lastRow - firstRow) + 1,
                                                       std::abs(lastColumn - firstColumn) + 1));
}
QVariantList WorkbookSheetAdapter::parseTabularText(const QString &text) const
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (normalized.endsWith(QLatin1Char('\n'))) normalized.chop(1);
    QVariantList rows;
    if (normalized.isEmpty()) return rows;
    for (const QString &line : normalized.split(QLatin1Char('\n'))) {
        QVariantList fields;
        for (const QString &field : line.split(QLatin1Char('\t'))) fields.append(field);
        rows.append(QVariant(fields));
    }
    return rows;
}

} // namespace QindaTK
