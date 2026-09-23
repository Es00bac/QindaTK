// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QVariantList>
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
    int frozenRows() const { return m_frozenRows; }
    int frozenColumns() const { return m_frozenColumns; }
    qreal defaultRowSize() const { return m_defaultRowSize; }
    qreal defaultColumnSize() const { return m_defaultColumnSize; }
    bool externalUndo() const { return m_externalUndo; }
    bool canUndo() const { return !m_externalUndo && m_historyCursor > 0; }
    bool canRedo() const { return !m_externalUndo && m_historyCursor < m_history.size(); }
    bool editing() const { return m_editing; }
    QString lastError() const { return m_lastError; }
    QVariantList ranges() const;
    GridCellSource *source() const { return m_source; }
    void setSource(GridCellSource *source);
    void setExternalUndo(bool enabled);
    void setValidator(std::function<QString(const QString &, const QString &, const QVariant &)> validator);

    Q_INVOKABLE bool setDimensions(int rows, int columns);
    Q_INVOKABLE bool setRowIds(const QStringList &ids);
    Q_INVOKABLE bool setColumnIds(const QStringList &ids);
    Q_INVOKABLE QString rowIdAt(int index) const;
    Q_INVOKABLE QString columnIdAt(int index) const;
    Q_INVOKABLE int rowIndex(const QString &id) const;
    Q_INVOKABLE int columnIndex(const QString &id) const;
    Q_INVOKABLE QVariant cellValue(const QString &rowId, const QString &columnId) const;
    Q_INVOKABLE QVariantList visibleCells(int firstRow, int rows, int firstColumn, int columns) const;
    Q_INVOKABLE QVariantList visibleRegions(qreal viewportWidth, qreal viewportHeight,
                                            qreal scrollX, qreal scrollY) const;
    Q_INVOKABLE QRectF cellRect(int row, int column, qreal scrollX, qreal scrollY) const;
    Q_INVOKABLE QVariantMap hitTest(qreal x, qreal y, qreal scrollX, qreal scrollY) const;
    Q_INVOKABLE bool requestRowResize(const QString &id, qreal size);
    Q_INVOKABLE bool requestColumnResize(const QString &id, qreal size);
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
    void rowResized(const QString &id, qreal size);
    void columnResized(const QString &id, qreal size);
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
    bool moveId(const QString &id, const QString &beforeId, bool rowAxis);
    bool resizeId(const QString &id, qreal size, bool rowAxis);
    bool setDefaultSize(qreal size, bool rowAxis);
    void rebuildIndexes(bool rowAxis);
    void rebuildPositions(bool rowAxis);
    void reconcile(bool invalidateHistory);
    bool validateCell(const QString &row, const QString &column, const QVariant &value);
    bool applyChanges(const QVector<Change> &changes, bool reverse);
    QVariantList changePayload(const QVector<Change> &changes) const;
    QString key(const QString &row, const QString &column) const;
    int indexAt(const QVector<qreal> &positions, qreal coordinate) const;
    QVariantMap region(int firstRow, int lastRow, int firstColumn, int lastColumn,
                       bool frozenRow, bool frozenColumn) const;

    QStringList m_rows, m_columns;
    QHash<QString, int> m_rowIndexes, m_columnIndexes;
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
    bool m_editing = false, m_externalUndo = false;
    QString m_label, m_lastError;
};

} // namespace QindaTK
