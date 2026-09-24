// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include "local_database_provider.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Turns a saved report binding into a paginated RichTextDocument and PDF.
class LocalReportDocumentModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RichTextDocument *document READ document NOTIFY changed)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString defaultPdfPath READ defaultPdfPath CONSTANT)
public:
    explicit LocalReportDocumentModel(QObject *parent = nullptr);
    RichTextDocument *document() const { return m_document; }
    int rowCount() const { return m_rowCount; }
    QString status() const { return m_status; }
    QString defaultPdfPath() const;
    Q_INVOKABLE QVariantMap build(LocalDatabaseProvider *provider, const QVariantMap &report);
    Q_INVOKABLE QVariantMap exportPdf(const QString &path);
signals:
    void changed();
private:
    static QString quoteIdentifier(const QString &identifier);
    RichTextDocument *m_document = nullptr;
    int m_rowCount = 0;
    QString m_status;
    bool m_built = false;
};
}
