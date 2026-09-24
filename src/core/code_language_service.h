// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "code_document.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: CodeDocument owns source/revision state; this optional
// adapter owns one explicitly launched local LSP process. Results may publish
// only for the exact document revision and LSP version they analyzed.
class CodeLanguageService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(CodeDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString program READ program WRITE setProgram NOTIFY configurationChanged)
    Q_PROPERTY(QStringList arguments READ arguments WRITE setArguments NOTIFY configurationChanged)
    Q_PROPERTY(QString languageId READ languageId WRITE setLanguageId NOTIFY configurationChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QString documentUri READ documentUri NOTIFY documentChanged)
public:
    explicit CodeLanguageService(QObject *parent = nullptr);
    ~CodeLanguageService() override;

    CodeDocument *document() const { return m_document; }
    void setDocument(CodeDocument *document);
    QString program() const { return m_program; }
    void setProgram(const QString &program);
    QStringList arguments() const { return m_arguments; }
    void setArguments(const QStringList &arguments);
    QString languageId() const { return m_languageId; }
    void setLanguageId(const QString &languageId);
    QString status() const { return m_status; }
    QString errorString() const { return m_error; }
    QString documentUri() const { return m_uri; }

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool requestCompletion(int cursorPosition, int prefixStart);

signals:
    void documentChanged();
    void configurationChanged();
    void statusChanged();
    void errorChanged();

private:
    struct CompletionRequest {
        int id = -1;
        int revision = -1;
        int version = -1;
        int position = -1;
        int prefixStart = -1;
    };
    void setStatus(const QString &status);
    void setError(const QString &error);
    void send(const QJsonObject &message);
    int sendRequest(const QString &method, const QJsonObject &params);
    void notify(const QString &method, const QJsonObject &params);
    void readMessages();
    void handleMessage(const QJsonObject &message);
    void handleDiagnostics(const QJsonObject &params);
    void handleCompletion(const QJsonObject &message);
    void opened();
    void closed();
    void changed();
    void cancelCompletion();
    QJsonObject lspPosition(int offset) const;
    int sourcePosition(const QJsonObject &position) const;
    void fail(const QString &error);

    QPointer<CodeDocument> m_document;
    QMetaObject::Connection m_textConnection;
    QMetaObject::Connection m_pathConnection;
    QMetaObject::Connection m_destroyConnection;
    QProcess m_process;
    QTimer m_startTimer;
    QTimer m_stopTimer;
    QByteArray m_input;
    QString m_program;
    QStringList m_arguments;
    QString m_languageId = QStringLiteral("plaintext");
    QString m_status = QStringLiteral("stopped");
    QString m_error;
    QString m_uri;
    QString m_sentText;
    int m_nextId = 1;
    int m_initializeId = -1;
    int m_shutdownId = -1;
    int m_version = 0;
    int m_sentRevision = -1;
    int m_expectedBytes = -1;
    CompletionRequest m_completion;
};

} // namespace QindaTK
