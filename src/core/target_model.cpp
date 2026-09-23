// SPDX-License-Identifier: LGPL-3.0-or-later
#include "target_model.h"

#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int MaxTargets = 256;
constexpr int MaxBands = 8;
constexpr int MaxPage = 16;

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

TargetModel::TargetModel(QObject *parent) : QAbstractListModel(parent) {}

int TargetModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> TargetModel::roleNames() const {
  return {{IdRole, "targetId"}, {LabelRole, "label"}, {UnitRole, "unit"},
          {MinimumRole, "minimum"}, {MaximumRole, "maximum"},
          {ValueRole, "value"}, {MissingRole, "missing"},
          {TargetRole, "target"}, {DirectionRole, "direction"},
          {ThresholdMeaningRole, "thresholdMeaning"},
          {BandsRole, "bands"}, {BandsTextRole, "bandsText"}};
}

QVariantList TargetModel::bandsFor(const Target &row) const {
  QVariantList result;
  result.reserve(row.bands.size());
  double start = row.minimum;
  for (const Band &band : row.bands) {
    result.append(QVariantMap{{QStringLiteral("start"), start},
                              {QStringLiteral("end"), band.end},
                              {QStringLiteral("meaning"), band.meaning}});
    start = band.end;
  }
  return result;
}

QString TargetModel::bandsTextFor(const Target &row) const {
  QStringList parts;
  double start = row.minimum;
  for (const Band &band : row.bands) {
    parts.append(QStringLiteral("%1–%2 %3")
                     .arg(QString::number(start, 'g', 17),
                          QString::number(band.end, 'g', 17), band.meaning));
    start = band.end;
  }
  return parts.join(QStringLiteral("; "));
}

QVariant TargetModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
    return {};
  const Target &row = m_rows[index.row()];
  switch (role) {
  case IdRole: return row.id;
  case LabelRole: return row.label;
  case UnitRole: return row.unit;
  case MinimumRole: return row.minimum;
  case MaximumRole: return row.maximum;
  case ValueRole: return row.missing ? QVariant() : QVariant(row.value);
  case MissingRole: return row.missing;
  case TargetRole: return row.target;
  case DirectionRole: return row.direction;
  case ThresholdMeaningRole: return row.thresholdMeaning;
  case BandsRole: return bandsFor(row);
  case BandsTextRole: return bandsTextFor(row);
  default: return {};
  }
}

QVariantMap TargetModel::get(int index) const {
  if (index < 0 || index >= m_rows.size()) return {};
  const Target &row = m_rows[index];
  return {{QStringLiteral("targetId"), row.id},
          {QStringLiteral("label"), row.label},
          {QStringLiteral("unit"), row.unit},
          {QStringLiteral("minimum"), row.minimum},
          {QStringLiteral("maximum"), row.maximum},
          {QStringLiteral("value"), row.missing ? QVariant() : QVariant(row.value)},
          {QStringLiteral("missing"), row.missing},
          {QStringLiteral("target"), row.target},
          {QStringLiteral("direction"), row.direction},
          {QStringLiteral("thresholdMeaning"), row.thresholdMeaning},
          {QStringLiteral("bands"), bandsFor(row)},
          {QStringLiteral("bandsText"), bandsTextFor(row)}};
}

QVariantList TargetModel::page(int start, int count) const {
  QVariantList result;
  if (start < 0 || count < 0 || count > MaxPage || start > m_rows.size())
    return result;
  const int end = qMin(start + count, static_cast<int>(m_rows.size()));
  result.reserve(end - start);
  for (int index = start; index < end; ++index) result.append(get(index));
  return result;
}

int TargetModel::indexOfId(const QString &id) const {
  for (int index = 0; index < m_rows.size(); ++index)
    if (m_rows[index].id == id) return index;
  return -1;
}

bool TargetModel::fail(const QString &message) {
  m_error = message;
  emit stateChanged();
  return false;
}

bool TargetModel::replaceTargets(const QVariantList &records) {
  if (records.size() > MaxTargets)
    return fail(QStringLiteral("At most 256 targets are supported"));
  QVector<Target> next;
  next.reserve(records.size());
  QSet<QString> ids;
  for (const QVariant &record : records) {
    if (!record.canConvert<QVariantMap>())
      return fail(QStringLiteral("Target must be a map"));
    const QVariantMap source = record.toMap();
    Target row;
    row.id = source.value(QStringLiteral("id")).toString().trimmed();
    row.label = source.value(QStringLiteral("label")).toString().trimmed();
    row.unit = source.value(QStringLiteral("unit")).toString().trimmed();
    row.direction = source.value(QStringLiteral("direction")).toString().trimmed();
    row.thresholdMeaning = source.value(QStringLiteral("thresholdMeaning"))
                               .toString().trimmed();
    if (row.id.isEmpty() || ids.contains(row.id))
      return fail(QStringLiteral("Target IDs must be nonempty and unique"));
    ids.insert(row.id);
    if (row.label.isEmpty() || row.unit.isEmpty() || row.thresholdMeaning.isEmpty())
      return fail(QStringLiteral("Label, unit and threshold meaning are required"));
    if (row.direction != QStringLiteral("higher") &&
        row.direction != QStringLiteral("lower"))
      return fail(QStringLiteral("Direction must be higher or lower"));
    if (!finiteNumber(source.value(QStringLiteral("minimum")), &row.minimum) ||
        !finiteNumber(source.value(QStringLiteral("maximum")), &row.maximum) ||
        !finiteNumber(source.value(QStringLiteral("target")), &row.target) ||
        !(row.maximum > row.minimum) ||
        !std::isfinite(row.maximum - row.minimum) ||
        row.target < row.minimum || row.target > row.maximum)
      return fail(QStringLiteral("Range and target must be finite and ordered"));
    row.missing = source.value(QStringLiteral("missing")).toBool();
    if (row.missing) {
      if (source.contains(QStringLiteral("value")))
        return fail(QStringLiteral("Missing targets must not contain a value"));
    } else if (!finiteNumber(source.value(QStringLiteral("value")), &row.value) ||
               row.value < row.minimum || row.value > row.maximum) {
      return fail(QStringLiteral("Current value must be finite and inside range"));
    }
    const QVariantList bands = source.value(QStringLiteral("bands")).toList();
    if (bands.isEmpty() || bands.size() > MaxBands)
      return fail(QStringLiteral("Each target requires 1–8 threshold bands"));
    double start = row.minimum;
    for (const QVariant &recordBand : bands) {
      if (!recordBand.canConvert<QVariantMap>())
        return fail(QStringLiteral("Threshold band must be a map"));
      const QVariantMap bandSource = recordBand.toMap();
      Band band;
      band.meaning = bandSource.value(QStringLiteral("meaning")).toString().trimmed();
      if (!finiteNumber(bandSource.value(QStringLiteral("end")), &band.end) ||
          band.meaning.isEmpty() || band.end <= start || band.end > row.maximum)
        return fail(QStringLiteral("Threshold bands must be ordered and named"));
      row.bands.append(band);
      start = band.end;
    }
    if (start != row.maximum)
      return fail(QStringLiteral("Threshold bands must cover the full range"));
    next.append(std::move(row));
  }
  beginResetModel();
  m_rows = std::move(next);
  m_error.clear();
  ++m_generation;
  endResetModel();
  emit stateChanged();
  return true;
}

QString TargetModel::csv() const {
  QString result = QStringLiteral("id,label,unit,minimum,maximum,value,missing,target,direction,threshold_meaning,bands\n");
  for (const Target &row : m_rows) {
    const auto number = [](double value) { return QString::number(value, 'g', 17); };
    result += csvCell(row.id) + QLatin1Char(',') + csvCell(row.label) +
              QLatin1Char(',') + csvCell(row.unit) + QLatin1Char(',') +
              number(row.minimum) + QLatin1Char(',') + number(row.maximum) +
              QLatin1Char(',') + (row.missing ? QString() : number(row.value)) +
              QLatin1Char(',') + (row.missing ? QStringLiteral("true") : QStringLiteral("false")) +
              QLatin1Char(',') + number(row.target) + QLatin1Char(',') +
              csvCell(row.direction) + QLatin1Char(',') +
              csvCell(row.thresholdMeaning) + QLatin1Char(',') +
              csvCell(bandsTextFor(row)) + QLatin1Char('\n');
  }
  return result;
}

} // namespace QindaTK
