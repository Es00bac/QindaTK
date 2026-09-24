// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A sampled surface z(x, y) on a rectilinear float64 lattice. The list model
// exposes one row per lattice node (the textual/tabular alternative).
// AGENT-CONTRACT: row r is ys[r] (row 0 is the SMALLEST y, unlike
// HeatmapModel whose row 0 is the top); column c is xs[c]. Missing nodes are
// stored as NaN internally because non-finite input is rejected, so NaN is
// unambiguous. Interpolation never invents a value next to a missing node:
// any contributing (nonzero weight) missing corner makes the result missing.
// ScientificPlotRenderer and ScientificScene3D sample through sampleAt().
class ScientificGrid : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString sourceId READ sourceId WRITE setSourceId NOTIFY changed)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY changed)
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY changed)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY changed)
    Q_PROPERTY(QString zUnit READ zUnit WRITE setZUnit NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters WRITE setParameters NOTIFY changed)
    Q_PROPERTY(int rows READ rows NOTIFY changed)
    Q_PROPERTY(int columns READ columns NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(QString shape READ shape NOTIFY changed)
    Q_PROPERTY(QString precision READ precision CONSTANT)
    Q_PROPERTY(bool hasValues READ hasValues NOTIFY changed)
    Q_PROPERTY(double zMinimum READ zMinimum NOTIFY changed)
    Q_PROPERTY(double zMaximum READ zMaximum NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Interpolation { Nearest, Bilinear };
    Q_ENUM(Interpolation)
    static constexpr int AxisLimit = 4096;
    static constexpr int NodeLimit = 1048576;
    explicit ScientificGrid(QObject *parent = nullptr);
    QString sourceId() const { return m_sourceId; }
    void setSourceId(const QString &value);
    QString label() const { return m_label.isEmpty() ? m_sourceId : m_label; }
    void setLabel(const QString &value);
    QString xUnit() const { return m_xUnit; }
    void setXUnit(const QString &value);
    QString yUnit() const { return m_yUnit; }
    void setYUnit(const QString &value);
    QString zUnit() const { return m_zUnit; }
    void setZUnit(const QString &value);
    QVariantMap parameters() const { return m_parameters; }
    void setParameters(const QVariantMap &value);
    int rows() const { return m_ys.size(); }
    int columns() const { return m_xs.size(); }
    int count() const { return m_values.size(); }
    int missingCount() const { return m_missing; }
    QString shape() const { return QStringLiteral("[%1,%2]").arg(rows()).arg(columns()); }
    QString precision() const { return QStringLiteral("float64"); }
    bool hasValues() const { return count()>m_missing; }
    double zMinimum() const { return m_zMin; }
    double zMaximum() const { return m_zMax; }
    QString lastError() const { return m_lastError; }
    const QVector<double> &xs() const { return m_xs; }
    const QVector<double> &ys() const { return m_ys; }
    // NaN when missing.
    double at(int row, int column) const { return m_values[row*columns()+column]; }
    // NaN when outside the lattice or when a contributing node is missing.
    double sampleAt(double x, double y, Interpolation mode) const;
    // xs/ys strictly increasing, 2..4096 each; values row-major (row = y),
    // numbers or null (missing). Atomic: rejects everything on any error.
    Q_INVOKABLE bool replaceGrid(const QVariantList &xs, const QVariantList &ys, const QVariantList &values);
    Q_INVOKABLE QVariantMap node(int row, int column) const;
    Q_INVOKABLE QVariantMap interpolate(double x, double y, Interpolation mode) const;
    // Bounded read of a lattice window: at most maxRows x maxColumns nodes
    // (each <= 512), taken at an integer stride (real nodes, never averages).
    Q_INVOKABLE QVariantMap window(int firstRow, int lastRow, int firstColumn, int lastColumn,
                                   int maxRows, int maxColumns) const;
    Q_INVOKABLE QString csvData() const;
    Q_INVOKABLE QString metadataJson() const;
    QVariantMap metadata() const;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
signals:
    void changed();
    void errorChanged();
private:
    enum Role { IdRole = Qt::UserRole + 1, RowRole, ColumnRole, XRole, YRole, ZRole,
                MissingRole, XUnitRole, YUnitRole, ZUnitRole };
    void setError(const QString &value);
    QString digest() const;
    QString m_sourceId, m_label, m_xUnit, m_yUnit, m_zUnit, m_lastError;
    QVariantMap m_parameters;
    QVector<double> m_xs, m_ys, m_values;
    int m_missing = 0;
    double m_zMin = 0, m_zMax = 0;
};

} // namespace QindaTK
