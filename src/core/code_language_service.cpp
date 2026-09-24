// SPDX-License-Identifier: LGPL-3.0-or-later
#include "code_language_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>
#include <QUuid>

namespace QindaTK {
namespace {
constexpr int MaxHeaderBytes = 8192;
constexpr int MaxMessageBytes = 4 * 1024 * 1024;

QString uriFor(CodeDocument *document)
{
    if (!document) return {};
    if (!document->filePath().isEmpty())
        return QUrl::fromLocalFile(QFileInfo(document->filePath()).absoluteFilePath())
            .toString(QUrl::FullyEncoded);
    return QUrl::fromLocalFile(QDir::tempPath() + QStringLiteral("/qindatk-lsp-")
                               + QUuid::createUuid().toString(QUuid::WithoutBraces)
                               + QStringLiteral(".cpp")).toString(QUrl::FullyEncoded);
}
} // namespace

CodeLanguageService::CodeLanguageService(QObject *parent) : QObject(parent)
{
    m_startTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("LSP initialize timed out"));
    });
    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning) m_process.kill();
    });
    connect(&m_process, &QProcess::started, this, [this] {
        m_startTimer.start(15000);
        m_initializeId = sendRequest(QStringLiteral("initialize"), {
            {QStringLiteral("processId"), qint64(QCoreApplication::applicationPid())},
            {QStringLiteral("rootUri"), QJsonValue::Null},
            {QStringLiteral("capabilities"), QJsonObject{
                {QStringLiteral("general"), QJsonObject{
                    {QStringLiteral("positionEncodings"), QJsonArray{QStringLiteral("utf-16")}}}},
                {QStringLiteral("textDocument"), QJsonObject{
                    {QStringLiteral("publishDiagnostics"), QJsonObject{}},
                    {QStringLiteral("completion"), QJsonObject{}}}}}},
            {QStringLiteral("clientInfo"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("QindaTK")}}}
        });
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CodeLanguageService::readMessages);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_process.readAllStandardError(); // Drain server logs; they are not protocol frames.
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        m_startTimer.stop();
        m_stopTimer.stop();
        m_input.clear();
        m_expectedBytes = -1;
        m_completion = {};
        m_initializeId = m_shutdownId = -1;
        m_sentRevision = -1;
        if (m_document) {
            m_document->publishDiagnostics(m_document->revision(), {});
            m_document->publishCompletions(m_document->revision(), 0, 0, {});
        }
        if (m_status != QStringLiteral("stopping") && m_status != QStringLiteral("error")
            && (exitStatus != QProcess::NormalExit || exitCode != 0))
            setError(QStringLiteral("Language server exited (%1)").arg(exitCode));
        setStatus(m_error.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("error"));
    });
}

CodeLanguageService::~CodeLanguageService()
{
    disconnect(m_textConnection);
    disconnect(m_pathConnection);
    disconnect(m_destroyConnection);
    m_startTimer.stop();
    m_stopTimer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(500)) {
            m_process.kill();
            m_process.waitForFinished(500);
        }
    }
}

void CodeLanguageService::setDocument(CodeDocument *document)
{
    if (m_document == document) return;
    if (m_status == QStringLiteral("ready")) closed();
    disconnect(m_textConnection);
    disconnect(m_pathConnection);
    disconnect(m_destroyConnection);
    m_document = document;
    m_uri = uriFor(document);
    if (document) {
        m_textConnection = connect(document, &CodeDocument::textChanged,
                                   this, &CodeLanguageService::changed);
        m_pathConnection = connect(document, &CodeDocument::filePathChanged, this, [this] {
            if (m_status == QStringLiteral("ready")) closed();
            m_uri = uriFor(m_document);
            emit documentChanged();
            if (m_status == QStringLiteral("ready")) opened();
        });
        m_destroyConnection = connect(document, &QObject::destroyed, this, [this] {
            if (m_status == QStringLiteral("ready") && !m_uri.isEmpty())
                notify(QStringLiteral("textDocument/didClose"), {
                    {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), m_uri}}}
                });
            m_document = nullptr;
            m_uri.clear();
            stop();
            emit documentChanged();
        });
    }
    emit documentChanged();
    if (m_status == QStringLiteral("ready") && document) opened();
}

void CodeLanguageService::setProgram(const QString &program)
{
    if (m_program == program) return;
    m_program = program;
    emit configurationChanged();
}

void CodeLanguageService::setArguments(const QStringList &arguments)
{
    if (m_arguments == arguments) return;
    m_arguments = arguments;
    emit configurationChanged();
}

void CodeLanguageService::setLanguageId(const QString &languageId)
{
    if (m_languageId == languageId) return;
    if (m_status == QStringLiteral("ready")) closed();
    m_languageId = languageId;
    emit configurationChanged();
    if (m_status == QStringLiteral("ready")) opened();
}

void CodeLanguageService::setStatus(const QString &status)
{
    if (m_status == status) return;
    m_status = status;
    emit statusChanged();
}

void CodeLanguageService::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

bool CodeLanguageService::start()
{
    if (!m_document || m_program.isEmpty() || m_process.state() != QProcess::NotRunning)
        return false;
    setError({});
    m_input.clear();
    m_expectedBytes = -1;
    m_nextId = 1;
    m_completion = {};
    setStatus(QStringLiteral("starting"));
    m_process.start(m_program, m_arguments);
    return true;
}

void CodeLanguageService::stop()
{
    m_startTimer.stop();
    if (m_process.state() == QProcess::NotRunning) {
        setStatus(QStringLiteral("stopped"));
        return;
    }
    if (m_status == QStringLiteral("stopping")) return;
    if (m_status == QStringLiteral("ready")) {
        cancelCompletion();
        closed();
        m_shutdownId = sendRequest(QStringLiteral("shutdown"), {});
    } else {
        m_process.terminate();
    }
    setStatus(QStringLiteral("stopping"));
    m_stopTimer.start(1500);
}

void CodeLanguageService::send(const QJsonObject &message)
{
    if (m_process.state() != QProcess::Running) return;
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process.write("Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
}

int CodeLanguageService::sendRequest(const QString &method, const QJsonObject &params)
{
    const int id = m_nextId++;
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("id"), id}, {QStringLiteral("method"), method},
          {QStringLiteral("params"), params}});
    return id;
}

void CodeLanguageService::notify(const QString &method, const QJsonObject &params)
{
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

void CodeLanguageService::fail(const QString &error)
{
    setError(error);
    setStatus(QStringLiteral("error"));
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
}

void CodeLanguageService::readMessages()
{
    m_input += m_process.readAllStandardOutput();
    while (true) {
        if (m_expectedBytes < 0) {
            const int end = m_input.indexOf("\r\n\r\n");
            if (end < 0) {
                if (m_input.size() > MaxHeaderBytes) fail(QStringLiteral("LSP header too large"));
                return;
            }
            if (end > MaxHeaderBytes) { fail(QStringLiteral("LSP header too large")); return; }
            const QList<QByteArray> lines = m_input.left(end).split('\n');
            int length = -1;
            for (QByteArray line : lines) {
                line = line.trimmed();
                if (!line.toLower().startsWith("content-length:")) continue;
                bool ok = false;
                length = line.mid(sizeof("content-length:") - 1).trimmed().toInt(&ok);
                if (!ok) length = -1;
            }
            if (length < 0 || length > MaxMessageBytes) {
                fail(QStringLiteral("Invalid LSP Content-Length")); return;
            }
            m_input.remove(0, end + 4);
            m_expectedBytes = length;
        }
        if (m_input.size() < m_expectedBytes) return;
        const QByteArray body = m_input.left(m_expectedBytes);
        m_input.remove(0, m_expectedBytes);
        m_expectedBytes = -1;
        QJsonParseError parseError;
        const QJsonDocument message = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !message.isObject()) {
            fail(QStringLiteral("Invalid LSP JSON message")); return;
        }
        handleMessage(message.object());
        if (m_status == QStringLiteral("error")) return;
    }
}

void CodeLanguageService::handleMessage(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    if (!method.isEmpty()) {
        if (method == QStringLiteral("textDocument/publishDiagnostics"))
            handleDiagnostics(message.value(QStringLiteral("params")).toObject());
        else if (message.contains(QStringLiteral("id"))) {
            const QJsonValue id = message.value(QStringLiteral("id"));
            const QJsonValue result = method == QStringLiteral("workspace/configuration")
                                          ? QJsonValue(QJsonArray{}) : QJsonValue(QJsonValue::Null);
            send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                  {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
        }
        return;
    }
    if (!message.contains(QStringLiteral("id"))) return;
    const int id = message.value(QStringLiteral("id")).toInt(-1);
    if (id < 0) return;
    if (id == m_initializeId) {
        m_initializeId = -1;
        m_startTimer.stop();
        if (message.contains(QStringLiteral("error"))) {
            fail(QStringLiteral("LSP initialize failed")); return;
        }
        const QString encoding = message.value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("capabilities")).toObject()
                                     .value(QStringLiteral("positionEncoding")).toString();
        if (!encoding.isEmpty() && encoding != QStringLiteral("utf-16")) {
            fail(QStringLiteral("LSP server requires unsupported position encoding: ") + encoding);
            return;
        }
        notify(QStringLiteral("initialized"), {});
        setStatus(QStringLiteral("ready"));
        opened();
    } else if (id == m_shutdownId) {
        m_shutdownId = -1;
        notify(QStringLiteral("exit"), {});
    } else if (id == m_completion.id) {
        handleCompletion(message);
    }
}

void CodeLanguageService::opened()
{
    if (!m_document || m_status != QStringLiteral("ready")) return;
    ++m_version;
    m_sentRevision = m_document->revision();
    m_sentText = m_document->text();
    notify(QStringLiteral("textDocument/didOpen"), {
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("uri"), m_uri}, {QStringLiteral("languageId"), m_languageId},
            {QStringLiteral("version"), m_version}, {QStringLiteral("text"), m_sentText}}}
    });
}

void CodeLanguageService::closed()
{
    cancelCompletion();
    if (!m_uri.isEmpty()) notify(QStringLiteral("textDocument/didClose"), {
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), m_uri}}}
    });
    if (m_document) {
        m_document->publishDiagnostics(m_document->revision(), {});
        m_document->publishCompletions(m_document->revision(), 0, 0, {});
    }
    m_sentRevision = -1;
    m_sentText.clear();
}

void CodeLanguageService::changed()
{
    if (!m_document || m_status != QStringLiteral("ready")) return;
    const QString text = m_document->text();
    if (text == m_sentText) {
        if (m_sentRevision != m_document->revision()) cancelCompletion();
        m_sentRevision = m_document->revision();
        return;
    }
    cancelCompletion();
    ++m_version;
    m_sentRevision = m_document->revision();
    m_sentText = text;
    notify(QStringLiteral("textDocument/didChange"), {
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("uri"), m_uri}, {QStringLiteral("version"), m_version}}},
        {QStringLiteral("contentChanges"), QJsonArray{
            QJsonObject{{QStringLiteral("text"), m_sentText}}}}
    });
}

void CodeLanguageService::cancelCompletion()
{
    if (m_completion.id < 0) return;
    notify(QStringLiteral("$/cancelRequest"), {{QStringLiteral("id"), m_completion.id}});
    m_completion = {};
}

QJsonObject CodeLanguageService::lspPosition(int offset) const
{
    const QVariantMap place = m_document->lineColumn(offset);
    return {{QStringLiteral("line"), place.value(QStringLiteral("line")).toInt() - 1},
            {QStringLiteral("character"), place.value(QStringLiteral("column")).toInt() - 1}};
}

int CodeLanguageService::sourcePosition(const QJsonObject &position) const
{
    if (!m_document || !position.value(QStringLiteral("line")).isDouble()
        || !position.value(QStringLiteral("character")).isDouble()) return -1;
    const int line = position.value(QStringLiteral("line")).toInt() + 1;
    const int column = position.value(QStringLiteral("character")).toInt() + 1;
    const int offset = m_document->positionForLineColumn(line, column);
    if (offset < 0) return -1;
    const QVariantMap roundtrip = m_document->lineColumn(offset);
    return roundtrip.value(QStringLiteral("line")).toInt() == line
                   && roundtrip.value(QStringLiteral("column")).toInt() == column ? offset : -1;
}

bool CodeLanguageService::requestCompletion(int cursorPosition, int prefixStart)
{
    if (m_status != QStringLiteral("ready") || !m_document
        || m_sentRevision != m_document->revision()
        || prefixStart < 0 || cursorPosition < prefixStart
        || cursorPosition > m_document->text().size()) return false;
    cancelCompletion();
    m_completion = {sendRequest(QStringLiteral("textDocument/completion"), {
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), m_uri}}},
        {QStringLiteral("position"), lspPosition(cursorPosition)}
    }), m_document->revision(), m_version, cursorPosition, prefixStart};
    return true;
}

void CodeLanguageService::handleDiagnostics(const QJsonObject &params)
{
    if (m_status != QStringLiteral("ready") || !m_document
        || params.value(QStringLiteral("uri")).toString() != m_uri
        || !params.value(QStringLiteral("version")).isDouble()
        || params.value(QStringLiteral("version")).toInt() != m_version
        || m_sentRevision != m_document->revision()) return;
    QVariantList converted;
    const QJsonArray diagnostics = params.value(QStringLiteral("diagnostics")).toArray();
    for (const QJsonValue &entry : diagnostics) {
        if (converted.size() >= 2000) break;
        const QJsonObject item = entry.toObject();
        const QJsonObject range = item.value(QStringLiteral("range")).toObject();
        const int start = sourcePosition(range.value(QStringLiteral("start")).toObject());
        const int end = sourcePosition(range.value(QStringLiteral("end")).toObject());
        if (start < 0 || end < start) continue;
        const int severity = item.value(QStringLiteral("severity")).toInt();
        const QString level = severity == 1 ? QStringLiteral("error")
                              : severity == 2 ? QStringLiteral("warning")
                              : severity == 3 ? QStringLiteral("info") : QStringLiteral("hint");
        converted.append(QVariantMap{{QStringLiteral("start"), start},
                                     {QStringLiteral("length"), end - start},
                                     {QStringLiteral("severity"), level},
                                     {QStringLiteral("message"), item.value(QStringLiteral("message")).toString()},
                                     {QStringLiteral("source"), item.value(QStringLiteral("source")).toString()}});
    }
    m_document->publishDiagnostics(m_sentRevision, converted);
}

void CodeLanguageService::handleCompletion(const QJsonObject &message)
{
    const CompletionRequest request = m_completion;
    m_completion = {};
    if (!m_document || m_status != QStringLiteral("ready")
        || request.revision != m_document->revision() || request.version != m_version
        || message.contains(QStringLiteral("error"))) return;
    const QJsonValue result = message.value(QStringLiteral("result"));
    const QJsonArray items = result.isArray() ? result.toArray()
                           : result.toObject().value(QStringLiteral("items")).toArray();
    QVariantList candidates;
    for (const QJsonValue &entry : items) {
        if (candidates.size() >= 100) break;
        const QJsonObject item = entry.toObject();
        const QString label = item.value(QStringLiteral("label")).toString();
        if (label.isEmpty()) continue;
        QString insertion = item.value(QStringLiteral("insertText")).toString();
        const QJsonObject edit = item.value(QStringLiteral("textEdit")).toObject();
        if (!edit.isEmpty()) {
            const QJsonObject range = edit.value(QStringLiteral("range")).toObject();
            if (sourcePosition(range.value(QStringLiteral("start")).toObject()) != request.prefixStart
                || sourcePosition(range.value(QStringLiteral("end")).toObject()) != request.position)
                continue;
            insertion = edit.value(QStringLiteral("newText")).toString();
        }
        if (insertion.isEmpty()) insertion = label;
        if (item.value(QStringLiteral("insertTextFormat")).toInt(1) == 2
            && insertion.contains(QLatin1Char('$'))) continue; // No snippet expander.
        candidates.append(QVariantMap{{QStringLiteral("label"), label},
                                      {QStringLiteral("insertText"), insertion}});
    }
    m_document->publishCompletions(request.revision, request.prefixStart,
                                   request.position, candidates);
}

} // namespace QindaTK
