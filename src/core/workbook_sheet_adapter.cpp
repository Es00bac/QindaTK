// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_sheet_adapter.h"

namespace QindaTK {

class WorkbookSheetAdapter::Source final : public GridCellSource, public GridCellSourceExtension {
public:
    explicit Source(WorkbookSheetAdapter *adapter) : GridCellSource(adapter), m_adapter(adapter) {}
    QVariant value(const QString &rowId, const QString &columnId) const override
    { return m_adapter->displayForIds(rowId, columnId); }
    QString validate(const QString &rowId, const QString &columnId, const QVariant &candidate) const override
    { return m_adapter->validateCandidate(rowId, columnId, candidate); }
    QVariant editValue(const QString &rowId, const QString &columnId) const override
    {
        const int row = m_adapter->rowFor(rowId), column = m_adapter->columnFor(columnId);
        return row < 0 || column < 0 ? QVariant() : QVariant(m_adapter->inputAt(row, column));
    }
    QVariantMap cellStyle(const QString &rowId, const QString &columnId) const override
    {
        const Workbook *wb = m_adapter->m_workbook;
        const int row = m_adapter->rowFor(rowId), column = m_adapter->columnFor(columnId);
        return wb && row >= 0 && column >= 0 ? wb->cellPresentation(m_adapter->m_sheetId, row, column) : QVariantMap();
    }
    QVector<QRect> mergedRanges(const QRect &window) const override
    {
        QVector<QRect> result;
        if (!m_adapter->m_workbook) return result;
        const QVariantList areas = m_adapter->m_workbook->mergedAreas(m_adapter->m_sheetId, window.top(), window.left(),
                                                                      window.bottom(), window.right());
        for (const QVariant &item : areas) {
            const QVariantMap a = item.toMap();
            result.append(QRect(QPoint(a.value(QStringLiteral("firstColumn")).toInt(), a.value(QStringLiteral("firstRow")).toInt()),
                                QPoint(a.value(QStringLiteral("lastColumn")).toInt(), a.value(QStringLiteral("lastRow")).toInt())));
        }
        return result;
    }
    bool commitChanges(const QString &label, const QVariantList &changes, QString *error) override
    { return m_adapter->applyGridChanges(label, changes, error); }
    bool fillCells(const QRect &source, const QRect &target, QString *error) override
    {
        if (m_adapter->fillRange(source.top(), source.left(), source.height(), source.width(), target.top(),
                                 target.left(), target.height(), target.width())) return true;
        *error = m_adapter->lastError();
        m_adapter->m_validationDetail = m_adapter->lastErrorDetail();
        return false;
    }
private:
    WorkbookSheetAdapter *m_adapter;
};

WorkbookSheetAdapter::WorkbookSheetAdapter(QObject *parent)
    : QObject(parent), m_grid(new EditableGridModel(this)), m_source(new Source(this))
{
    m_grid->setSource(m_source);
    m_grid->setSourceCommits(true);
    connect(m_grid, &EditableGridModel::rejected, this, [this](const QString &reason) {
        setError(reason, m_validationDetail);
        m_validationDetail.clear();
    });
    connect(m_grid, &EditableGridModel::resizeFinished, this, [this](const QString &id, bool rows, qreal size) {
        if (m_applyingProperties || !m_workbook) return;
        const int index = rows ? m_grid->rowIndex(id) : m_grid->columnIndex(id);
        const bool ok = rows ? m_workbook->setRowHeight(m_sheetId, index, size)
                             : m_workbook->setColumnWidth(m_sheetId, index, size);
        if (!report(ok)) applyProperties(); // e.g. protected: snap back to the Workbook's size
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
                // A replayed step may restructure a sheet that is still locked
                // until its properties arrive: structure sync bypasses readOnly.
                const bool locked = m_grid->readOnly();
                if (locked) m_grid->setReadOnly(false);
                // Fresh generated IDs for inserted sections; removed ones take their IDs along.
                if (insert) rows ? m_grid->insertGeneratedRowsAt(at, count) : m_grid->insertGeneratedColumnsAt(at, count);
                else rows ? m_grid->removeRowsAt(at, count) : m_grid->removeColumnsAt(at, count);
                if (locked) m_grid->setReadOnly(true);
                emit dimensionsChanged();
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
                                     m_grid->notifySourceChanged(r1, r2 - r1 + 1, c1, c2 - c1 + 1);
                                     emit cellsChanged(r1, c1, r2, c2);
                                 })
                      << connect(m_workbook, &Workbook::sheetPropertiesChanged, this, [this](const QString &sheetId) {
                                     if (sheetId == m_sheetId) applyProperties();
                                 })
                      << connect(m_workbook, &Workbook::sheetsChanged, this, [this]() {
                                     if (m_workbook->sheetIndex(m_sheetId) < 0) setSheetId(m_workbook->sheetIdAt(0));
                                     else emit sheetNameChanged();
                                 })
                      << connect(m_workbook, &Workbook::resetOccurred, this, [this]() {
                                     const bool kept = m_workbook->sheetIndex(m_sheetId) >= 0;
                                     if (!kept) m_sheetId = m_workbook->sheetIdAt(0);
                                     rebuildIds(!kept);
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

// AGENT-NOTE: showing a sheet empties the grid (dropping selection and grid
// history) and regrows it with generated ID runs — r:1.., c:1.. — instead of
// one string per section (docs/workbook.md G-8).
void WorkbookSheetAdapter::rebuildIds(bool fresh)
{
    const int rows = m_workbook ? m_workbook->rowCount(m_sheetId) : 0;
    const int columns = m_workbook ? m_workbook->columnCount(m_sheetId) : 0;
    m_grid->setReadOnly(false); // applyProperties locks a protected sheet again
    if (fresh) {
        m_grid->setDimensions(0, 0);
        m_sizedRows.clear();
        m_sizedColumns.clear();
    }
    m_grid->setDimensions(rows, columns);
    applyProperties();
    emit dimensionsChanged();
    refreshAll();
}

void WorkbookSheetAdapter::applyProperties()
{
    if (!m_workbook) return;
    m_applyingProperties = true;
    const QVariantMap properties = m_workbook->sheetProperties(m_sheetId);
    // A protected sheet is read-only in the grid: no editor, paste or fill handle.
    m_grid->setReadOnly(properties.value(QStringLiteral("protected")).toBool());
    m_grid->setFrozenRows(properties.value(QStringLiteral("frozenRows")).toInt());
    m_grid->setFrozenColumns(properties.value(QStringLiteral("frozenColumns")).toInt());
    const auto apply = [this](const QVariantMap &sizes, bool rows, QSet<QString> *sized) {
        QSet<QString> next;
        for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) {
            const QString id = rows ? m_grid->rowIdAt(it.key().toInt()) : m_grid->columnIdAt(it.key().toInt());
            if (id.isEmpty()) continue;
            next.insert(id);
            rows ? m_grid->requestRowResize(id, it.value().toDouble()) : m_grid->requestColumnResize(id, it.value().toDouble());
        }
        for (const QString &id : std::as_const(*sized))
            if (!next.contains(id)) rows ? m_grid->resetRowSize(id) : m_grid->resetColumnSize(id);
        *sized = next;
    };
    apply(m_workbook->rowHeights(m_sheetId), true, &m_sizedRows);
    apply(m_workbook->columnWidths(m_sheetId), false, &m_sizedColumns);
    m_applyingProperties = false;
}

void WorkbookSheetAdapter::refreshAll()
{
    if (rowCount() == 0 || columnCount() == 0) return;
    m_grid->notifySourceChanged(0, rowCount(), 0, columnCount());
    emit cellsChanged(0, 0, rowCount() - 1, columnCount() - 1);
}

int WorkbookSheetAdapter::rowFor(const QString &id) const
{
    if (m_memoStamp != m_grid->revisionValue()) {
        m_memoStamp = m_grid->revisionValue();
        m_memoRowId.clear();
        m_memoColumnId.clear();
    }
    if (id != m_memoRowId || m_memoRowId.isEmpty()) { m_memoRowId = id; m_memoRow = m_grid->rowIndex(id); }
    return m_memoRow;
}
int WorkbookSheetAdapter::columnFor(const QString &id) const
{
    if (m_memoStamp != m_grid->revisionValue()) {
        m_memoStamp = m_grid->revisionValue();
        m_memoRowId.clear();
        m_memoColumnId.clear();
    }
    if (id != m_memoColumnId || m_memoColumnId.isEmpty()) { m_memoColumnId = id; m_memoColumn = m_grid->columnIndex(id); }
    return m_memoColumn;
}

QVariant WorkbookSheetAdapter::displayForIds(const QString &rowId, const QString &columnId) const
{
    ++m_sourceReads;
    if (!m_workbook) return {};
    const int row = rowFor(rowId), column = columnFor(columnId);
    if (row < 0 || column < 0) return {};
    const QString text = m_workbook->cellDisplay(m_sheetId, row, column);
    return text.isEmpty() ? QVariant() : QVariant(text);
}
QString WorkbookSheetAdapter::validateCandidate(const QString &rowId, const QString &columnId,
                                                const QVariant &candidate) const
{
    if (!m_workbook || candidate.metaType().id() != QMetaType::QString) return {};
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

// AGENT-GUARD: this runs inside EditableGridModel::commitEdit() (sourceCommits).
// Returning false keeps the grid's editor open with the typed text; the
// grid reports `error` through `rejected`, so m_validationDetail must hold
// the matching detail before returning.
bool WorkbookSheetAdapter::applyGridChanges(const QString &label, const QVariantList &changes, QString *error)
{
    if (!m_workbook) { *error = QStringLiteral("No workbook"); return false; }
    QVariantList cells;
    for (const QVariant &item : changes) {
        const QVariantMap change = item.toMap();
        const QVariant after = change.value(QStringLiteral("after"));
        cells.append(QVariantMap{{"row", m_grid->rowIndex(change.value(QStringLiteral("rowId")).toString())},
                                 {"column", m_grid->columnIndex(change.value(QStringLiteral("columnId")).toString())},
                                 {"input", after.isValid() ? after.toString() : QString()}});
    }
    if (cells.size() == 1) {
        const QVariantMap cell = cells.first().toMap();
        const int row = cell.value(QStringLiteral("row")).toInt(), column = cell.value(QStringLiteral("column")).toInt();
        const QString input = cell.value(QStringLiteral("input")).toString();
        const QVariantMap check = m_workbook->validateInput(m_sheetId, row, column, input);
        const QString style = check.value(QStringLiteral("style")).toString();
        if (!check.value(QStringLiteral("ok")).toBool() && !check.value(QStringLiteral("syntax")).toBool() && style != u"stop") {
            *error = check.value(QStringLiteral("message")).toString();
            m_validationDetail = check;
            emit validationPrompt(row, column, input, check);
            return false;
        }
    }
    if (m_workbook->setCellInputs(m_sheetId, cells, QStringLiteral("allOrNothing"), label)) {
        report(true);
        return true;
    }
    *error = m_workbook->lastError();
    m_validationDetail = m_workbook->lastErrorDetail();
    return false;
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
QVariantMap WorkbookSheetAdapter::validateInput(int row, int column, const QString &input) const
{ return m_workbook ? m_workbook->validateInput(m_sheetId, row, column, input) : QVariantMap{{"ok", false}}; }
QVariantMap WorkbookSheetAdapter::validationAt(int row, int column) const
{ return m_workbook ? m_workbook->validationAt(m_sheetId, row, column) : QVariantMap(); }
QStringList WorkbookSheetAdapter::validationItems(int row, int column) const
{ return m_workbook ? m_workbook->validationItems(m_sheetId, row, column) : QStringList(); }

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
bool WorkbookSheetAdapter::pasteInputs(int row, int column, const QVariantList &rows, const QString &policy)
{ return report(m_workbook && m_workbook->pasteInputs(m_sheetId, row, column, rows, policy)); }
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
