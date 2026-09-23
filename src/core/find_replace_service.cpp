// SPDX-License-Identifier: LGPL-3.0-or-later
#include "find_replace_service.h"

#include <QFutureWatcher>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QtConcurrent>

namespace QindaTK {
namespace {
struct SearchReply {
    QVariantList rows;
    QString error;
};

bool wordCharacter(QChar value)
{
    return value.isLetterOrNumber() || value == QLatin1Char('_');
}

SearchReply findMatches(const QString &text, const QString &query,
                        const QVariantMap &options, int first, int end,
                        const QSharedPointer<std::atomic_bool> &cancelled)
{
    SearchReply reply;
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.value(QStringLiteral("caseSensitive")).toBool())
        flags |= QRegularExpression::CaseInsensitiveOption;
    const bool pattern = options.value(QStringLiteral("pattern")).toBool();
    const bool wholeWord = options.value(QStringLiteral("wholeWord")).toBool();
    const auto expression = QRegularExpression(
        pattern ? query : QRegularExpression::escape(query), flags);
    if (!expression.isValid()) {
        reply.error = expression.errorString();
        return reply;
    }
    auto matches = expression.globalMatch(text, first);
    while (matches.hasNext()) {
        if (cancelled->load()) return {};
        const auto match = matches.next();
        if (!match.hasMatch()) continue;
        const int start = match.capturedStart();
        const int length = match.capturedLength();
        if (start >= end) break;
        if (length == 0 || start + length > end) continue;
        if (wholeWord && ((start > 0 && wordCharacter(text[start - 1]))
            || (start + length < text.size() && wordCharacter(text[start + length])))) continue;
        if (reply.rows.size() >= 10000) {
            reply.error = QStringLiteral("Search result limit exceeded");
            reply.rows.clear();
            return reply;
        }
        const int previewStart = qMax(0, start - 24);
        const int previewEnd = qMin(text.size(), start + length + 24);
        reply.rows.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("%1:%2").arg(start).arg(length)},
            {QStringLiteral("start"), start},
            {QStringLiteral("length"), length},
            {QStringLiteral("preview"), text.mid(previewStart, previewEnd - previewStart)}});
    }
    return reply;
}
} // namespace

FindReplaceService::FindReplaceService(QObject *parent) : QObject(parent) {}

int FindReplaceService::revision() const
{
    return m_document ? m_document->revision() : -1;
}

void FindReplaceService::invalidate()
{
    if (m_searchCancel) m_searchCancel->store(true);
    m_activeRequest.clear();
    m_busy = false;
    m_results.clear();
    m_resultsRevision = -1;
    m_currentMatch = -1;
    emit stateChanged();
}

void FindReplaceService::setTextDocument(QQuickTextDocument *wrapper)
{
    if (m_wrapper == wrapper) return;
    if (m_wrapper) disconnect(m_wrapper, nullptr, this, nullptr);
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    invalidate();
    m_wrapper = wrapper;
    m_document = wrapper ? wrapper->textDocument() : nullptr;
    if (m_wrapper) {
        connect(m_wrapper, &QQuickTextDocument::textDocumentChanged, this, [this, wrapper] {
            setTextDocument(nullptr);
            setTextDocument(wrapper);
        });
        connect(m_wrapper, &QObject::destroyed, this, [this] {
            m_wrapper = nullptr;
            m_document = nullptr;
            invalidate();
        });
    }
    if (m_document) {
        connect(m_document, &QTextDocument::contentsChanged,
                this, &FindReplaceService::invalidate);
        connect(m_document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            invalidate();
        });
    }
    emit stateChanged();
}

void FindReplaceService::setReadOnly(bool value)
{
    if (m_readOnly == value) return;
    m_readOnly = value;
    emit stateChanged();
}

void FindReplaceService::setComposing(bool value)
{
    if (m_composing == value) return;
    m_composing = value;
    emit stateChanged();
}

QString FindReplaceService::search(const QString &query, const QVariantMap &options,
                                   const QVariantMap &scope)
{
    const QString requestId = QString::number(m_nextRequest++);
    invalidate();
    if (!m_document) {
        m_lastError = QStringLiteral("No text document");
        emit stateChanged();
        emit resultsReady(requestId, -1, {}, -1, m_lastError);
        return requestId;
    }
    if (query.isEmpty()) {
        m_lastError.clear();
        emit stateChanged();
        emit resultsReady(requestId, revision(), {}, -1, {});
        return requestId;
    }
    const QString snapshot = m_document->toPlainText();
    if (query.size() > 256 || snapshot.size() > 2000000) {
        m_lastError = QStringLiteral("Search input exceeds the configured limit");
        emit stateChanged();
        emit resultsReady(requestId, revision(), {}, -1, m_lastError);
        return requestId;
    }
    int start = 0;
    int end = snapshot.size();
    if (!scope.isEmpty()) {
        bool startOk = false, lengthOk = false;
        start = scope.value(QStringLiteral("start")).toInt(&startOk);
        const int length = scope.value(QStringLiteral("length")).toInt(&lengthOk);
        if (!startOk || !lengthOk || start < 0 || length < 0 || start > snapshot.size()
            || length > snapshot.size() - start) {
            m_lastError = QStringLiteral("Invalid search scope");
            emit stateChanged();
            emit resultsReady(requestId, revision(), {}, -1, m_lastError);
            return requestId;
        }
        end = start + length;
    }
    const int sourceRevision = revision();
    m_lastError.clear();
    m_busy = true;
    m_activeRequest = requestId;
    m_searchCancel = QSharedPointer<std::atomic_bool>::create(false);
    emit stateChanged();
    auto *watcher = new QFutureWatcher<SearchReply>(this);
    const auto cancellation = m_searchCancel;
    connect(watcher, &QFutureWatcher<SearchReply>::finished, this,
            [this, watcher, requestId, sourceRevision, cancellation] {
        const auto reply = watcher->result();
        watcher->deleteLater();
        if (cancellation->load() || requestId != m_activeRequest) return;
        m_activeRequest.clear();
        m_busy = false;
        if (!m_document || revision() != sourceRevision) {
            m_lastError = QStringLiteral("Document changed during search");
            emit stateChanged();
            emit resultsReady(requestId, revision(), {}, -1, m_lastError);
            return;
        }
        m_results = reply.rows;
        m_resultsRevision = sourceRevision;
        m_currentMatch = m_results.isEmpty() ? -1 : 0;
        m_lastError = reply.error;
        emit stateChanged();
        emit resultsReady(requestId, sourceRevision, m_results, m_currentMatch, m_lastError);
    });
    watcher->setFuture(QtConcurrent::run([snapshot, query, options, start, end, cancellation] {
        return findMatches(snapshot, query, options, start, end, cancellation);
    }));
    return requestId;
}

bool FindReplaceService::cancelSearch(const QString &requestId)
{
    if (requestId.isEmpty() || requestId != m_activeRequest) return false;
    if (m_searchCancel) m_searchCancel->store(true);
    m_activeRequest.clear();
    m_busy = false;
    emit stateChanged();
    return true;
}

bool FindReplaceService::setCurrentMatch(int index)
{
    if (index < 0 || index >= m_results.size() || m_resultsRevision != revision()) return false;
    if (m_currentMatch != index) {
        m_currentMatch = index;
        emit stateChanged();
    }
    return true;
}

QVariantMap FindReplaceService::replace(bool all, int expectedRevision,
                                         const QString &replacement)
{
    const QString operationId = QString::number(m_nextRequest++);
    auto failure = [this, &operationId](const QString &message) {
        m_lastError = message;
        emit stateChanged();
        const QVariantMap result{{QStringLiteral("ok"), false},
            {QStringLiteral("replaced"), 0}, {QStringLiteral("error"), message},
            {QStringLiteral("revision"), revision()}};
        emit operationFinished(operationId, result);
        return result;
    };
    if (!m_document) return failure(QStringLiteral("No text document"));
    if (m_readOnly || m_composing) return failure(QStringLiteral("Editing is unavailable"));
    if (expectedRevision != revision() || m_resultsRevision != revision())
        return failure(QStringLiteral("Search results are stale"));
    if (replacement.size() > 1000000) return failure(QStringLiteral("Replacement is too large"));
    if (m_results.isEmpty() || (!all && (m_currentMatch < 0 || m_currentMatch >= m_results.size())))
        return failure(QStringLiteral("No match selected"));
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    int replaced = 0;
    const int first = all ? m_results.size() - 1 : m_currentMatch;
    const int last = all ? 0 : m_currentMatch;
    for (int index = first; index >= last; --index) {
        const auto row = m_results[index].toMap();
        const int start = row.value(QStringLiteral("start")).toInt();
        const int length = row.value(QStringLiteral("length")).toInt();
        cursor.setPosition(start);
        cursor.setPosition(start + length, QTextCursor::KeepAnchor);
        cursor.insertText(replacement);
        ++replaced;
    }
    cursor.endEditBlock();
    m_lastError.clear();
    const QVariantMap result{{QStringLiteral("ok"), true},
        {QStringLiteral("replaced"), replaced}, {QStringLiteral("error"), QString{}},
        {QStringLiteral("revision"), revision()}};
    emit stateChanged();
    emit operationFinished(operationId, result);
    return result;
}

QVariantMap FindReplaceService::replaceCurrent(int expectedRevision, const QString &replacement)
{
    return replace(false, expectedRevision, replacement);
}

QVariantMap FindReplaceService::replaceAll(int expectedRevision, const QString &replacement)
{
    return replace(true, expectedRevision, replacement);
}
} // namespace QindaTK
