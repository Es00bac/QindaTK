// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QMap>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: row zero is the top of the matrix. Numeric cell coordinates
// are band centers in the declared [minimum, maximum] domains; categorical
// coordinates use ordered labels. Missing cells are never numeric zero.
class HeatmapModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int rows READ rows NOTIFY changed)
    Q_PROPERTY(int columns READ columns NOTIFY changed)
    Q_PROPERTY(double xMinimum READ xMinimum WRITE setXMinimum NOTIFY changed)
    Q_PROPERTY(double xMaximum READ xMaximum WRITE setXMaximum NOTIFY changed)
    Q_PROPERTY(double yMinimum READ yMinimum WRITE setYMinimum NOTIFY changed)
    Q_PROPERTY(double yMaximum READ yMaximum WRITE setYMaximum NOTIFY changed)
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY changed)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY changed)
    Q_PROPERTY(QString valueUnit READ valueUnit WRITE setValueUnit NOTIFY changed)
    Q_PROPERTY(QStringList xCategories READ xCategories WRITE setXCategories NOTIFY changed)
    Q_PROPERTY(QStringList yCategories READ yCategories WRITE setYCategories NOTIFY changed)
    Q_PROPERTY(QStringList valueCategories READ valueCategories WRITE setValueCategories NOTIFY changed)
    Q_PROPERTY(double colorMinimum READ colorMinimum WRITE setColorMinimum NOTIFY changed)
    Q_PROPERTY(double colorMaximum READ colorMaximum WRITE setColorMaximum NOTIFY changed)
    Q_PROPERTY(int storedCellCount READ storedCellCount NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(bool valid READ isValid NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit HeatmapModel(QObject *parent = nullptr);
    int rows() const { return m_rows; }
    int columns() const { return m_columns; }
    double xMinimum() const { return m_xMin; }
    double xMaximum() const { return m_xMax; }
    double yMinimum() const { return m_yMin; }
    double yMaximum() const { return m_yMax; }
    QString xUnit() const { return m_xUnit; }
    QString yUnit() const { return m_yUnit; }
    QString valueUnit() const { return m_valueUnit; }
    QStringList xCategories() const { return m_xCategories; }
    QStringList yCategories() const { return m_yCategories; }
    QStringList valueCategories() const { return m_valueCategories; }
    double colorMinimum() const { return m_colorMin; }
    double colorMaximum() const { return m_colorMax; }
    int storedCellCount() const { return m_cells.size(); }
    quint64 revision() const { return m_revision; }
    bool isValid() const { return error().isEmpty(); }
    QString error() const;
    void setXMinimum(double v);
    void setXMaximum(double v);
    void setYMinimum(double v);
    void setYMaximum(double v);
    void setXUnit(const QString &v);
    void setYUnit(const QString &v);
    void setValueUnit(const QString &v);
    void setXCategories(const QStringList &v);
    void setYCategories(const QStringList &v);
    void setValueCategories(const QStringList &v);
    void setColorMinimum(double v);
    void setColorMaximum(double v);
    Q_INVOKABLE bool setDimensions(int rows, int columns);
    Q_INVOKABLE bool setCell(int row, int column, const QString &id, const QVariant &value);
    Q_INVOKABLE QVariantMap cellAt(int row, int column) const;
    Q_INVOKABLE QVariantList tile(int firstRow, int lastRow, int firstColumn,
                                  int lastColumn, int binsY, int binsX) const;
    Q_INVOKABLE quint64 requestTile(int firstRow, int lastRow, int firstColumn,
                                    int lastColumn, int binsY, int binsX);
    Q_INVOKABLE void cancelTile();
signals:
    void changed();
    void tileReady(quint64 requestId, quint64 revision, const QVariantList &bins);
private:
    struct Cell { QString id; QVariant value; };
    static quint64 key(int row, int column) { return (quint64(quint32(row)) << 32) | quint32(column); }
    QVariantMap record(int row, int column, const Cell *cell) const;
    void touch();
    int m_rows = 0;
    int m_columns = 0;
    double m_xMin = 0, m_xMax = 1, m_yMin = 0, m_yMax = 1;
    double m_colorMin = 0, m_colorMax = 1;
    QString m_xUnit, m_yUnit, m_valueUnit;
    QStringList m_xCategories, m_yCategories, m_valueCategories;
    QMap<quint64, Cell> m_cells;
    QMap<QString, quint64> m_ids;
    quint64 m_revision = 0, m_request = 0;
};

} // namespace QindaTK
