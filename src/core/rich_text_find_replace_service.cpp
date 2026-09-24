// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_find_replace_service.h"

#include <QFutureWatcher>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent>

namespace QindaTK {
namespace {
struct ParagraphSnapshot {
    QUuid id;
    QString text;
    int globalStart = 0;
};
struct SearchRow {
    QUuid paragraph;
    int rawStart = 0;
    int rawEnd = 0;
    int start = 0;
    int length = 0;
    QString preview;
};
struct SearchReply {
    QVector<SearchRow> rows;
    QString error;
};

bool wordCharacter(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

SearchReply findMatches(const QVector<ParagraphSnapshot> &paragraphs, const QString &query,
                        const QVariantMap &options, int first, int end,
                        const QSharedPointer<std::atomic_bool> &cancelled)
{
    SearchReply reply;
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.value(QStringLiteral("caseSensitive")).toBool())
        flags |= QRegularExpression::CaseInsensitiveOption;
    const bool wholeWord = options.value(QStringLiteral("wholeWord")).toBool();
    const QRegularExpression expression(options.value(QStringLiteral("pattern")).toBool()
        ? query : QRegularExpression::escape(query), flags);
    if (!expression.isValid()) { reply.error = expression.errorString(); return reply; }
    for (const ParagraphSnapshot &p : paragraphs) {
        if (cancelled->load()) return {};
        if (p.globalStart >= end) break;
        auto matches = expression.globalMatch(p.text);
        while (matches.hasNext()) {
            if (cancelled->load()) return {};
            const auto match = matches.next();
            const int offset = match.capturedStart();
            const int length = match.capturedLength();
            const int global = p.globalStart + offset;
            if (!length || global < first || global + length > end) continue;
            if (wholeWord && ((offset && wordCharacter(p.text.at(offset - 1)))
                || (offset + length < p.text.size()
                    && wordCharacter(p.text.at(offset + length))))) continue;
            // A wildcard or explicit U+FFFC query must never erase an object.
            if (p.text.mid(offset, length).contains(QChar(RichText::ObjectChar))) continue;
            if (reply.rows.size() >= 10000) {
                reply.error = QStringLiteral("Search result limit exceeded");
                reply.rows.clear();
                return reply;
            }
            const int previewStart = qMax(0, offset - 24);
            const int previewEnd = qMin(int(p.text.size()), offset + length + 24);
            QString preview = p.text.mid(previewStart, previewEnd - previewStart);
            preview.replace(QChar(RichText::ObjectChar), QChar(u'\u25a1'));
            preview.replace(QChar(RichText::LineBreakChar), QChar(u'\n'));
            reply.rows.append({p.id, offset, offset + length, global, length, preview});
        }
    }
    return reply;
}
} // namespace

RichTextFindReplaceService::RichTextFindReplaceService(QObject *parent) : QObject(parent) {}

int RichTextFindReplaceService::revision() const
{
    return m_document ? int(m_document->revision()) : -1;
}

void RichTextFindReplaceService::invalidate()
{
    if (m_searchCancel) m_searchCancel->store(true);
    m_activeRequest.clear();
    m_results.clear();
    m_matches.clear();
    m_resultsRevision = -1;
    m_currentMatch = -1;
    m_busy = false;
    emit stateChanged();
}

void RichTextFindReplaceService::setDocument(RichTextDocument *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    invalidate();
    m_document = document;
    if (document) {
        connect(document, &RichTextDocument::contentChanged, this,
                &RichTextFindReplaceService::invalidate);
        connect(document, &RichTextDocument::changed, this,
                &RichTextFindReplaceService::stateChanged);
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            invalidate();
        });
    }
    emit stateChanged();
}

void RichTextFindReplaceService::setComposing(bool composing)
{
    if (m_composing == composing) return;
    m_composing = composing;
    emit stateChanged();
}

void RichTextFindReplaceService::replyLater(const QString &requestId)
{
    const int sourceRevision = revision();
    const QString error = m_lastError;
    QTimer::singleShot(0, this, [this, requestId, sourceRevision, error] {
        emit resultsReady(requestId, sourceRevision, {}, -1, error);
    });
}

QString RichTextFindReplaceService::search(const QString &query, const QVariantMap &options,
                                           const QVariantMap &scope)
{
    const QString requestId = QString::number(m_nextRequest++);
    invalidate();
    if (!m_document || m_document->inTransaction()) {
        m_lastError = QStringLiteral("No stable rich-text document");
        emit stateChanged();
        replyLater(requestId);
        return requestId;
    }
    if (query.isEmpty()) {
        m_lastError.clear();
        emit stateChanged();
        replyLater(requestId);
        return requestId;
    }
    QVector<ParagraphSnapshot> snapshots;
    int textSize = 0;
    const auto &ordered = m_document->paragraphsInOrder();
    for (int index = 0; index < ordered.size(); ++index) {
        const auto *p = ordered.at(index);
        if (index) ++textSize; // RichTextArea joins every paragraph with newline
        ParagraphSnapshot snapshot;
        snapshot.id = p->id;
        snapshot.globalStart = textSize;
        snapshot.text = p->text;
        textSize += snapshot.text.size();
        snapshots.append(std::move(snapshot));
    }
    if (query.size() > 256 || textSize > 2000000) {
        m_lastError = QStringLiteral("Search input exceeds the configured limit");
        emit stateChanged();
        replyLater(requestId);
        return requestId;
    }
    int first = 0, end = textSize;
    if (!scope.isEmpty()) {
        bool startOk = false, lengthOk = false;
        first = scope.value(QStringLiteral("start")).toInt(&startOk);
        const int length = scope.value(QStringLiteral("length")).toInt(&lengthOk);
        if (!startOk || !lengthOk || first < 0 || length < 0 || first > textSize
            || length > textSize - first) {
            m_lastError = QStringLiteral("Invalid search scope");
            emit stateChanged();
            replyLater(requestId);
            return requestId;
        }
        end = first + length;
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
        if (!m_document || m_document->inTransaction() || revision() != sourceRevision) {
            m_lastError = QStringLiteral("Document changed during search");
            emit stateChanged();
            emit resultsReady(requestId, revision(), {}, -1, m_lastError);
            return;
        }
        m_lastError = reply.error;
        if (m_lastError.isEmpty()) {
            for (const SearchRow &row : reply.rows) {
                const RichText::Position start{row.paragraph, row.rawStart};
                const RichText::Position end{row.paragraph, row.rawEnd};
                if (!m_document->isValidPosition(start) || !m_document->isValidPosition(end))
                    continue;
                m_matches.append({row.paragraph, row.rawStart, row.rawEnd - row.rawStart});
                m_results.append(QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("%1:%2").arg(row.start).arg(row.length)},
                    {QStringLiteral("start"), row.start},
                    {QStringLiteral("length"), row.length},
                    {QStringLiteral("preview"), row.preview},
                    {QStringLiteral("paragraphId"), row.paragraph.toString(QUuid::WithoutBraces)},
                    {QStringLiteral("offset"), row.rawStart}});
            }
        }
        m_resultsRevision = sourceRevision;
        m_currentMatch = m_results.isEmpty() ? -1 : 0;
        emit stateChanged();
        emit resultsReady(requestId, sourceRevision, m_results, m_currentMatch, m_lastError);
    });
    watcher->setFuture(QtConcurrent::run([snapshots, query, options, first, end, cancellation] {
        return findMatches(snapshots, query, options, first, end, cancellation);
    }));
    return requestId;
}

bool RichTextFindReplaceService::cancelSearch(const QString &requestId)
{
    if (requestId.isEmpty() || requestId != m_activeRequest) return false;
    if (m_searchCancel) m_searchCancel->store(true);
    m_activeRequest.clear();
    m_busy = false;
    emit stateChanged();
    return true;
}

bool RichTextFindReplaceService::setCurrentMatch(int index)
{
    if (index < 0 || index >= m_matches.size() || m_resultsRevision != revision()) return false;
    if (m_currentMatch != index) { m_currentMatch = index; emit stateChanged(); }
    return true;
}

QVariantMap RichTextFindReplaceService::replace(bool all, int expectedRevision,
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
    if (!m_document) return failure(QStringLiteral("No rich-text document"));
    if (readOnly() || m_composing || m_document->inTransaction())
        return failure(QStringLiteral("Editing is unavailable"));
    if (expectedRevision != revision() || m_resultsRevision != revision())
        return failure(QStringLiteral("Search results are stale"));
    if (replacement.size() > 1000000) return failure(QStringLiteral("Replacement is too large"));
    if (m_matches.isEmpty() || (!all && (m_currentMatch < 0 || m_currentMatch >= m_matches.size())))
        return failure(QStringLiteral("No match selected"));
    if (!m_document->beginTransaction(all ? tr("Replace all") : tr("Replace")))
        return failure(m_document->lastError());
    const int first = all ? m_matches.size() - 1 : m_currentMatch;
    const int last = all ? 0 : m_currentMatch;
    int replaced = 0;
    for (int index = first; index >= last; --index) {
        const Match &match = m_matches.at(index);
        const RichText::Position start{match.paragraph, match.offset};
        const RichText::Position end{match.paragraph, match.offset + match.rawLength};
        if (!m_document->deleteRange(start, end)
            || !m_document->insertText(start, replacement)) {
            const QString error = m_document->lastError();
            m_document->cancelTransaction();
            return failure(error.isEmpty() ? QStringLiteral("Replacement failed") : error);
        }
        ++replaced;
    }
    if (!m_document->commitTransaction())
        return failure(m_document->lastError());
    m_lastError.clear();
    const QVariantMap result{{QStringLiteral("ok"), true},
        {QStringLiteral("replaced"), replaced}, {QStringLiteral("error"), QString{}},
        {QStringLiteral("revision"), revision()}};
    emit stateChanged();
    emit operationFinished(operationId, result);
    return result;
}

QVariantMap RichTextFindReplaceService::replaceCurrent(int expectedRevision, const QString &replacement)
{ return replace(false, expectedRevision, replacement); }

QVariantMap RichTextFindReplaceService::replaceAll(int expectedRevision, const QString &replacement)
{ return replace(true, expectedRevision, replacement); }

} // namespace QindaTK
