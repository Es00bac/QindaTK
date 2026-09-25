// SPDX-License-Identifier: LGPL-3.0-or-later
#include "sqlite_query_source.h"

#include "local_database_provider.h"

#include <QAtomicInteger>
#include <QCryptographicHash>
#include <QMetaType>
#include <cmath>

namespace QindaTK {
namespace {
QString quoted(const QString &identifier)
{
    return QLatin1Char('"') + QString(identifier).replace(QLatin1Char('"'), QStringLiteral("\"\""))
        + QLatin1Char('"');
}

bool isIntegerVariant(int id)
{
    return id == QMetaType::LongLong || id == QMetaType::Int || id == QMetaType::UInt
        || id == QMetaType::ULongLong || id == QMetaType::Long || id == QMetaType::ULong;
}

// One stored SQLite value as the field's QueryType. SQLite columns are
// dynamically typed, so a value of another storage class is an error value
// (never a silent guess); QueryEngine then rejects the whole refresh.
QueryValue normalized(const QVariant &value, QueryType type, QString *problem, const QString &field)
{
    if (!value.isValid() || value.isNull()) return QueryValue::null();
    const int id = value.metaType().id();
    const bool integer = isIntegerVariant(id);
    const bool real = id == QMetaType::Double;
    const bool text = id == QMetaType::QString;
    auto wrong = [&](const QString &stored) {
        const QString message = QStringLiteral("Column %1 holds %2 in a %3 column")
                                    .arg(field, stored, SqliteTableSource::typeName(type));
        if (problem->isEmpty()) *problem = message;
        return QueryValue::failure(message);
    };
    const QString stored = integer ? QStringLiteral("an integer") : real ? QStringLiteral("a real number")
        : text ? QStringLiteral("text") : QStringLiteral("binary data");
    switch (type) {
    case QueryType::Integer:
        if (integer) return QueryValue::integer(value.toLongLong());
        if (real) {
            const double n = value.toDouble();
            if (std::isfinite(n) && std::floor(n) == n && std::fabs(n) < 9.0e18)
                return QueryValue::integer(qint64(n));
        }
        return wrong(stored);
    case QueryType::Number:
        if ((integer || real) && std::isfinite(value.toDouble())) return QueryValue::number(value.toDouble());
        return wrong(stored);
    case QueryType::Boolean:
        if (integer && (value.toLongLong() == 0 || value.toLongLong() == 1))
            return QueryValue::boolean(value.toLongLong() == 1);
        return wrong(stored);
    case QueryType::Text:
        if (text) return QueryValue::text(value.toString());
        if (integer) return QueryValue::text(QString::number(value.toLongLong()));
        if (real) return QueryValue::text(QString::number(value.toDouble(), 'g', 17));
        return wrong(stored);
    case QueryType::Null:
    case QueryType::Error:
        break;
    }
    return wrong(stored);
}
} // namespace

QueryType SqliteTableSource::fieldType(const QString &declaredType, bool *supported)
{
    // SQLite type affinity (https://sqlite.org/datatype3.html §3.1), with
    // BOOL and DATE/TIME named explicitly because hosts declare them.
    const QString t = declaredType.toUpper();
    if (supported) *supported = true;
    if (t.contains(QLatin1String("BOOL"))) return QueryType::Boolean;
    if (t.contains(QLatin1String("INT"))) return QueryType::Integer;
    if (t.contains(QLatin1String("CHAR")) || t.contains(QLatin1String("CLOB"))
        || t.contains(QLatin1String("TEXT"))) return QueryType::Text;
    if (t.contains(QLatin1String("BLOB")) || t.trimmed().isEmpty()) {
        if (supported) *supported = false;
        return QueryType::Error;
    }
    if (t.contains(QLatin1String("REAL")) || t.contains(QLatin1String("FLOA"))
        || t.contains(QLatin1String("DOUB"))) return QueryType::Number;
    if (t.contains(QLatin1String("DATE")) || t.contains(QLatin1String("TIME"))) return QueryType::Text;
    return QueryType::Number; // NUMERIC affinity: NUMERIC, DECIMAL(10,2), ...
}

QString SqliteTableSource::typeName(QueryType type)
{
    switch (type) {
    case QueryType::Boolean: return QStringLiteral("boolean");
    case QueryType::Integer: return QStringLiteral("integer");
    case QueryType::Number: return QStringLiteral("number");
    case QueryType::Text: return QStringLiteral("text");
    case QueryType::Null: return QStringLiteral("null");
    case QueryType::Error: break;
    }
    return QStringLiteral("unsupported");
}

QuerySchema SqliteTableSource::schemaFor(const QString &table, const QVariantMap &providerSchema,
                                         QStringList *unsupported)
{
    QuerySchema schema;
    schema.id = QStringLiteral("sqlite:main.") + table;
    QByteArray signature = schema.id.toUtf8();
    for (const QVariant &entry : providerSchema.value(QStringLiteral("fields")).toList()) {
        const QVariantMap field = entry.toMap();
        const QString name = field.value(QStringLiteral("name")).toString();
        bool ok = false;
        const QueryType type = fieldType(field.value(QStringLiteral("type")).toString(), &ok);
        if (!ok) {
            if (unsupported) unsupported->append(name);
            continue;
        }
        // INTEGER PRIMARY KEY is the rowid alias and never null; other
        // SQLite key columns accept NULL unless declared NOT NULL.
        const bool rowid = type == QueryType::Integer
            && field.value(QStringLiteral("primaryKeyOrder")).toInt() > 0;
        const bool nullable = !field.value(QStringLiteral("notNull")).toBool() && !rowid;
        schema.fields.append({name, type, nullable});
        signature += '\x1f' + name.toUtf8() + ':' + typeName(type).toUtf8() + (nullable ? "?" : "!");
    }
    schema.revision = QString::fromLatin1(
        QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex().left(16));
    return schema;
}

std::unique_ptr<SqliteTableSource> SqliteTableSource::read(LocalDatabaseProvider *provider,
                                                           const QString &table, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return std::unique_ptr<SqliteTableSource>();
    };
    if (!provider || !provider->connected()) return fail(QStringLiteral("SQLite database is not connected"));
    const QVariantMap live = provider->schema(table);
    if (!live.value(QStringLiteral("ok")).toBool())
        return fail(QStringLiteral("Report table %1: %2").arg(table, live.value(QStringLiteral("error")).toString()));
    std::unique_ptr<SqliteTableSource> source(new SqliteTableSource);
    source->m_schema = schemaFor(table, live);
    if (source->m_schema.fields.isEmpty()) return fail(QStringLiteral("Report table %1 has no reportable columns").arg(table));
    QStringList columns;
    for (const QueryField &field : std::as_const(source->m_schema.fields)) columns.append(quoted(field.id));
    // AGENT-NOTE: one bounded, parameter-bound read; filters and grouping
    // run in QueryEngine so every report uses the WP-029 null and type rules.
    const QString sql = QStringLiteral("SELECT ") + columns.join(QStringLiteral(", ")) + QStringLiteral(" FROM ")
        + quoted(table) + QStringLiteral(" LIMIT ?");
    const QVariantMap result = provider->query(sql, {QVariant::fromValue<qlonglong>(MaximumRows + 1)});
    if (!result.value(QStringLiteral("ok")).toBool())
        return fail(result.value(QStringLiteral("error")).toString());
    const QVariantList rows = result.value(QStringLiteral("rows")).toList();
    if (rows.size() > MaximumRows)
        return fail(QStringLiteral("Report table %1 exceeds the %2-row report bound").arg(table).arg(MaximumRows));
    source->m_rows.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QVariantMap values = rows.at(i).toMap();
        QueryRow row;
        // Zero padded, so the engine's tie break by row ID keeps table order.
        row.id = QStringLiteral("r%1").arg(i, 6, 10, QLatin1Char('0'));
        for (const QueryField &field : std::as_const(source->m_schema.fields))
            row.values.insert(field.id, normalized(values.value(field.id), field.type, &source->m_problem, field.id));
        source->m_rows.append(std::move(row));
    }
    static QAtomicInteger<quint64> serial = 0;
    source->m_revision = QStringLiteral("sqlite-snapshot-%1").arg(++serial);
    return source;
}

QueryRow SqliteTableSource::rowAt(qsizetype row, const QStringList &requestedFields) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    const QueryRow &stored = m_rows.at(row);
    QueryRow out;
    out.id = stored.id;
    for (const QString &field : requestedFields)
        if (stored.values.contains(field)) out.values.insert(field, stored.values.value(field));
    return out;
}

} // namespace QindaTK
