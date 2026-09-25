// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "query_core.h"

#include <QVariantMap>
#include <memory>

namespace QindaTK {
class LocalDatabaseProvider;

// A bounded, immutable QuerySource snapshot of one SQLite table (WP-059,
// docs/forms-reports.md#evaluation). Reports evaluate filters, grouping and
// totals through the WP-029 QueryEngine over this snapshot.
// AGENT-CONTRACT (docs/query-pivot.md#query-contract): field IDs are the
// SQLite column names; each declared type maps to one QueryType by SQLite's
// affinity rules (fieldType), and every stored value is normalized to that
// type or becomes an error value, which QueryEngine rejects as a whole.
// problem() names the first such value so a host can show which column
// holds data of the wrong type.
class SqliteTableSource final : public QuerySource {
public:
    static constexpr qsizetype MaximumRows = 4000;

    // Reads every supported column of `table` (at most MaximumRows rows).
    // Returns null and sets `error` when the table is unknown, has no
    // reportable column, is larger than the bound or cannot be read.
    static std::unique_ptr<SqliteTableSource> read(LocalDatabaseProvider *provider,
                                                   const QString &table, QString *error);
    // The QuerySchema of a LocalDatabaseProvider::schema() map. BLOB and
    // untyped columns cannot be reported and are listed in `unsupported`.
    static QuerySchema schemaFor(const QString &table, const QVariantMap &providerSchema,
                                 QStringList *unsupported = nullptr);
    static QueryType fieldType(const QString &declaredType, bool *supported = nullptr);
    static QString typeName(QueryType type);
    static bool isNumeric(QueryType type)
    { return type == QueryType::Integer || type == QueryType::Number; }

    QuerySchema schema() const override { return m_schema; }
    QString revision() const override { return m_revision; }
    qsizetype rowCount() const override { return m_rows.size(); }
    QueryRow rowAt(qsizetype row, const QStringList &requestedFields) const override;
    QueryCapabilities capabilities() const override { return QueryCapabilities::local(); }
    QString problem() const { return m_problem; }

private:
    SqliteTableSource() = default;
    QuerySchema m_schema;
    QString m_revision;
    QString m_problem;
    QVector<QueryRow> m_rows;
};

} // namespace QindaTK
