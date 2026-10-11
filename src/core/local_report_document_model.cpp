// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_report_document_model.h"

#include "local_database_provider.h"
#include "local_report_spec.h"
#include "rich_text_codec.h"
#include "sqlite_query_source.h"
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>

namespace QindaTK {
namespace {
QString bandKind(LocalReport::Band::Kind kind)
{
    using Kind = LocalReport::Band::Kind;
    switch (kind) {
    case Kind::ReportHeader: return QStringLiteral("reportHeader");
    case Kind::ReportInfo: return QStringLiteral("reportInfo");
    case Kind::GroupHeader: return QStringLiteral("groupHeader");
    case Kind::Detail: return QStringLiteral("detail");
    case Kind::GroupFooter: return QStringLiteral("groupFooter");
    case Kind::ReportFooter: return QStringLiteral("reportFooter");
    case Kind::Empty: return QStringLiteral("empty");
    }
    return {};
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

QVariantMap LocalReportDocumentModel::fail(const QString &stage, const QString &message)
{
    m_error = message;
    m_errorStage = stage;
    m_status = tr("Report not rebuilt · %1").arg(message);
    emit changed();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}, {QStringLiteral("stage"), stage}};
}

QVariantMap LocalReportDocumentModel::build(LocalDatabaseProvider *provider, const QVariantMap &report,
                                             const QVariantMap &parameters)
{
    // AGENT-GUARD: every failure returns before the document is touched;
    // RichTextCodec::fromJsonObject validates the whole layout before it
    // replaces anything, so a bad row never leaves a half-built report.
    if (!provider || !provider->connected()) return fail(QStringLiteral("query"), tr("SQLite database is not connected"));
    QString error;
    const auto source = SqliteTableSource::read(provider, report.value(QStringLiteral("tableName")).toString(), &error);
    if (!source) return fail(QStringLiteral("query"), error);
    LocalReport::Spec spec;
    QString stage;
    error = LocalReport::parse(report, source->schema(), parameters, &spec, &stage);
    if (!error.isEmpty()) return fail(stage, error);
    const LocalReport::Output output = LocalReport::evaluate(*source, spec);
    if (!output.error.isEmpty()) return fail(output.stage, output.error);
    OperationError decodeError;
    const QJsonObject json = LocalReport::documentJson(output, spec, m_document->documentIdString());
    if (!RichTextCodec::fromJsonObject(json, *m_document, &decodeError))
        return fail(QStringLiteral("layout"), decodeError.message);

    QVariantList bands;
    for (const LocalReport::Band &band : output.bands)
        bands.append(QVariantMap{{QStringLiteral("kind"), bandKind(band.kind)}, {QStringLiteral("level"), band.level},
                                 {QStringLiteral("text"), band.text}, {QStringLiteral("cells"), band.cells},
                                 {QStringLiteral("pageBreakBefore"), band.pageBreakBefore},
                                 {QStringLiteral("pageBreakAfter"), band.pageBreakAfter},
                                 {QStringLiteral("keepTogether"), band.keepTogether}, {QStringLiteral("keepWith"), band.keepWith}});
    m_bands = bands;
    m_columns = output.columnLabels;
    m_rowCount = output.detailRows;
    m_groupCount = output.groupCount;
    m_built = true;
    m_error.clear();
    m_errorStage.clear();
    m_status = tr("Paginated document ready · %1 rows · %2 groups · %3 columns")
                   .arg(m_rowCount).arg(m_groupCount).arg(m_columns.size());
    emit changed();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("rowCount"), m_rowCount},
            {QStringLiteral("groupCount"), m_groupCount}, {QStringLiteral("columns"), m_columns},
            {QStringLiteral("bandCount"), m_bands.size()},
            {QStringLiteral("documentId"), m_document->documentIdString()}};
}

QVariantMap LocalReportDocumentModel::exportPdf(const QString &path)
{
    const QString localPath = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    if (localPath.trimmed().isEmpty()) return fail(QStringLiteral("output"), tr("PDF path is empty"));
    if (!m_built) return fail(QStringLiteral("output"), tr("Build a report document before exporting"));
    if (!QDir().mkpath(QFileInfo(localPath).absolutePath()))
        return fail(QStringLiteral("output"), tr("Cannot create the PDF output directory"));
    if (!m_document->exportPdf(localPath)) return fail(QStringLiteral("output"), m_document->lastError());
    const QString absolute = QFileInfo(localPath).absoluteFilePath();
    m_status = tr("PDF written · %1").arg(absolute);
    emit changed();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), absolute},
            {QStringLiteral("rowCount"), m_rowCount}};
}
}
