// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/financial-displays.md): each row is one host-defined
// session slot, never an inferred market calendar interval.
class OhlcModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(int count READ count NOTIFY stateChanged)
  Q_PROPERTY(QString priceUnit READ priceUnit NOTIFY stateChanged)
  Q_PROPERTY(QString intervalMeaning READ intervalMeaning NOTIFY stateChanged)
  Q_PROPERTY(QString timeZoneId READ timeZoneId NOTIFY stateChanged)
  Q_PROPERTY(double minimum READ minimum NOTIFY stateChanged)
  Q_PROPERTY(double maximum READ maximum NOTIFY stateChanged)
  Q_PROPERTY(bool hasReferencePrice READ hasReferencePrice NOTIFY stateChanged)
  Q_PROPERTY(double referencePrice READ referencePrice NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  Q_PROPERTY(quint64 generation READ generation NOTIFY stateChanged)
public:
  enum Role { IdRole = Qt::UserRole + 1, UtcMsRole, MissingRole, OpenRole,
              HighRole, LowRole, CloseRole };
  explicit OhlcModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return m_rows.size(); }
  QString priceUnit() const { return m_priceUnit; }
  QString intervalMeaning() const { return m_intervalMeaning; }
  QString timeZoneId() const { return m_timeZoneId; }
  double minimum() const { return m_minimum; }
  double maximum() const { return m_maximum; }
  bool hasReferencePrice() const { return m_hasReferencePrice; }
  double referencePrice() const { return m_referencePrice; }
  QString error() const { return m_error; }
  quint64 generation() const { return m_generation; }
  Q_INVOKABLE bool replaceCandles(const QVariantList &rows, const QString &priceUnit,
                                   const QString &intervalMeaning,
                                   const QString &timeZoneId,
                                   const QVariant &referencePrice);
  Q_INVOKABLE QVariantMap get(int row) const;
  Q_INVOKABLE QVariantList page(int start, int count) const;
  Q_INVOKABLE int indexOfId(const QString &id) const;
  Q_INVOKABLE QString csv() const;
signals:
  void stateChanged();
private:
  struct Candle {
    QString id, utcMs;
    qint64 time = 0;
    bool missing = false;
    double open = 0, high = 0, low = 0, close = 0;
  };
  QVector<Candle> m_rows;
  QString m_priceUnit, m_intervalMeaning, m_timeZoneId, m_error;
  double m_minimum = 0, m_maximum = 1, m_referencePrice = 0;
  bool m_hasReferencePrice = false;
  quint64 m_generation = 0;
  bool fail(const QString &message);
};

} // namespace QindaTK
