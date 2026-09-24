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
class Workbook : public QObject, public SnapshotSource, public UndoParticipant {
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
    Q_PROPERTY(QVariantList names READ names NOTIFY namesChanged)
    Q_PROPERTY(QVariantMap lastPasteReport READ lastPasteReport NOTIFY pasteReported)
public:
    static constexpr int DefaultRows = 1000;
    static constexpr int DefaultColumns = 26;
    static constexpr int MaxSheets = 256;
    static constexpr int MaxTransactionCells = 65536;
    static constexpr int MaxVisibleCells = 4096;
    static constexpr int MaxHistorySteps = 200;
    // Estimated undo data the history retains; the oldest steps go first.
    static constexpr qint64 MaxHistoryBytes = 64 * 1024 * 1024;
    static constexpr qsizetype MaxTextLength = 32767;
    static constexpr qsizetype MaxFileBytes = 64 * 1024 * 1024;

    explicit Workbook(QObject *parent = nullptr);
    ~Workbook() override;

    QString documentId() const;
    QString revision() const;
    QString resetGeneration() const;
    QVariantList sheets() const;
    int sheetCount() const;
    bool canUndo() const override;
    bool canRedo() const override;
    QString undoText() const override;
    QString redoText() const override;
    bool isModified() const override;
    bool transactionActive() const;
    QString lastError() const;
    QVariantMap lastErrorDetail() const;
    QVariantMap statistics() const;
    QStringList functionNames() const { return Calc::functionNames(); }
    QUrl fileUrl() const;
    QVariantList names() const;
    QVariantMap lastPasteReport() const;

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
    // {lastRow, lastColumn} of the stored cells ({} for an empty sheet).
    Q_INVOKABLE QVariantMap usedRange(const QString &sheetId) const;
    // Non-blank cells of a window of at most MaxVisibleCells positions.
    Q_INVOKABLE QVariantList visibleCells(const QString &sheetId, int firstRow, int rowCount,
                                          int firstColumn, int columnCount) const;
    // Validates an input without changing the workbook: {ok, kind, message,
    // start, end, references:[{start,end,text,sheetId,firstRow,...}]}.
    Q_INVOKABLE QVariantMap checkInput(const QString &sheetId, const QString &input) const;

    // Range operations; each is one atomic, reversible transaction step.
    // Pastes validate each cell: policy "allOrNothing" applies nothing when a
    // stop-style validation fails, "skipInvalid" applies the valid cells;
    // lastPasteReport lists every invalid cell either way.
    Q_INVOKABLE bool pasteInputs(const QString &sheetId, int row, int column, const QVariantList &rows,
                                 const QString &policy = QStringLiteral("allOrNothing"));
    // Clipboard ranges (docs/workbook.md#clipboard). rangeTransfer returns
    // {ok, range (a TransferItemKind::Range payload), text (tab-separated
    // display values)} or {ok: false, message}; pasteTransfer pastes such a
    // range from this or another workbook as one "Paste cells" step.
    Q_INVOKABLE QVariantMap rangeTransfer(const QString &sheetId, int firstRow, int firstColumn, int lastRow,
                                          int lastColumn) const;
    Q_INVOKABLE bool pasteTransfer(const QString &sheetId, int row, int column, const QVariantMap &range,
                                   const QString &policy = QStringLiteral("allOrNothing"));
    // Inputs [{row, column, input}] as one transaction under a paste policy.
    Q_INVOKABLE bool setCellInputs(const QString &sheetId, const QVariantList &cells,
                                   const QString &policy = QStringLiteral("allOrNothing"),
                                   const QString &label = {});
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

    // Presentation (docs/workbook.md#formats-rules-and-names). Style keys:
    // numberFormat, horizontalAlignment (general|left|center|right), bold,
    // italic, underline, strikeout, textColor and fillColor ("#rrggbb"; ""
    // restores the theme default). A patch changes only the keys it names.
    Q_INVOKABLE bool applyStyle(const QString &sheetId, int firstRow, int firstColumn, int lastRow,
                                int lastColumn, const QVariantMap &patch);
    Q_INVOKABLE bool clearFormats(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn);
    // Explicit formatting of one cell, without conditional formats.
    Q_INVOKABLE QVariantMap cellFormat(const QString &sheetId, int row, int column) const;
    // What a view draws: formats, then conditional formats (which win), the
    // general alignment of the value's type, dataBar/icon, error and numeric flags.
    Q_INVOKABLE QVariantMap cellPresentation(const QString &sheetId, int row, int column) const;
    Q_INVOKABLE QString numberFormatProblem(const QString &format) const;

    // Merged areas: disjoint rectangles of two or more cells; merging keeps
    // the top-left input and clears the others (one undo step).
    Q_INVOKABLE bool mergeCells(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn);
    Q_INVOKABLE bool unmergeCells(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn);
    Q_INVOKABLE QVariantList mergedAreas(const QString &sheetId, int firstRow, int firstColumn, int lastRow,
                                         int lastColumn) const;
    Q_INVOKABLE QVariantMap mergeAt(const QString &sheetId, int row, int column) const;

    // Sheet properties: tabColor, hidden, protected, frozenRows, frozenColumns.
    // A protected sheet refuses cell, structure and format edits.
    Q_INVOKABLE QVariantMap sheetProperties(const QString &sheetId) const;
    Q_INVOKABLE bool setSheetProperties(const QString &sheetId, const QVariantMap &changes);
    // Sizes in device-independent pixels; a size <= 0 restores the default.
    Q_INVOKABLE bool setColumnWidth(const QString &sheetId, int column, double width);
    Q_INVOKABLE bool setRowHeight(const QString &sheetId, int row, double height);
    Q_INVOKABLE QVariantMap columnWidths(const QString &sheetId) const; // {"<index>": size}
    Q_INVOKABLE QVariantMap rowHeights(const QString &sheetId) const;

    // Workbook names: formulas use them like ranges (=SUM(Sales)).
    Q_INVOKABLE bool defineName(const QString &name, const QString &sheetId, int firstRow, int firstColumn,
                                int lastRow, int lastColumn);
    Q_INVOKABLE bool removeName(const QString &name);
    Q_INVOKABLE QString nameProblem(const QString &name) const;

    // Conditional formats in priority order (index 0 applies first). A rule
    // map: id, ranges ["A1:B5"], type, operator, value1, value2, text, style,
    // minColor/midColor/maxColor and stopIfTrue (docs/workbook.md).
    Q_INVOKABLE QVariantList conditionalFormats(const QString &sheetId) const;
    Q_INVOKABLE QVariantMap checkConditionalFormat(const QString &sheetId, const QVariantMap &rule) const;
    // Adds (unknown or empty id) or replaces a rule; returns its id or "".
    // priority -1 keeps a replaced rule's place and puts a new one first.
    Q_INVOKABLE QString setConditionalFormat(const QString &sheetId, const QVariantMap &rule, int priority = -1);
    Q_INVOKABLE bool removeConditionalFormat(const QString &sheetId, const QString &ruleId);
    Q_INVOKABLE bool moveConditionalFormat(const QString &sheetId, const QString &ruleId, int priority);
    // cellPresentation of a window as if `rule` were set (the editor preview).
    Q_INVOKABLE QVariantList previewConditionalFormat(const QString &sheetId, const QVariantMap &rule, int firstRow,
                                                      int firstColumn, int rowCount, int columnCount) const;

    // Data validation; each cell has at most one rule (a new rule takes its
    // cells from older ones). Rule map: id, ranges, type, operator, value1,
    // value2, items, allowBlank, inputTitle, inputMessage, errorStyle,
    // errorTitle, errorMessage.
    Q_INVOKABLE QVariantList validations(const QString &sheetId) const;
    Q_INVOKABLE QVariantMap checkValidation(const QString &sheetId, const QVariantMap &rule) const;
    Q_INVOKABLE QString setValidation(const QString &sheetId, const QVariantMap &rule);
    Q_INVOKABLE bool removeValidation(const QString &sheetId, const QString &ruleId);
    Q_INVOKABLE QVariantMap validationAt(const QString &sheetId, int row, int column) const;
    // The allowed values of a list rule (inline items or its source range).
    Q_INVOKABLE QStringList validationItems(const QString &sheetId, int row, int column) const;
    // {ok, style, title, message, ruleId}; setCellInput refuses only "stop".
    Q_INVOKABLE QVariantMap validateInput(const QString &sheetId, int row, int column, const QString &input) const;

    // Explicit transactions group operations into one history step. Values
    // are recalculated at commit; cancel restores the exact prior state.
    Q_INVOKABLE bool beginTransaction(const QString &label);
    Q_INVOKABLE bool commitTransaction();
    Q_INVOKABLE bool cancelTransaction();
    Q_INVOKABLE bool undo() override;
    Q_INVOKABLE bool redo() override;
    Q_INVOKABLE void markSaved() override;
    // The current state is not saved anywhere (an import or a recovery).
    Q_INVOKABLE void markUnsaved();

    // Native versioned format (docs/workbook.md). Restore is all-or-nothing,
    // replaces the document, clears history and recalculates every formula.
    // serialize() captures committed state; it is empty during a transaction.
    QByteArray serialize() const;
    bool restore(const QByteArray &bytes);
    // Immutable payload for FormatProvider export (schema qindatk.workbook.v1).
    SnapshotPtr snapshot() const;
    SnapshotPtr exportSnapshot() const override { return snapshot(); }
    Q_INVOKABLE QString serializedText() const;
    Q_INVOKABLE bool restoreText(const QString &text);
    // Atomic local-file save. An existing file is replaced only when it still
    // matches the bytes this workbook last read or wrote there, or when
    // `replaceExisting` is true for a file it has not seen.
    Q_INVOKABLE bool save(const QUrl &url, bool replaceExisting = false);
    Q_INVOKABLE bool open(const QUrl &url);
    Q_INVOKABLE void reset();
    // Adds the native (qindatk.workbook.v1), CSV (text.csv) and TSV (text.tsv)
    // providers to a Tk.FormatRegistry; false when one was already registered.
    Q_INVOKABLE bool registerFormat(QObject *formatRegistry) const;
    // Replaces this document with a succeeded registry import job of a
    // workbook provider (all-or-nothing, like restore); the file stays unsaved.
    Q_INVOKABLE bool adoptImport(QObject *formatRegistry, const QString &jobId);

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
    void sheetPropertiesChanged(const QString &sheetId);
    void namesChanged();
    void pasteReported();

private:
    friend struct WorkbookPrivate;
    std::unique_ptr<WorkbookPrivate> d;
};

} // namespace QindaTK
