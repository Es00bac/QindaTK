// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_report_document_model.h"

#include "local_database_provider.h"
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

namespace QindaTK {
namespace {
QVariantMap failed(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString cellText(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) return QStringLiteral("∅");
    if (value.metaType().id() == QMetaType::QByteArray)
        return QStringLiteral("[BLOB %1 bytes]").arg(value.toByteArray().size());
    return value.toString().replace(u'\n', u' ').replace(u'\r', u' ');
}
}

LocalReportDocumentModel::LocalReportDocumentModel(QObject *parent) : QObject(parent)
{
    m_document = new RichTextDocument(this);
}

QString LocalReportDocumentModel::defaultPdfPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/qindatk-report.pdf");
}

QString LocalReportDocumentModel::quoteIdentifier(const QString &identifier)
{
    return QLatin1Char('"') + QString(identifier).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
}

QVariantMap LocalReportDocumentModel::build(LocalDatabaseProvider *provider,
                                             const QVariantMap &report)
{
    if (!provider || !provider->connected()) return failed(tr("SQLite database is not connected"));
    const QString table = report.value(QStringLiteral("tableName")).toString();
    const QVariantMap schema = provider->schema(table);
    if (!schema.value(QStringLiteral("ok")).toBool()) return failed(schema.value(QStringLiteral("error")).toString());
    QStringList fields;
    QStringList numericFields;
    for (const QVariant &field : schema.value(QStringLiteral("fields")).toList()) {
        const QVariantMap entry = field.toMap();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString type = entry.value(QStringLiteral("type")).toString().toUpper();
        fields.append(name);
        if (type.contains(QLatin1String("INT")) || type.contains(QLatin1String("REAL"))
            || type.contains(QLatin1String("NUM")) || type.contains(QLatin1String("DEC"))
            || type.contains(QLatin1String("FLOAT")) || type.contains(QLatin1String("DOUBLE")))
            numericFields.append(name);
    }
    if (fields.isEmpty()) return failed(tr("Report source has no visible fields"));
    const QString filter = report.value(QStringLiteral("filterField")).toString();
    const QString group = report.value(QStringLiteral("groupField")).toString();
    const QString sum = report.value(QStringLiteral("sumField")).toString();
    if ((!filter.isEmpty() && !fields.contains(filter)) || (!group.isEmpty() && !fields.contains(group)))
        return failed(tr("Report filter or grouping field no longer matches the live schema"));
    if (!sum.isEmpty() && (group.isEmpty() || !numericFields.contains(sum)))
        return failed(tr("Report total requires a group and a numeric field in the live schema"));

    QStringList outputColumns;
    QString sql;
    if (group.isEmpty()) {
        outputColumns = fields;
        QStringList quoted;
        for (const QString &field : fields) quoted.append(quoteIdentifier(field));
        sql = QStringLiteral("SELECT ") + quoted.join(QStringLiteral(", "));
    } else {
        outputColumns = {group, QStringLiteral("rowCount")};
        if (!sum.isEmpty()) outputColumns.append(QStringLiteral("sumValue"));
        sql = QStringLiteral("SELECT ") + quoteIdentifier(group)
            + QStringLiteral(", COUNT(*) AS rowCount");
        if (!sum.isEmpty()) sql += QStringLiteral(", SUM(") + quoteIdentifier(sum)
            + QStringLiteral(") AS sumValue");
    }
    sql += QStringLiteral(" FROM ") + quoteIdentifier(table);
    QVariantList bindings;
    if (!filter.isEmpty()) {
        sql += QStringLiteral(" WHERE ") + quoteIdentifier(filter) + QStringLiteral(" = ?");
        bindings.append(report.value(QStringLiteral("filterValue")));
    }
    if (!group.isEmpty()) sql += QStringLiteral(" GROUP BY ") + quoteIdentifier(group)
        + QStringLiteral(" ORDER BY ") + quoteIdentifier(group);
    else if (fields.contains(QStringLiteral("id")))
        sql += QStringLiteral(" ORDER BY ") + quoteIdentifier(QStringLiteral("id"));
    sql += QStringLiteral(" LIMIT ?");
    bindings.append(2001);
    const QVariantMap query = provider->query(sql, bindings);
    if (!query.value(QStringLiteral("ok")).toBool()) return failed(query.value(QStringLiteral("error")).toString());
    const QVariantList rows = query.value(QStringLiteral("rows")).toList();
    if (rows.size() > 2000 || query.value(QStringLiteral("truncated")).toBool())
        return failed(tr("Report exceeds the 2000-row document bound; refine the filter"));

    QStringList lines;
    qsizetype textLength = 0;
    auto appendLine = [&lines, &textLength](const QString &line) {
        textLength += line.size() + 1;
        lines.append(line);
        return textLength <= 2 * 1024 * 1024;
    };
    const QString title = report.value(QStringLiteral("headerText")).toString().trimmed();
    if (!appendLine(title.isEmpty() ? tr("Data report") : title)
        || !appendLine(tr("Source: main.%1 · %2 rows").arg(table).arg(rows.size()))
        || !appendLine(outputColumns.join(QStringLiteral("  |  "))))
        return failed(tr("Report text exceeds the 2 MiB document bound"));
    for (const QVariant &row : rows) {
        const QVariantMap values = row.toMap();
        QStringList cells;
        for (const QString &column : outputColumns) cells.append(cellText(values.value(column)));
        if (!appendLine(cells.join(QStringLiteral("  |  "))))
            return failed(tr("Report text exceeds the 2 MiB document bound"));
    }
    const QString footer = report.value(QStringLiteral("footerText")).toString().trimmed();
    if (!footer.isEmpty() && (!appendLine(QString()) || !appendLine(footer)))
        return failed(tr("Report text exceeds the 2 MiB document bound"));

    auto *next = new RichTextDocument(this);
    const auto inserted = next->insertText(next->startPosition(), lines.join(u'\n'));
    if (!inserted) {
        next->deleteLater();
        return failed(next->lastError().isEmpty() ? tr("Could not build report document") : next->lastError());
    }
    if (m_document) m_document->deleteLater();
    m_document = next;
    m_rowCount = rows.size();
    m_built = true;
    m_status = tr("Paginated document ready · %1 rows · %2 columns").arg(m_rowCount).arg(outputColumns.size());
    emit changed();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("rowCount"), m_rowCount},
            {QStringLiteral("columns"), QVariant::fromValue(outputColumns)},
            {QStringLiteral("documentId"), m_document->documentIdString()}};
}

QVariantMap LocalReportDocumentModel::exportPdf(const QString &path)
{
    const QString localPath = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    if (localPath.trimmed().isEmpty()) return failed(tr("PDF path is empty"));
    if (!m_document || !m_built) return failed(tr("Build a report document before exporting"));
    if (!QDir().mkpath(QFileInfo(localPath).absolutePath())) return failed(tr("Cannot create the PDF output directory"));
    if (!m_document->exportPdf(localPath)) {
        m_status = m_document->lastError();
        emit changed();
        return failed(m_status);
    }
    const QString absolute = QFileInfo(localPath).absoluteFilePath();
    m_status = tr("PDF written · %1").arg(absolute);
    emit changed();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), absolute},
            {QStringLiteral("rowCount"), m_rowCount}};
}
}
