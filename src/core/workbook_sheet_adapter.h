// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "editable_grid_model.h"
#include "workbook.h"
#include <QPointer>
#include <QSet>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: presents one sheet of a Workbook through the existing
// EditableGridModel contract (docs/editable-grid.md#domain-sources) without a
// second store. Its GridCellSource returns display text for requested cells
// only and, as a GridCellSourceExtension, the formula source for editors,
// resolved cell presentation and merged areas. The grid runs in
// sourceCommits mode: a committed edit, paste or fill-handle drag is applied
// to the Workbook as one transaction and the grid keeps no cell values or
// history. Structural workbook changes move the grid's stable row/column IDs
// (compact generated runs); frozen panes and section sizes follow the
// sheet's properties, and a finished header resize is recorded in the
// Workbook (one undo step per gesture).
class WorkbookSheetAdapter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::Workbook *workbook READ workbook WRITE setWorkbook NOTIFY workbookChanged)
    Q_PROPERTY(QString sheetId READ sheetId WRITE setSheetId NOTIFY sheetIdChanged)
    Q_PROPERTY(QString sheetName READ sheetName NOTIFY sheetNameChanged)
    Q_PROPERTY(QindaTK::EditableGridModel *gridModel READ gridModel CONSTANT)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY dimensionsChanged)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY dimensionsChanged)
    Q_PROPERTY(bool hasCopiedRange READ hasCopiedRange NOTIFY copiedRangeChanged)
    Q_PROPERTY(QString copiedRange READ copiedRange NOTIFY copiedRangeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap lastErrorDetail READ lastErrorDetail NOTIFY errorChanged)
public:
    explicit WorkbookSheetAdapter(QObject *parent = nullptr);
    ~WorkbookSheetAdapter() override;

    Workbook *workbook() const { return m_workbook; }
    void setWorkbook(Workbook *workbook);
    QString sheetId() const { return m_sheetId; }
    void setSheetId(const QString &sheetId);
    QString sheetName() const;
    EditableGridModel *gridModel() const { return m_grid; }
    int rowCount() const { return m_grid->rows(); }
    int columnCount() const { return m_grid->columns(); }
    bool hasCopiedRange() const { return !m_copy.sheetId.isEmpty(); }
    QString copiedRange() const;
    QString lastError() const { return m_lastError; }
    QVariantMap lastErrorDetail() const { return m_lastErrorDetail; }

    Q_INVOKABLE QString columnLabel(int column) const { return Calc::columnName(column); }
    Q_INVOKABLE QString address(int row, int column) const;
    Q_INVOKABLE QString inputAt(int row, int column) const;
    Q_INVOKABLE QString displayAt(int row, int column) const;
    Q_INVOKABLE QVariantMap cellInfo(int row, int column) const;
    Q_INVOKABLE QVariantMap checkInput(const QString &input) const;
    // Applies one input; an unchanged input is a no-op without history.
    Q_INVOKABLE bool commitInput(int row, int column, const QString &input);
    // Workbook.validateInput / validationAt / validationItems for this sheet.
    Q_INVOKABLE QVariantMap validateInput(int row, int column, const QString &input) const;
    Q_INVOKABLE QVariantMap validationAt(int row, int column) const;
    Q_INVOKABLE QStringList validationItems(int row, int column) const;
    // Excel-style fills over an inclusive selection rectangle.
    Q_INVOKABLE bool fillDown(int firstRow, int firstColumn, int lastRow, int lastColumn);
    Q_INVOKABLE bool fillRight(int firstRow, int firstColumn, int lastRow, int lastColumn);
    Q_INVOKABLE bool fillRange(int firstRow, int firstColumn, int rowCount, int columnCount,
                               int targetRow, int targetColumn, int targetRowCount, int targetColumnCount);
    // Formula-aware copy: remembers the source range; paste copies its
    // current contents and moves relative references by the paste offset.
    Q_INVOKABLE bool copyRange(int firstRow, int firstColumn, int lastRow, int lastColumn);
    Q_INVOKABLE bool pasteCopied(int row, int column);
    Q_INVOKABLE bool clearRange(int firstRow, int firstColumn, int lastRow, int lastColumn);
    // A matrix of inputs under a paste policy (Workbook.pasteInputs).
    Q_INVOKABLE bool pasteInputs(int row, int column, const QVariantList &rows,
                                 const QString &policy = QStringLiteral("allOrNothing"));
    // Reports the result of a Workbook call made by the view: false copies the
    // Workbook's lastError/lastErrorDetail here, true clears the error.
    Q_INVOKABLE bool reportResult(bool ok) { return report(ok); }
    // Tab/newline separated inputs as a rectangular matrix for pasteMatrix.
    Q_INVOKABLE QVariantList parseTabularText(const QString &text) const;
    // Instrumentation: cells the grid pulled from this adapter (the bounded
    // visible work). Not a property, so reads never trigger bindings.
    Q_INVOKABLE qint64 sourceReadCount() const { return m_sourceReads; }
    Q_INVOKABLE void resetSourceReads() { m_sourceReads = 0; }

signals:
    void workbookChanged();
    void sheetIdChanged();
    void sheetNameChanged();
    void dimensionsChanged();
    void copiedRangeChanged();
    void errorChanged();
    // Inputs or values changed inside this rectangle of the shown sheet.
    void cellsChanged(int firstRow, int firstColumn, int lastRow, int lastColumn);
    // A grid edit of one cell broke a warning/information validation rule;
    // nothing was applied. The view asks and commits with commitInput.
    void validationPrompt(int row, int column, const QString &input, const QVariantMap &check);

private:
    class Source;
    friend class Source;
    struct CopiedRange { QString sheetId; int row = 0, column = 0, rows = 0, columns = 0; };
    QVariant displayForIds(const QString &rowId, const QString &columnId) const;
    // ID -> index through a one-entry memo per axis (views read row-major);
    // the memo is dropped whenever the grid revision moves.
    int rowFor(const QString &id) const;
    int columnFor(const QString &id) const;
    QString validateCandidate(const QString &rowId, const QString &columnId, const QVariant &candidate) const;
    bool applyGridChanges(const QString &label, const QVariantList &changes, QString *error);
    // `fresh`: another sheet is shown (empty and regrow); otherwise the same
    // sheet was reset or reopened and keeps its IDs (so the view keeps its cell).
    void rebuildIds(bool fresh = true);
    void applyProperties();
    void refreshAll();
    bool report(bool ok);
    void setError(const QString &message, const QVariantMap &detail);

    QPointer<Workbook> m_workbook;
    QString m_sheetId;
    EditableGridModel *m_grid = nullptr;
    Source *m_source = nullptr;
    mutable qint64 m_sourceReads = 0;
    mutable QVariantMap m_validationDetail;
    mutable quint64 m_memoStamp = ~quint64(0);
    mutable QString m_memoRowId, m_memoColumnId;
    mutable int m_memoRow = -1, m_memoColumn = -1;
    QSet<QString> m_sizedRows, m_sizedColumns; // grid IDs carrying workbook sizes
    bool m_applyingProperties = false;
    CopiedRange m_copy;
    QString m_lastError;
    QVariantMap m_lastErrorDetail;
    QList<QMetaObject::Connection> m_connections;
};

} // namespace QindaTK
