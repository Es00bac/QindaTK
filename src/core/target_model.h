// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/financial-displays.md): targets and threshold bands
// have host-declared meaning; no performance classification is inferred.
class TargetModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(int count READ count NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  Q_PROPERTY(quint64 generation READ generation NOTIFY stateChanged)
public:
  enum Role { IdRole = Qt::UserRole + 1, LabelRole, UnitRole, MinimumRole,
              MaximumRole, ValueRole, MissingRole, TargetRole, DirectionRole,
              ThresholdMeaningRole, BandsRole, BandsTextRole };
  explicit TargetModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return m_rows.size(); }
  QString error() const { return m_error; }
  quint64 generation() const { return m_generation; }
  Q_INVOKABLE bool replaceTargets(const QVariantList &rows);
  Q_INVOKABLE QVariantMap get(int row) const;
  Q_INVOKABLE QVariantList page(int start, int count) const;
  Q_INVOKABLE int indexOfId(const QString &id) const;
  Q_INVOKABLE QString csv() const;
signals:
  void stateChanged();
private:
  struct Band { double end = 0; QString meaning; };
  struct Target {
    QString id, label, unit, direction, thresholdMeaning;
    double minimum = 0, maximum = 0, value = 0, target = 0;
    bool missing = false;
    QVector<Band> bands;
  };
  QVector<Target> m_rows;
  QString m_error;
  quint64 m_generation = 0;
  bool fail(const QString &message);
  QVariantList bandsFor(const Target &row) const;
  QString bandsTextFor(const Target &row) const;
};

} // namespace QindaTK
