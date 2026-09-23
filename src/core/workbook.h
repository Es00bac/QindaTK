// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "workbook_formula.h"
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {
struct WorkbookPrivate;

// AGENT-CONTRACT: Workbook is the single authoritative spreadsheet model and
// the only revision/history owner for its cells, sheets and formulas. Views
// (WorkbookSheetAdapter/SheetView, FormulaBar, WorkbookTabs) read it and
// route every change through these operations; none keeps cell state.
// docs/workbook.md defines the grammar, functions, reference rules,
// transactions and the native file format.
class Workbook : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString documentId READ documentId NOTIFY resetOccurred)
    Q_PROPERTY(QString revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString resetGeneration READ resetGeneration NOTIFY resetOccurred)
    Q_PROPERTY(QVariantList sheets READ sheets NOTIFY sheetsChanged)
    Q_PROPERTY(int sheetCount READ sheetCount NOTIFY sheetsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY historyChanged)
    Q_PROPERTY(bool transactionActive READ transactionActive NOTIFY transactionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap lastErrorDetail READ lastErrorDetail NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY revisionChanged)
    Q_PROPERTY(QStringList functionNames READ functionNames CONSTANT)
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileChanged)
public:
    static constexpr int DefaultRows = 1000;
    static constexpr int DefaultColumns = 26;
    static constexpr int MaxSheets = 256;
    static constexpr int MaxTransactionCells = 65536;
    static constexpr int MaxVisibleCells = 4096;
    static constexpr int MaxHistorySteps = 200;
    static constexpr qsizetype MaxTextLength = 32767;
    static constexpr qsizetype MaxFileBytes = 64 * 1024 * 1024;

    explicit Workbook(QObject *parent = nullptr);
    ~Workbook() override;

    QString documentId() const;
    QString revision() const;
    QString resetGeneration() const;
    QVariantList sheets() const;
    int sheetCount() const;
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool isModified() const;
    bool transactionActive() const;
    QString lastError() const;
    QVariantMap lastErrorDetail() const;
    QVariantMap statistics() const;
    QStringList functionNames() const { return Calc::functionNames(); }
    QUrl fileUrl() const;

    // Sheets: IDs are stable, never reused within a document and persisted.
    Q_INVOKABLE QString addSheet(const QString &name = {}, int index = -1,
                                 int rows = DefaultRows, int columns = DefaultColumns);
    Q_INVOKABLE bool removeSheet(const QString &sheetId);
    Q_INVOKABLE bool renameSheet(const QString &sheetId, const QString &name);
    Q_INVOKABLE bool moveSheet(const QString &sheetId, int index);
    Q_INVOKABLE QString sheetIdAt(int index) const;
    Q_INVOKABLE int sheetIndex(const QString &sheetId) const;
    Q_INVOKABLE QString sheetName(const QString &sheetId) const;
    Q_INVOKABLE QString sheetIdForName(const QString &name) const;
    Q_INVOKABLE int rowCount(const QString &sheetId) const;
    Q_INVOKABLE int columnCount(const QString &sheetId) const;

    // Cells. `input` is what a user types: "=..." formula, "'" text escape,
    // TRUE/FALSE, error literal, C-locale number, other text, "" to clear.
    Q_INVOKABLE bool setCellInput(const QString &sheetId, int row, int column, const QString &input);
    Q_INVOKABLE QString cellInput(const QString &sheetId, int row, int column) const;
    Q_INVOKABLE QString cellDisplay(const QString &sheetId, int row, int column) const;
    Q_INVOKABLE QVariant cellValue(const QString &sheetId, int row, int column) const;
    Q_INVOKABLE QString cellType(const QString &sheetId, int row, int column) const;
    Q_INVOKABLE QVariantMap cellInfo(const QString &sheetId, int row, int column) const;
    // Non-blank cells of a window of at most MaxVisibleCells positions.
    Q_INVOKABLE QVariantList visibleCells(const QString &sheetId, int firstRow, int rowCount,
                                          int firstColumn, int columnCount) const;
    // Validates an input without changing the workbook: {ok, kind, message,
    // start, end, references:[{start,end,text,sheetId,firstRow,...}]}.
    Q_INVOKABLE QVariantMap checkInput(const QString &sheetId, const QString &input) const;

    // Range operations; each is one atomic, reversible transaction step.
    Q_INVOKABLE bool pasteInputs(const QString &sheetId, int row, int column, const QVariantList &rows);
    Q_INVOKABLE bool copyCells(const QString &sourceSheetId, int firstRow, int firstColumn,
                               int rowCount, int columnCount, const QString &targetSheetId,
                               int targetRow, int targetColumn);
    Q_INVOKABLE bool fill(const QString &sheetId, int firstRow, int firstColumn, int rowCount,
                          int columnCount, int targetRow, int targetColumn, int targetRowCount,
                          int targetColumnCount);
    Q_INVOKABLE bool clearCells(const QString &sheetId, int firstRow, int firstColumn,
                                int rowCount, int columnCount);
    Q_INVOKABLE bool insertRows(const QString &sheetId, int row, int count = 1);
    Q_INVOKABLE bool removeRows(const QString &sheetId, int row, int count = 1);
    Q_INVOKABLE bool insertColumns(const QString &sheetId, int column, int count = 1);
    Q_INVOKABLE bool removeColumns(const QString &sheetId, int column, int count = 1);

    // Explicit transactions group operations into one history step. Values
    // are recalculated at commit; cancel restores the exact prior state.
    Q_INVOKABLE bool beginTransaction(const QString &label);
    Q_INVOKABLE bool commitTransaction();
    Q_INVOKABLE bool cancelTransaction();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void markSaved();

    // Native versioned format (docs/workbook.md). Restore is all-or-nothing,
    // replaces the document, clears history and recalculates every formula.
    // serialize() captures committed state; it is empty during a transaction.
    QByteArray serialize() const;
    bool restore(const QByteArray &bytes);
    // Immutable payload for FormatProvider export (schema qindatk.workbook.v1).
    SnapshotPtr snapshot() const;
    Q_INVOKABLE QString serializedText() const;
    Q_INVOKABLE bool restoreText(const QString &text);
    // Atomic local-file save. An existing file is replaced only when it still
    // matches the bytes this workbook last read or wrote there, or when
    // `replaceExisting` is true for a file it has not seen.
    Q_INVOKABLE bool save(const QUrl &url, bool replaceExisting = false);
    Q_INVOKABLE bool open(const QUrl &url);
    Q_INVOKABLE void reset();
    // Adds the qindatk.workbook.v1 provider to a Tk.FormatRegistry.
    Q_INVOKABLE bool registerFormat(QObject *formatRegistry) const;

    Q_INVOKABLE QString columnName(int column) const { return Calc::columnName(column); }
    // Signature text for a supported function name, or "" (FormulaBar hints).
    Q_INVOKABLE QString functionSignature(const QString &name) const;
    Q_INVOKABLE QString address(int row, int column) const;
    Q_INVOKABLE QString rangeAddress(int firstRow, int firstColumn, int lastRow, int lastColumn) const;
    Q_INVOKABLE QVariantMap parseAddress(const QString &text) const;
    Q_INVOKABLE QString referenceText(const QString &fromSheetId, const QString &toSheetId,
                                      int firstRow, int firstColumn, int lastRow, int lastColumn) const;

signals:
    void revisionChanged();
    void historyChanged();
    void sheetsChanged();
    void transactionChanged();
    void errorChanged();
    void resetOccurred();
    void fileChanged();
    void rejected(const QString &reason);
    // Inputs or computed values changed inside this bounding rectangle.
    void cellsChanged(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn);
    void rowsInserted(const QString &sheetId, int row, int count);
    void rowsRemoved(const QString &sheetId, int row, int count);
    void columnsInserted(const QString &sheetId, int column, int count);
    void columnsRemoved(const QString &sheetId, int column, int count);

private:
    friend struct WorkbookPrivate;
    std::unique_ptr<WorkbookPrivate> d;
};

} // namespace QindaTK
