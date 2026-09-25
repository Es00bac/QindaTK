// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include "local_database_provider.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Runs a saved report definition against the local database and lays it out
// as a paginated RichTextDocument (docs/forms-reports.md#output).
// AGENT-CONTRACT (docs/printing.md#one-output-description): `document` is a
// stable object for the model's lifetime, so a Tk.PrintAdapter or a
// RichDocumentView bound to it follows every rebuild; preview, exportPdf and
// printers share its one page geometry and painter. A failed build (bad
// definition, parameters or data) leaves the document, rowCount and bands of
// the last good build untouched and reports errorText/errorStage.
class LocalReportDocumentModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RichTextDocument *document READ document NOTIFY changed)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY changed)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY changed)
    Q_PROPERTY(QStringList columns READ columns NOTIFY changed)
    Q_PROPERTY(QVariantList bands READ bands NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString errorStage READ errorStage NOTIFY changed)
    Q_PROPERTY(bool built READ built NOTIFY changed)
    Q_PROPERTY(QString defaultPdfPath READ defaultPdfPath CONSTANT)
public:
    explicit LocalReportDocumentModel(QObject *parent = nullptr);
    RichTextDocument *document() const { return m_document; }
    int rowCount() const { return m_rowCount; }
    int groupCount() const { return m_groupCount; }
    QStringList columns() const { return m_columns; }
    QVariantList bands() const { return m_bands; }
    QString status() const { return m_status; }
    QString errorText() const { return m_error; }
    QString errorStage() const { return m_errorStage; }
    bool built() const { return m_built; }
    QString defaultPdfPath() const;
    // `report` is a design's report map (docs/forms-reports.md#report-definition);
    // `parameters` overrides its declared parameter values for this run.
    // Returns {ok, rowCount, groupCount, columns, bandCount, documentId} or
    // {ok: false, error, stage}.
    Q_INVOKABLE QVariantMap build(LocalDatabaseProvider *provider, const QVariantMap &report,
                                  const QVariantMap &parameters = {});
    // The document's default output ticket: document paper, every page,
    // through the shared print painter (RichTextDocument::exportPdf).
    Q_INVOKABLE QVariantMap exportPdf(const QString &path);
signals:
    void changed();
private:
    QVariantMap fail(const QString &stage, const QString &message);
    RichTextDocument *m_document = nullptr;
    int m_rowCount = 0;
    int m_groupCount = 0;
    QStringList m_columns;
    QVariantList m_bands;
    QString m_status;
    QString m_error;
    QString m_errorStage;
    bool m_built = false;
};
}
