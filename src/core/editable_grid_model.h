// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "grid_axis_ids.h"
#include <QAbstractTableModel>
#include <QHash>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <functional>

namespace QindaTK {

// Lazy, read-only baseline. Its value() must be cheap for a visible cell and
// validation must be side-effect free. The grid owns committed sparse edits.
class GridCellSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~GridCellSource() override = default;
    virtual QVariant value(const QString &rowId, const QString &columnId) const = 0;
    virtual QString validate(const QString &rowId, const QString &columnId,
                             const QVariant &candidate) const
    { Q_UNUSED(rowId); Q_UNUSED(columnId); Q_UNUSED(candidate); return {}; }
};

// AGENT-CONTRACT (docs/editable-grid.md#domain-sources): optional capabilities
// a GridCellSource adds by also deriving from this interface. The model finds
// it with dynamic_cast, so existing sources keep their behavior and ABI. A
// domain source that owns its data (a spreadsheet) supplies the editor text
// separately from the display value, cell presentation, merged areas, and —
// with EditableGridModel::sourceCommits — applies validated change sets and
// fills itself instead of the grid's override layer.
class GridCellSourceExtension {
public:
    virtual ~GridCellSourceExtension() = default;
    // Text an editor starts from; the displayed value() may be computed.
    virtual QVariant editValue(const QString &rowId, const QString &columnId) const = 0;
    // Optional presentation: horizontalAlignment ("left"|"center"|"right"),
    // bold/italic/underline/strikeout booleans, textColor/fillColor document
    // colors ("#rrggbb"). Empty means the view's theme defaults.
    virtual QVariantMap cellStyle(const QString &rowId, const QString &columnId) const
    { Q_UNUSED(rowId); Q_UNUSED(columnId); return {}; }
    // Merged areas intersecting an index window (x/left = first column,
    // y/top = first row). Areas never overlap; the top-left cell is the anchor.
    virtual QVector<QRect> mergedRanges(const QRect &window) const
    { Q_UNUSED(window); return {}; }
    // Applies one validated change set (the `committed` payload) atomically
    // when sourceCommits is true; false with an error leaves the edit open.
    virtual bool commitChanges(const QString &label, const QVariantList &changes, QString *error) = 0;
    // Domain fill of `target` from the repeating `source` block. Return false
    // with an empty error when unsupported; the grid then copies values.
    virtual bool fillCells(const QRect &source, const QRect &target, QString *error)
    { Q_UNUSED(source); Q_UNUSED(target); Q_UNUSED(error); return false; }
};

class EditableGridModel : public QAbstractTableModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int rowCount READ rows NOTIFY structureChanged)
    Q_PROPERTY(int columnCount READ columns NOTIFY structureChanged)
    Q_PROPERTY(QString revision READ revision NOTIFY cellsChanged)
    Q_PROPERTY(int frozenRows READ frozenRows WRITE setFrozenRows NOTIFY geometryChanged)
    Q_PROPERTY(int frozenColumns READ frozenColumns WRITE setFrozenColumns NOTIFY geometryChanged)
    Q_PROPERTY(qreal defaultRowSize READ defaultRowSize WRITE setDefaultRowSize NOTIFY geometryChanged)
    Q_PROPERTY(qreal defaultColumnSize READ defaultColumnSize WRITE setDefaultColumnSize NOTIFY geometryChanged)
    Q_PROPERTY(bool externalUndo READ externalUndo WRITE setExternalUndo NOTIFY historyChanged)
    Q_PROPERTY(bool sourceCommits READ sourceCommits WRITE setSourceCommits NOTIFY historyChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY rejected)
    Q_PROPERTY(QVariantList ranges READ ranges NOTIFY selectionChanged)
public:
    explicit EditableGridModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    int rows() const { return m_rows.size(); }
    int columns() const { return m_columns.size(); }
    QString revision() const { return QString::number(m_revision); }
    // Changes whenever values or section identity/order change; cheap to compare.
    quint64 revisionValue() const { return m_revision; }
    int frozenRows() const { return m_frozenRows; }
    int frozenColumns() const { return m_frozenColumns; }
    qreal defaultRowSize() const { return m_defaultRowSize; }
    qreal defaultColumnSize() const { return m_defaultColumnSize; }
    bool externalUndo() const { return m_externalUndo; }
    bool sourceCommits() const { return m_sourceCommits; }
    bool canUndo() const { return !m_readOnly && !m_externalUndo && !m_sourceCommits && m_historyCursor > 0; }
    bool canRedo() const
    { return !m_readOnly && !m_externalUndo && !m_sourceCommits && m_historyCursor < m_history.size(); }
    bool readOnly() const { return m_readOnly; }
    bool editing() const { return m_editing; }
    QString lastError() const { return m_lastError; }
    QVariantList ranges() const;
    GridCellSource *source() const { return m_source; }
    void setSource(GridCellSource *source);
    void setExternalUndo(bool enabled);
    // Source-owned edits: commits, pastes and fills go to the source's
    // GridCellSourceExtension; the grid keeps no overrides or history.
    void setSourceCommits(bool enabled);
    void setReadOnly(bool enabled);
    void setValidator(std::function<QString(const QString &, const QString &, const QVariant &)> validator);

    Q_INVOKABLE bool setDimensions(int rows, int columns);
    Q_INVOKABLE bool setRowIds(const QStringList &ids);
    Q_INVOKABLE bool setColumnIds(const QStringList &ids);
    Q_INVOKABLE bool insertRowsAt(int index, const QStringList &ids);
    Q_INVOKABLE bool insertColumnsAt(int index, const QStringList &ids);
    Q_INVOKABLE bool removeRowsAt(int index, int count);
    Q_INVOKABLE bool removeColumnsAt(int index, int count);
    // Inserts `count` fresh generated IDs ("r:N"/"c:N") as one compact run.
    Q_INVOKABLE bool insertGeneratedRowsAt(int index, int count);
    Q_INVOKABLE bool insertGeneratedColumnsAt(int index, int count);
    Q_INVOKABLE QString rowIdAt(int index) const;
    Q_INVOKABLE QString columnIdAt(int index) const;
    Q_INVOKABLE int rowIndex(const QString &id) const;
    Q_INVOKABLE int columnIndex(const QString &id) const;
    Q_INVOKABLE QVariant cellValue(const QString &rowId, const QString &columnId) const;
    // Editor text: an override, else the source extension's editValue, else cellValue.
    Q_INVOKABLE QVariant editValue(const QString &rowId, const QString &columnId) const;
    // The source's values changed in this index window: bumps the revision
    // and notifies views, which re-read only the cells they show.
    Q_INVOKABLE void notifySourceChanged(int firstRow, int rowCount, int firstColumn, int columnCount);
    Q_INVOKABLE QVariantList mergedRegions(int firstRow, int rowCount, int firstColumn, int columnCount) const;
    Q_INVOKABLE QVariantMap mergeAt(int row, int column) const;
    // Smallest index rectangle holding the given one and every merged area it
    // touches: {firstRow, firstColumn, lastRow, lastColumn}; {} for an empty grid.
    Q_INVOKABLE QVariantMap mergeBounds(int firstRow, int firstColumn, int lastRow, int lastColumn) const;
    // The cells a viewport shows: visibleCells records plus x/y/width/height
    // and frozen, one record per merged area (rowSpan/columnSpan; covered
    // cells omitted). At most 4096 records.
    Q_INVOKABLE QVariantList layoutCells(qreal viewportWidth, qreal viewportHeight,
                                         qreal scrollX, qreal scrollY) const;
    Q_INVOKABLE QVariantList visibleCells(int firstRow, int rows, int firstColumn, int columns) const;
    Q_INVOKABLE QVariantList visibleRegions(qreal viewportWidth, qreal viewportHeight,
                                            qreal scrollX, qreal scrollY) const;
    Q_INVOKABLE QRectF cellRect(int row, int column, qreal scrollX, qreal scrollY) const;
    Q_INVOKABLE QVariantMap hitTest(qreal x, qreal y, qreal scrollX, qreal scrollY) const;
    Q_INVOKABLE bool requestRowResize(const QString &id, qreal size);
    Q_INVOKABLE bool requestColumnResize(const QString &id, qreal size);
    // Ends an interactive resize (a header drag); hosts that persist sizes
    // listen to resizeFinished instead of every intermediate rowResized.
    Q_INVOKABLE void finishResize(const QString &id, bool rowAxis);
    // Returns one section to the default size (it then follows default changes).
    Q_INVOKABLE bool resetRowSize(const QString &id);
    Q_INVOKABLE bool resetColumnSize(const QString &id);
    Q_INVOKABLE bool requestRowMove(const QString &id, const QString &beforeId);
    Q_INVOKABLE bool requestColumnMove(const QString &id, const QString &beforeId);
    void setFrozenRows(int count);
    void setFrozenColumns(int count);
    void setDefaultRowSize(qreal size);
    void setDefaultColumnSize(qreal size);
    Q_INVOKABLE bool setRange(const QString &anchorRowId, const QString &anchorColumnId,
                              const QString &activeRowId, const QString &activeColumnId,
                              bool append = false);
    Q_INVOKABLE void clearRanges();

    Q_INVOKABLE bool beginEdit(const QString &label = QStringLiteral("Edit cells"));
    Q_INVOKABLE bool stageCell(const QString &rowId, const QString &columnId,
                               const QVariant &candidate);
    Q_INVOKABLE bool paste(int firstRow, int firstColumn, const QVariantList &rows);
    Q_INVOKABLE bool fill(int firstRow, int firstColumn, int rowCount, int columnCount,
                          int targetRow, int targetColumn);
    // Repeats the source block over the target rectangle (fill handle). With
    // sourceCommits the source performs a domain fill; otherwise values copy.
    Q_INVOKABLE bool fillRange(int firstRow, int firstColumn, int rowCount, int columnCount,
                               int targetRow, int targetColumn, int targetRowCount,
                               int targetColumnCount);
    Q_INVOKABLE bool commitEdit();
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // AGENT-CONTRACT: Host history owns command routing when externalUndo is
    // true; it passes the exact committed payload and current revision here.
    // The grid checks provenance and cell preconditions before any mutation.
    Q_INVOKABLE bool applyHistoryChanges(const QString &transactionId,
                                         const QVariantList &changes, bool reverse,
                                         const QString &expectedRevision);
    Q_INVOKABLE bool releaseHistoryTransaction(const QString &transactionId);
    int sparseCellCount() const { return m_sparseCount; }

signals:
    void structureChanged();
    void geometryChanged();
    void cellsChanged();
    void selectionChanged();
    void editingChanged();
    void historyChanged();
    void readOnlyChanged();
    void rowResized(const QString &id, qreal size);
    void columnResized(const QString &id, qreal size);
    void resizeFinished(const QString &id, bool rowAxis, qreal size);
    void rowMoved(const QString &id, int newIndex);
    void columnMoved(const QString &id, int newIndex);
    void committed(const QString &transactionId, const QString &label,
                   const QVariantList &changes);
    void rejected(const QString &reason);

private:
    struct Change {
        QString row;
        QString column;
        QVariant before;
        QVariant after;
        bool hadOverride = false;
    };
    struct Step {
        QString id;
        QString label;
        QVector<Change> changes;
    };
    struct Range { QString ar, ac, br, bc; };
    bool fail(const QString &reason);
    bool setIds(QStringList ids, bool rowAxis);
    bool resizeAxis(int count, bool rowAxis);
    bool insertGenerated(int index, int count, bool rowAxis);
    bool resetSize(const QString &id, bool rowAxis);
    bool commitToSource(const QVector<Change> &changes, const QString &label);
    GridCellSourceExtension *extension() const;
    QVariantMap cellRecord(int row, int column, const GridCellSourceExtension *hooks) const;
    // cellValue for IDs known to exist (no membership checks).
    QVariant storedValue(const QString &rowId, const QString &columnId) const;
    bool mergeInside(const QRect &area) const;
    bool insertIds(int index, const QStringList &ids, bool rowAxis);
    bool removeIds(int index, int count, bool rowAxis);
    bool moveId(const QString &id, const QString &beforeId, bool rowAxis);
    bool resizeId(const QString &id, qreal size, bool rowAxis);
    bool setDefaultSize(qreal size, bool rowAxis);
    void rebuildPositions(bool rowAxis);
    void reconcile(bool invalidateHistory);
    bool validateCell(const QString &row, const QString &column, const QVariant &value);
    bool applyChanges(const QVector<Change> &changes, bool reverse);
    QVariantList changePayload(const QVector<Change> &changes) const;
    QString key(const QString &row, const QString &column) const;
    int indexAt(const QVector<qreal> &positions, qreal coordinate) const;
    QVariantMap region(int firstRow, int lastRow, int firstColumn, int lastColumn,
                       bool frozenRow, bool frozenColumn) const;

    GridAxisIds m_rows, m_columns;
    QVector<qreal> m_rowSizes, m_columnSizes, m_rowPositions, m_columnPositions;
    QSet<QString> m_explicitRowSizes, m_explicitColumnSizes;
    QHash<QString, QVariant> m_overrides;
    QPointer<GridCellSource> m_source;
    std::function<QString(const QString &, const QString &, const QVariant &)> m_validator;
    QVector<Range> m_ranges;
    QVector<Change> m_pending;
    QVector<Step> m_history;
    QHash<QString, Step> m_externalSteps;
    qsizetype m_historyCursor = 0;
    quint64 m_revision = 0, m_structureGeneration = 0;
    quint64 m_nextRow = 1, m_nextColumn = 1;
    int m_frozenRows = 0, m_frozenColumns = 0, m_sparseCount = 0;
    qreal m_defaultRowSize = 24, m_defaultColumnSize = 100;
    bool m_editing = false, m_externalUndo = false, m_readOnly = false, m_sourceCommits = false;
    QString m_label, m_lastError;
};

} // namespace QindaTK
