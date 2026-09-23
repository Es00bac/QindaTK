// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ohlc_model.h"

#include <QSet>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr int MaxCandles = 20000;
constexpr int MaxPage = 512;

bool finiteNumber(const QVariant &input, double *result) {
  bool ok = false;
  const double value = input.toDouble(&ok);
  if (!ok || !std::isfinite(value)) return false;
  *result = value;
  return true;
}

QString csvCell(QString text) {
  text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
  return QStringLiteral("\"") + text + QStringLiteral("\"");
}
} // namespace

OhlcModel::OhlcModel(QObject *parent) : QAbstractListModel(parent) {}

int OhlcModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> OhlcModel::roleNames() const {
  return {{IdRole, "candleId"}, {UtcMsRole, "utcMs"},
          {MissingRole, "missing"}, {OpenRole, "open"},
          {HighRole, "high"}, {LowRole, "low"}, {CloseRole, "close"}};
}

QVariant OhlcModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
    return {};
  const Candle &row = m_rows[index.row()];
  switch (role) {
  case IdRole: return row.id;
  case UtcMsRole: return row.utcMs;
  case MissingRole: return row.missing;
  case OpenRole: return row.missing ? QVariant() : QVariant(row.open);
  case HighRole: return row.missing ? QVariant() : QVariant(row.high);
  case LowRole: return row.missing ? QVariant() : QVariant(row.low);
  case CloseRole: return row.missing ? QVariant() : QVariant(row.close);
  default: return {};
  }
}

QVariantMap OhlcModel::get(int index) const {
  if (index < 0 || index >= m_rows.size()) return {};
  const Candle &row = m_rows[index];
  return {{QStringLiteral("candleId"), row.id},
          {QStringLiteral("utcMs"), row.utcMs},
          {QStringLiteral("missing"), row.missing},
          {QStringLiteral("open"), row.missing ? QVariant() : QVariant(row.open)},
          {QStringLiteral("high"), row.missing ? QVariant() : QVariant(row.high)},
          {QStringLiteral("low"), row.missing ? QVariant() : QVariant(row.low)},
          {QStringLiteral("close"), row.missing ? QVariant() : QVariant(row.close)}};
}

QVariantList OhlcModel::page(int start, int count) const {
  QVariantList result;
  if (start < 0 || count < 0 || count > MaxPage || start > m_rows.size())
    return result;
  const int end = qMin(start + count, static_cast<int>(m_rows.size()));
  result.reserve(end - start);
  for (int index = start; index < end; ++index) result.append(get(index));
  return result;
}

int OhlcModel::indexOfId(const QString &id) const {
  for (int index = 0; index < m_rows.size(); ++index)
    if (m_rows[index].id == id) return index;
  return -1;
}

bool OhlcModel::fail(const QString &message) {
  m_error = message;
  emit stateChanged();
  return false;
}

bool OhlcModel::replaceCandles(const QVariantList &records,
                               const QString &priceUnit,
                               const QString &intervalMeaning,
                               const QString &timeZoneId,
                               const QVariant &referencePrice) {
  if (records.size() > MaxCandles)
    return fail(QStringLiteral("At most 20000 session slots are supported"));
  if (priceUnit.trimmed().isEmpty() || intervalMeaning.trimmed().isEmpty() ||
      timeZoneId.trimmed().isEmpty())
    return fail(QStringLiteral("Price unit, interval meaning and time zone are required"));
  double reference = 0;
  const bool hasReference = referencePrice.isValid() && !referencePrice.isNull();
  if (hasReference && !finiteNumber(referencePrice, &reference))
    return fail(QStringLiteral("Reference price must be finite"));
  QVector<Candle> next;
  next.reserve(records.size());
  QSet<QString> ids;
  double minimum = hasReference ? reference : std::numeric_limits<double>::infinity();
  double maximum = hasReference ? reference : -std::numeric_limits<double>::infinity();
  qint64 previousTime = 0;
  bool first = true;
  for (const QVariant &record : records) {
    if (!record.canConvert<QVariantMap>())
      return fail(QStringLiteral("Session slot must be a map"));
    const QVariantMap source = record.toMap();
    Candle row;
    row.id = source.value(QStringLiteral("id")).toString().trimmed();
    if (row.id.isEmpty() || ids.contains(row.id))
      return fail(QStringLiteral("Session slot IDs must be nonempty and unique"));
    ids.insert(row.id);
    row.utcMs = source.value(QStringLiteral("utcMs")).toString();
    bool timeOk = false;
    row.time = row.utcMs.toLongLong(&timeOk);
    if (!timeOk || QString::number(row.time) != row.utcMs)
      return fail(QStringLiteral("utcMs must be a signed 64-bit decimal string"));
    if (!first && row.time <= previousTime)
      return fail(QStringLiteral("Session times must be strictly increasing"));
    previousTime = row.time;
    first = false;
    row.missing = source.value(QStringLiteral("missing")).toBool();
    const auto hasPrice = [&source](const char *key) {
      return source.contains(QLatin1String(key));
    };
    if (row.missing) {
      if (hasPrice("open") || hasPrice("high") || hasPrice("low") || hasPrice("close"))
        return fail(QStringLiteral("Missing slots must not contain prices"));
    } else {
      if (!finiteNumber(source.value(QStringLiteral("open")), &row.open) ||
          !finiteNumber(source.value(QStringLiteral("high")), &row.high) ||
          !finiteNumber(source.value(QStringLiteral("low")), &row.low) ||
          !finiteNumber(source.value(QStringLiteral("close")), &row.close))
        return fail(QStringLiteral("OHLC prices must be finite and complete"));
      if (row.low > row.open || row.low > row.close || row.high < row.open ||
          row.high < row.close || row.low > row.high)
        return fail(QStringLiteral("OHLC high/low must enclose open and close"));
      minimum = qMin(minimum, row.low);
      maximum = qMax(maximum, row.high);
    }
    next.append(row);
  }
  if (!std::isfinite(minimum)) { minimum = 0; maximum = 1; }
  if (minimum == maximum) {
    const double pad = qMax(qMax(std::abs(minimum), 1.0) * 0.01, 0.01);
    minimum -= pad;
    maximum += pad;
  }
  if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
      !std::isfinite(maximum - minimum) || maximum <= minimum)
    return fail(QStringLiteral("Price domain overflow"));
  beginResetModel();
  m_rows = std::move(next);
  m_priceUnit = priceUnit.trimmed();
  m_intervalMeaning = intervalMeaning.trimmed();
  m_timeZoneId = timeZoneId.trimmed();
  m_minimum = minimum;
  m_maximum = maximum;
  m_hasReferencePrice = hasReference;
  m_referencePrice = reference;
  m_error.clear();
  ++m_generation;
  endResetModel();
  emit stateChanged();
  return true;
}

QString OhlcModel::csv() const {
  QString result = QStringLiteral("id,utc_ms,open,high,low,close,missing,price_unit,interval,time_zone,reference_price\n");
  for (const Candle &row : m_rows) {
    const auto price = [&row](double value) {
      return row.missing ? QString() : QString::number(value, 'g', 17);
    };
    result += csvCell(row.id) + QLatin1Char(',') + csvCell(row.utcMs) +
              QLatin1Char(',') + price(row.open) + QLatin1Char(',') +
              price(row.high) + QLatin1Char(',') + price(row.low) +
              QLatin1Char(',') + price(row.close) + QLatin1Char(',') +
              (row.missing ? QStringLiteral("true") : QStringLiteral("false")) +
              QLatin1Char(',') + csvCell(m_priceUnit) + QLatin1Char(',') +
              csvCell(m_intervalMeaning) + QLatin1Char(',') +
              csvCell(m_timeZoneId) + QLatin1Char(',') +
              (m_hasReferencePrice ? QString::number(m_referencePrice, 'g', 17)
                                   : QString()) + QLatin1Char('\n');
  }
  return result;
}

} // namespace QindaTK
