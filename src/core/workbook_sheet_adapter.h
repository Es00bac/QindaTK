// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "editable_grid_model.h"
#include "workbook.h"
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: presents one sheet of a Workbook through the existing
// EditableGridModel contract (docs/editable-grid.md) without a second store.
// The grid's GridCellSource returns display text for requested cells only;
// its validator parses candidate inputs; externalUndo transfers history to
// the Workbook. A committed grid edit or paste is applied to the Workbook as
// one transaction, then the grid's override layer is reverted through its
// applyHistoryChanges bridge, so the grid never holds cell values.
// Structural workbook changes move the grid's stable row/column IDs.
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
    int rowCount() const { return m_rowIds.size(); }
    int columnCount() const { return m_columnIds.size(); }
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

private:
    class Source;
    friend class Source;
    struct CopiedRange { QString sheetId; int row = 0, column = 0, rows = 0, columns = 0; };
    QVariant displayForIds(const QString &rowId, const QString &columnId) const;
    QString validateCandidate(const QString &rowId, const QString &columnId, const QVariant &candidate) const;
    void absorb(const QString &transactionId, const QString &label, const QVariantList &changes);
    void rebuildIds();
    void applyIds();
    void moveIds(QStringList *ids, bool insert, int at, int count, bool rows);
    void refreshAll();
    bool report(bool ok);
    void setError(const QString &message, const QVariantMap &detail);

    QPointer<Workbook> m_workbook;
    QString m_sheetId;
    EditableGridModel *m_grid = nullptr;
    Source *m_source = nullptr;
    QStringList m_rowIds, m_columnIds;
    quint64 m_nextRowId = 1, m_nextColumnId = 1;
    mutable qint64 m_sourceReads = 0;
    mutable QVariantMap m_validationDetail;
    bool m_absorbing = false;
    CopiedRange m_copy;
    QString m_lastError;
    QVariantMap m_lastErrorDetail;
    QList<QMetaObject::Connection> m_connections;
};

} // namespace QindaTK
