// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QPair>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/distribution-plot.md): records own finite source
// values, a declared common unit, and optional explicitly named intervals.
// The model computes statistics; the QML view only projects them.
class DistributionModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(int count READ count NOTIFY stateChanged)
  Q_PROPERTY(QString unit READ unit NOTIFY stateChanged)
  Q_PROPERTY(QString intervalMeaning READ intervalMeaning NOTIFY stateChanged)
  Q_PROPERTY(double minimum READ minimum NOTIFY stateChanged)
  Q_PROPERTY(double maximum READ maximum NOTIFY stateChanged)
  Q_PROPERTY(QString estimator READ estimator CONSTANT)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  Q_PROPERTY(quint64 generation READ generation NOTIFY stateChanged)
public:
  enum Role {
    IdRole = Qt::UserRole + 1,
    LabelRole,
    UnitRole,
    CountRole,
    MinimumRole,
    Q1Role,
    MedianRole,
    Q3Role,
    MaximumRole,
    MeanRole,
    BandwidthRole,
    DensityRole,
    HasIntervalRole,
    IntervalLowRole,
    IntervalHighRole,
    IntervalMeaningRole,
    EstimateRole
  };
  explicit DistributionModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return m_groups.size(); }
  QString unit() const { return m_unit; }
  QString intervalMeaning() const { return m_intervalMeaning; }
  double minimum() const { return m_minimum; }
  double maximum() const { return m_maximum; }
  QString estimator() const {
    return QStringLiteral("Gaussian KDE; Silverman bandwidth; 64 samples");
  }
  QString error() const { return m_error; }
  quint64 generation() const { return m_generation; }
  Q_INVOKABLE bool replaceGroups(const QVariantList &groups);
  Q_INVOKABLE QVariantMap get(int row) const;
  Q_INVOKABLE int indexOfId(const QString &id) const;
  Q_INVOKABLE QVariantList sourceValues(const QString &id) const;
  Q_INVOKABLE QString summaryCsv() const;
signals:
  void stateChanged();

private:
  struct Group {
    QString id, label;
    QVector<double> values;
    QVector<QPair<double, double>> density;
    double minimum = 0, q1 = 0, median = 0, q3 = 0, maximum = 0, mean = 0;
    double bandwidth = 0;
    double intervalLow = 0, intervalHigh = 0, estimate = 0;
    bool hasInterval = false;
  };
  QVector<Group> m_groups;
  QString m_unit, m_intervalMeaning, m_error;
  double m_minimum = 0, m_maximum = 1;
  quint64 m_generation = 0;
  bool fail(const QString &error);
};

} // namespace QindaTK
