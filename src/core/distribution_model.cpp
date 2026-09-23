// SPDX-License-Identifier: LGPL-3.0-or-later
#include "distribution_model.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr int MaxGroups = 64;
constexpr int MaxValuesPerGroup = 10000;
constexpr int MaxTotalValues = 100000;
constexpr int DensitySamples = 64;
constexpr double Pi = 3.14159265358979323846;

double quantile(const QVector<double> &sorted, double probability) {
  const double position = (sorted.size() - 1) * probability;
  const int first = static_cast<int>(position);
  const int second = qMin(first + 1, static_cast<int>(sorted.size()) - 1);
  const long double fraction = position - first;
  return static_cast<double>((1 - fraction) * sorted[first] +
                             fraction * sorted[second]);
}

bool number(const QVariant &variant, double *result) {
  bool ok = false;
  const double value = variant.toDouble(&ok);
  if (!ok || !std::isfinite(value))
    return false;
  *result = value;
  return true;
}

QString csvCell(const QString &value) {
  QString escaped = value;
  escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
  return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}
} // namespace

DistributionModel::DistributionModel(QObject *parent)
    : QAbstractListModel(parent) {}

int DistributionModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_groups.size();
}

QHash<int, QByteArray> DistributionModel::roleNames() const {
  return {{IdRole, "groupId"},
          {LabelRole, "label"},
          {UnitRole, "unit"},
          {CountRole, "sampleCount"},
          {MinimumRole, "minimum"},
          {Q1Role, "q1"},
          {MedianRole, "median"},
          {Q3Role, "q3"},
          {MaximumRole, "maximum"},
          {MeanRole, "mean"},
          {BandwidthRole, "bandwidth"},
          {DensityRole, "density"},
          {HasIntervalRole, "hasInterval"},
          {IntervalLowRole, "intervalLow"},
          {IntervalHighRole, "intervalHigh"},
          {IntervalMeaningRole, "intervalMeaning"},
          {EstimateRole, "estimate"}};
}

QVariantMap DistributionModel::get(int row) const {
  if (row < 0 || row >= m_groups.size())
    return {};
  const Group &group = m_groups[row];
  QVariantList density;
  density.reserve(group.density.size());
  for (const auto &[value, amount] : group.density)
    density.append(QVariantMap{{QStringLiteral("value"), value},
                               {QStringLiteral("density"), amount}});
  return {{QStringLiteral("groupId"), group.id},
          {QStringLiteral("label"), group.label},
          {QStringLiteral("unit"), m_unit},
          {QStringLiteral("sampleCount"), group.values.size()},
          {QStringLiteral("minimum"), group.minimum},
          {QStringLiteral("q1"), group.q1},
          {QStringLiteral("median"), group.median},
          {QStringLiteral("q3"), group.q3},
          {QStringLiteral("maximum"), group.maximum},
          {QStringLiteral("mean"), group.mean},
          {QStringLiteral("bandwidth"), group.bandwidth},
          {QStringLiteral("density"), density},
          {QStringLiteral("hasInterval"), group.hasInterval},
          {QStringLiteral("intervalLow"),
           group.hasInterval ? QVariant(group.intervalLow) : QVariant()},
          {QStringLiteral("intervalHigh"),
           group.hasInterval ? QVariant(group.intervalHigh) : QVariant()},
          {QStringLiteral("intervalMeaning"),
           group.hasInterval ? m_intervalMeaning : QString()},
          {QStringLiteral("estimate"), group.estimate}};
}

QVariant DistributionModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_groups.size())
    return {};
  const Group &group = m_groups[index.row()];
  switch (role) {
  case IdRole:
    return group.id;
  case LabelRole:
    return group.label;
  case UnitRole:
    return m_unit;
  case CountRole:
    return group.values.size();
  case MinimumRole:
    return group.minimum;
  case Q1Role:
    return group.q1;
  case MedianRole:
    return group.median;
  case Q3Role:
    return group.q3;
  case MaximumRole:
    return group.maximum;
  case MeanRole:
    return group.mean;
  case BandwidthRole:
    return group.bandwidth;
  case DensityRole:
    return get(index.row()).value(QStringLiteral("density"));
  case HasIntervalRole:
    return group.hasInterval;
  case IntervalLowRole:
    return group.hasInterval ? QVariant(group.intervalLow) : QVariant();
  case IntervalHighRole:
    return group.hasInterval ? QVariant(group.intervalHigh) : QVariant();
  case IntervalMeaningRole:
    return group.hasInterval ? m_intervalMeaning : QString();
  case EstimateRole:
    return group.estimate;
  default:
    return {};
  }
}

bool DistributionModel::fail(const QString &error) {
  m_error = error;
  emit stateChanged();
  return false;
}

bool DistributionModel::replaceGroups(const QVariantList &groups) {
  if (groups.size() > MaxGroups)
    return fail(QStringLiteral("At most 64 groups are supported"));
  QVector<Group> next;
  next.reserve(groups.size());
  QSet<QString> ids;
  QString unit, intervalMeaning;
  int totalValues = 0;
  double lower = std::numeric_limits<double>::infinity();
  double upper = -std::numeric_limits<double>::infinity();
  for (const QVariant &record : groups) {
    if (!record.canConvert<QVariantMap>())
      return fail(QStringLiteral("Group must be a map"));
    const QVariantMap source = record.toMap();
    Group group;
    group.id = source.value(QStringLiteral("id")).toString().trimmed();
    group.label = source.value(QStringLiteral("label")).toString();
    const QString groupUnit =
        source.value(QStringLiteral("unit")).toString().trimmed();
    if (group.id.isEmpty() || ids.contains(group.id))
      return fail(QStringLiteral("Group IDs must be nonempty and unique"));
    if (group.label.isEmpty())
      return fail(QStringLiteral("Group label is required"));
    if (groupUnit.isEmpty())
      return fail(QStringLiteral("Group unit is required"));
    if (unit.isEmpty())
      unit = groupUnit;
    if (unit != groupUnit)
      return fail(QStringLiteral("Group units must match"));
    ids.insert(group.id);
    const QVariantList values = source.value(QStringLiteral("values")).toList();
    if (values.isEmpty() || values.size() > MaxValuesPerGroup)
      return fail(QStringLiteral("Each group requires 1–10000 source values"));
    totalValues += values.size();
    if (totalValues > MaxTotalValues)
      return fail(QStringLiteral("At most 100000 source values are supported"));
    long double sum = 0;
    for (const QVariant &sourceValue : values) {
      double value = 0;
      if (!number(sourceValue, &value))
        return fail(QStringLiteral("Source values must be finite numbers"));
      group.values.append(value);
      sum += value;
    }
    const QVector<double> original = group.values;
    QVector<double> sorted = original;
    std::sort(sorted.begin(), sorted.end());
    group.minimum = sorted.first();
    group.maximum = sorted.last();
    group.q1 = quantile(sorted, 0.25);
    group.median = quantile(sorted, 0.5);
    group.q3 = quantile(sorted, 0.75);
    group.mean = static_cast<double>(sum / sorted.size());
    if (!std::isfinite(group.mean))
      return fail(QStringLiteral("Mean overflow"));
    group.estimate = group.mean;
    long double squared = 0;
    for (double value : sorted) {
      const long double delta = static_cast<long double>(value) - group.mean;
      squared += delta * delta;
    }
    const double deviation =
        sorted.size() > 1
            ? static_cast<double>(std::sqrt(squared / (sorted.size() - 1)))
            : 0;
    const double iqr = group.q3 - group.q1;
    double bandwidth = 0.9 * std::min(deviation, iqr / 1.34) *
                       std::pow(static_cast<double>(sorted.size()), -0.2);
    if (!(bandwidth > 0) || !std::isfinite(bandwidth))
      bandwidth = std::max((group.maximum - group.minimum) / 10.0,
                           std::max(std::abs(group.median), 1.0) * 0.01);
    group.bandwidth = bandwidth;
    const double first = group.minimum - 3 * bandwidth;
    const double last = group.maximum + 3 * bandwidth;
    if (!std::isfinite(first) || !std::isfinite(last) ||
        !std::isfinite(last - first) || !(last > first))
      return fail(QStringLiteral("Density domain overflow"));
    group.density.reserve(DensitySamples);
    for (int point = 0; point < DensitySamples; ++point) {
      const double at = first + (last - first) * point / (DensitySamples - 1);
      long double kernels = 0;
      for (double value : sorted) {
        const double z = (at - value) / bandwidth;
        kernels += std::exp(-0.5 * z * z);
      }
      const double amount = static_cast<double>(
          kernels / (sorted.size() * bandwidth * std::sqrt(2 * Pi)));
      if (!std::isfinite(amount))
        return fail(QStringLiteral("Density overflow"));
      group.density.append({at, amount});
    }
    lower = std::min(lower, first);
    upper = std::max(upper, last);

    if (source.contains(QStringLiteral("interval"))) {
      const QVariantMap interval =
          source.value(QStringLiteral("interval")).toMap();
      const QString meaning =
          interval.value(QStringLiteral("meaning")).toString().trimmed();
      if (meaning.isEmpty())
        return fail(QStringLiteral("Interval meaning is required"));
      if (!number(interval.value(QStringLiteral("low")), &group.intervalLow) ||
          !number(interval.value(QStringLiteral("high")),
                  &group.intervalHigh) ||
          group.intervalLow > group.intervalHigh)
        return fail(
            QStringLiteral("Interval endpoints must be finite and ordered"));
      if (intervalMeaning.isEmpty())
        intervalMeaning = meaning;
      if (intervalMeaning != meaning)
        return fail(QStringLiteral("Interval meanings must match"));
      if (interval.contains(QStringLiteral("estimate")) &&
          !number(interval.value(QStringLiteral("estimate")), &group.estimate))
        return fail(QStringLiteral("Interval estimate must be finite"));
      group.hasInterval = true;
      lower = std::min(lower, group.intervalLow);
      upper = std::max(upper, group.intervalHigh);
      lower = std::min(lower, group.estimate);
      upper = std::max(upper, group.estimate);
    }
    next.append(std::move(group));
  }
  if (!next.isEmpty() && !std::isfinite(upper - lower))
    return fail(QStringLiteral("Plot domain overflow"));
  beginResetModel();
  m_groups = std::move(next);
  m_unit = unit;
  m_intervalMeaning = intervalMeaning;
  m_minimum = m_groups.isEmpty() ? 0 : lower;
  m_maximum = m_groups.isEmpty() ? 1 : upper;
  m_error.clear();
  ++m_generation;
  endResetModel();
  emit stateChanged();
  return true;
}

int DistributionModel::indexOfId(const QString &id) const {
  for (int index = 0; index < m_groups.size(); ++index)
    if (m_groups[index].id == id)
      return index;
  return -1;
}

QVariantList DistributionModel::sourceValues(const QString &id) const {
  const int index = indexOfId(id);
  if (index < 0)
    return {};
  QVariantList rows;
  rows.reserve(m_groups[index].values.size());
  for (int valueIndex = 0; valueIndex < m_groups[index].values.size();
       ++valueIndex)
    rows.append(QVariantMap{
        {QStringLiteral("index"), valueIndex + 1},
        {QStringLiteral("value"), m_groups[index].values[valueIndex]},
        {QStringLiteral("unit"), m_unit}});
  return rows;
}

QString DistributionModel::summaryCsv() const {
  QString csv =
      QStringLiteral("id,label,unit,count,min,q1,median,q3,max,mean,interval_"
                     "low,interval_high,interval_meaning,estimate\n");
  for (const Group &group : m_groups) {
    auto numeric = [](double value) { return QString::number(value, 'g', 17); };
    csv += csvCell(group.id) + QLatin1Char(',') + csvCell(group.label) +
           QLatin1Char(',') + csvCell(m_unit) + QLatin1Char(',') +
           QString::number(group.values.size()) + QLatin1Char(',') +
           numeric(group.minimum) + QLatin1Char(',') + numeric(group.q1) +
           QLatin1Char(',') + numeric(group.median) + QLatin1Char(',') +
           numeric(group.q3) + QLatin1Char(',') + numeric(group.maximum) +
           QLatin1Char(',') + numeric(group.mean) + QLatin1Char(',') +
           (group.hasInterval ? numeric(group.intervalLow) : QString()) +
           QLatin1Char(',') +
           (group.hasInterval ? numeric(group.intervalHigh) : QString()) +
           QLatin1Char(',') +
           csvCell(group.hasInterval ? m_intervalMeaning : QString()) +
           QLatin1Char(',') +
           (group.hasInterval ? numeric(group.estimate) : QString()) +
           QLatin1Char('\n');
  }
  return csv;
}

} // namespace QindaTK
