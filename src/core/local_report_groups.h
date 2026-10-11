// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "local_report_spec.h"

namespace QindaTK::LocalReport {
// A temporary, immutable projection of an existing report snapshot. Original
// field IDs and values remain unchanged; each group gets its own derived key.
class GroupSource final : public QuerySource {
public:
    GroupSource(const QuerySource &source, const QVector<Group> &groups);
    QuerySchema schema() const override { return m_schema; }
    QString revision() const override { return m_source.revision(); }
    qsizetype rowCount() const override { return m_source.rowCount(); }
    QueryRow rowAt(qsizetype row, const QStringList &fields) const override;
    QueryCapabilities capabilities() const override { return m_source.capabilities(); }
    QStringList keys() const { return m_keys; }
private:
    const QuerySource &m_source;
    QVector<Group> m_groups;
    QuerySchema m_schema;
    QStringList m_keys;
};
QueryValue groupValue(const QueryValue &value, const Group &group);
QString parseGroupOptions(const QVariantMap &map, Group *group);
QString parseReportOptions(const QVariantMap &map, const QuerySchema &schema, Spec *spec);
void sortGroupRows(QueryResult *rows, const QStringList &keys, const QVector<Group> &groups);
// Applies whole-group keep constraints with the same paginator used by
// preview/print/PDF. Oversized groups retain normal flowing pagination.
QJsonObject keepReportGroups(QJsonObject json, const QVector<QPair<int, int>> &ranges);
}
