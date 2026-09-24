// SPDX-License-Identifier: LGPL-3.0-or-later
#include "code_document.h"

#include <QFile>
#include <QQuickTextDocument>
#include <QSaveFile>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <algorithm>

namespace QindaTK {
namespace {

class TokenHighlighter final : public QSyntaxHighlighter {
public:
    explicit TokenHighlighter(QTextDocument *document, CodeDocument *owner)
        : QSyntaxHighlighter(document), m_owner(owner) {}

protected:
    void highlightBlock(const QString &block) override
    {
        Q_UNUSED(block)
        for (const auto &span : m_owner->spansForBlock(currentBlock().blockNumber())) {
            const QColor color = m_owner->colorForKind(span.kind);
            if (!color.isValid()) continue;
            QTextCharFormat format;
            format.setForeground(color);
            setFormat(span.start, span.length, format);
        }
    }

private:
    CodeDocument *m_owner;
};

} // namespace

CodeDocument::CodeDocument(QObject *parent) : QObject(parent)
{
    watch(&m_storage);
}

CodeDocument::~CodeDocument()
{
    disconnect(m_changeConnection);
    delete m_highlighter;
}

QTextDocument *CodeDocument::active() const
{
    return m_view ? m_view.data() : const_cast<QTextDocument *>(&m_storage);
}

void CodeDocument::watch(QTextDocument *document)
{
    disconnect(m_changeConnection);
    m_lastText = document->toPlainText();
    m_changeConnection = connect(document, &QTextDocument::contentsChanged, this,
                                 [this, document] {
        if (m_rehighlighting) return;
        const QString current = document->toPlainText();
        if (current == m_lastText) return;
        m_lastText = current;
        if (!m_folds.isEmpty()) {
            m_folds.clear();
            for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
                if (!block.isVisible()) {
                    block.setVisible(true);
                    block.setLineCount(1);
                }
            }
            document->markContentsDirty(0, document->characterCount());
            emit foldsChanged();
        }
        ++m_revision;
        clearProviderResults();
        emit textChanged();
    });
}

QString CodeDocument::text() const { return active()->toPlainText(); }
int CodeDocument::lineCount() const { return active()->blockCount(); }
bool CodeDocument::modified() const { return active()->isModified(); }

void CodeDocument::setText(const QString &value)
{
    if (text() == value) return;
    active()->setPlainText(value);
}

void CodeDocument::clearProviderResults()
{
    if (!m_completions.isEmpty()) {
        m_completions.clear();
        emit completionsChanged();
    }
    m_completionRevision = -1;
    m_completionStart = m_completionEnd = -1;
    if (!m_tokens.isEmpty()) {
        m_tokens.clear();
        m_spansByBlock.clear();
        emit syntaxTokensChanged();
        refreshHighlighting();
    }
    if (!m_diagnostics.isEmpty()) {
        m_diagnostics.clear();
        emit diagnosticsChanged();
    }
}

void CodeDocument::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

void CodeDocument::attach(QQuickTextDocument *quickDocument)
{
    QTextDocument *document = quickDocument ? quickDocument->textDocument() : nullptr;
    if (!document || document == m_view) return;
    const QString previous = text();
    detach();
    if (!m_folds.isEmpty()) {
        m_folds.clear();
        emit foldsChanged();
    }
    disconnect(m_changeConnection);
    m_view = document;
    document->setPlainText(previous);
    document->setModified(m_storage.isModified());
    watch(document);
    delete m_highlighter;
    m_highlighter = new TokenHighlighter(document, this);
    m_viewDestroyConnection = connect(document, &QObject::destroyed, this, [this] {
        m_view = nullptr;
        m_highlighter = nullptr;
        watch(&m_storage);
    });
    emit textChanged();
}

void CodeDocument::detach()
{
    if (!m_view) return;
    QTextDocument *view = m_view.data();
    const QString current = view->toPlainText();
    const bool wasModified = view->isModified();
    disconnect(m_changeConnection);
    disconnect(m_viewDestroyConnection);
    delete m_highlighter;
    m_highlighter = nullptr;
    m_view = nullptr;
    m_folds.clear();
    emit foldsChanged();
    m_storage.setPlainText(current);
    m_storage.setModified(wasModified);
    watch(&m_storage);
}

int CodeDocument::visualLineCount() const
{
    int count = lineCount();
    for (const QVariant &entry : m_folds) {
        const QVariantMap fold = entry.toMap();
        count -= fold.value(QStringLiteral("end")).toInt()
                 - fold.value(QStringLiteral("start")).toInt();
    }
    return count;
}

int CodeDocument::lineForVisualRow(int row) const
{
    if (row < 0 || row >= visualLineCount()) return -1;
    int line = row + 1;
    for (const QVariant &entry : m_folds) {
        const QVariantMap fold = entry.toMap();
        const int first = fold.value(QStringLiteral("start")).toInt();
        if (line > first) line += fold.value(QStringLiteral("end")).toInt() - first;
    }
    return line;
}

bool CodeDocument::foldLines(int firstLine, int lastLine)
{
    if (firstLine < 1 || lastLine <= firstLine || lastLine > lineCount()) return false;
    for (const QVariant &entry : m_folds) {
        const QVariantMap fold = entry.toMap();
        if (firstLine <= fold.value(QStringLiteral("end")).toInt()
            && lastLine >= fold.value(QStringLiteral("start")).toInt()) return false;
    }
    for (int line = firstLine + 1; line <= lastLine; ++line) {
        QTextBlock block = active()->findBlockByNumber(line - 1);
        block.setVisible(false);
        block.setLineCount(0);
    }
    active()->markContentsDirty(active()->findBlockByNumber(firstLine - 1).position(),
                                active()->findBlockByNumber(lastLine - 1).position()
                                    - active()->findBlockByNumber(firstLine - 1).position() + 1);
    QVariantMap fold {{QStringLiteral("start"), firstLine}, {QStringLiteral("end"), lastLine}};
    m_folds.append(fold);
    std::sort(m_folds.begin(), m_folds.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("start")).toInt()
               < b.toMap().value(QStringLiteral("start")).toInt();
    });
    emit foldsChanged();
    return true;
}

bool CodeDocument::unfoldLines(int firstLine)
{
    for (int i = 0; i < m_folds.size(); ++i) {
        const QVariantMap fold = m_folds.at(i).toMap();
        if (fold.value(QStringLiteral("start")).toInt() != firstLine) continue;
        const int last = fold.value(QStringLiteral("end")).toInt();
        for (int line = firstLine + 1; line <= last; ++line) {
            QTextBlock block = active()->findBlockByNumber(line - 1);
            if (block.isValid()) {
                block.setVisible(true);
                block.setLineCount(1);
            }
        }
        m_folds.removeAt(i);
        active()->markContentsDirty(active()->findBlockByNumber(firstLine - 1).position(),
                                    active()->findBlockByNumber(last - 1).position()
                                        - active()->findBlockByNumber(firstLine - 1).position() + 1);
        emit foldsChanged();
        return true;
    }
    return false;
}

bool CodeDocument::replace(int start, int length, const QString &value)
{
    if (start < 0 || length < 0 || start + length > active()->characterCount() - 1) return false;
    QTextCursor cursor(active());
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    cursor.insertText(value);
    return true;
}

bool CodeDocument::undo()
{
    if (!active()->isUndoAvailable()) return false;
    active()->undo();
    return true;
}

bool CodeDocument::redo()
{
    if (!active()->isRedoAvailable()) return false;
    active()->redo();
    return true;
}

QVariantMap CodeDocument::findNext(const QString &needle, int from, bool caseSensitive) const
{
    if (needle.isEmpty()) return {};
    QTextCursor cursor(active());
    cursor.setPosition(qBound(0, from, active()->characterCount() - 1));
    const auto flags = caseSensitive ? QTextDocument::FindCaseSensitively : QTextDocument::FindFlags{};
    QTextCursor match = active()->find(needle, cursor, flags);
    if (match.isNull() && from > 0) match = active()->find(needle, 0, flags);
    if (match.isNull()) return {};
    return {{QStringLiteral("start"), match.selectionStart()},
            {QStringLiteral("end"), match.selectionEnd()}};
}

QVariantMap CodeDocument::lineColumn(int position) const
{
    const int bounded = qBound(0, position, active()->characterCount() - 1);
    const QTextBlock block = active()->findBlock(bounded);
    return {{QStringLiteral("line"), block.blockNumber() + 1},
            {QStringLiteral("column"), bounded - block.position() + 1}};
}

int CodeDocument::positionForLineColumn(int line, int column) const
{
    if (line < 1 || column < 1) return -1;
    const QTextBlock block = active()->findBlockByNumber(line - 1);
    if (!block.isValid()) return -1;
    int position = block.position() + qMin(column - 1, block.length() - 1);
    const QString contents = block.text();
    const int local = position - block.position();
    if (local > 0 && local < contents.size() && contents.at(local - 1).isHighSurrogate()
        && contents.at(local).isLowSurrogate()) --position;
    return position;
}

bool CodeDocument::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0')) {
        setError(QStringLiteral("Binary input is not supported"));
        return false;
    }
    setText(QString::fromUtf8(bytes));
    active()->setModified(false);
    m_filePath = path;
    emit filePathChanged();
    emit textChanged();
    setError({});
    return true;
}

bool CodeDocument::save(const QString &path)
{
    const QString destination = path.isEmpty() ? m_filePath : path;
    if (destination.isEmpty()) {
        setError(QStringLiteral("No file path"));
        return false;
    }
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(file.errorString());
        return false;
    }
    const QByteArray bytes = text().toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(file.errorString());
        return false;
    }
    active()->setModified(false);
    if (m_filePath != destination) {
        m_filePath = destination;
        emit filePathChanged();
    }
    emit textChanged();
    setError({});
    return true;
}

bool CodeDocument::publishSyntaxTokens(int sourceRevision, const QVariantList &tokens)
{
    if (sourceRevision != m_revision) return false;
    const int length = active()->characterCount() - 1;
    for (const QVariant &entry : tokens) {
        const QVariantMap token = entry.toMap();
        const int start = token.value(QStringLiteral("start"), -1).toInt();
        const int count = token.value(QStringLiteral("length"), -1).toInt();
        if (start < 0 || count <= 0 || start + count > length
            || token.value(QStringLiteral("kind")).toString().isEmpty()) return false;
    }
    m_spansByBlock.clear();
    for (const QVariant &entry : tokens) {
        const QVariantMap token = entry.toMap();
        const int start = token.value(QStringLiteral("start")).toInt();
        const int end = start + token.value(QStringLiteral("length")).toInt();
        const QString kind = token.value(QStringLiteral("kind")).toString();
        QTextBlock block = active()->findBlock(start);
        while (block.isValid() && block.position() < end) {
            const int fragmentStart = qMax(start, block.position());
            const int fragmentEnd = qMin(end, block.position() + block.length() - 1);
            if (fragmentEnd > fragmentStart)
                m_spansByBlock[block.blockNumber()].append(
                    {fragmentStart - block.position(), fragmentEnd - fragmentStart, kind});
            block = block.next();
        }
    }
    m_tokens = tokens;
    emit syntaxTokensChanged();
    refreshHighlighting();
    return true;
}

bool CodeDocument::publishDiagnostics(int sourceRevision, const QVariantList &diagnostics)
{
    if (sourceRevision != m_revision) return false;
    const int length = active()->characterCount() - 1;
    QVariantList normalized;
    for (const QVariant &entry : diagnostics) {
        QVariantMap item = entry.toMap();
        const int start = item.value(QStringLiteral("start"), -1).toInt();
        const int count = item.value(QStringLiteral("length"), 0).toInt();
        if (start < 0 || count < 0 || start + count > length) return false;
        item.insert(QStringLiteral("line"), lineColumn(start).value(QStringLiteral("line")));
        normalized.append(item);
    }
    m_diagnostics = normalized;
    emit diagnosticsChanged();
    return true;
}

bool CodeDocument::publishCompletions(int sourceRevision, int start, int end,
                                      const QVariantList &candidates)
{
    const QString source = text();
    if (sourceRevision != m_revision || start < 0 || end < start || end > source.size()
        || (start > 0 && start < source.size() && source.at(start - 1).isHighSurrogate()
            && source.at(start).isLowSurrogate())
        || (end > 0 && end < source.size() && source.at(end - 1).isHighSurrogate()
            && source.at(end).isLowSurrogate())) return false;
    for (const QVariant &entry : candidates) {
        const QVariantMap candidate = entry.toMap();
        if (candidate.value(QStringLiteral("label")).toString().isEmpty()
            || !candidate.contains(QStringLiteral("insertText"))) return false;
    }
    m_completionRevision = sourceRevision;
    m_completionStart = start;
    m_completionEnd = end;
    m_completions = candidates;
    emit completionsChanged();
    return true;
}

bool CodeDocument::applyCompletion(int index, int cursorPosition)
{
    if (m_completionRevision != m_revision || cursorPosition != m_completionEnd
        || index < 0 || index >= m_completions.size()) return false;
    const QString insertion = m_completions.at(index).toMap()
                                  .value(QStringLiteral("insertText")).toString();
    return replace(m_completionStart, m_completionEnd - m_completionStart, insertion);
}

QVariantList CodeDocument::editCursors(const QVariantList &positions, const QString &value,
                                        const QString &operation)
{
    if (positions.isEmpty() || (operation != QStringLiteral("insert")
                                && operation != QStringLiteral("backspace")
                                && operation != QStringLiteral("delete"))) return {};
    const QString source = text();
    QVector<QTextCursor> cursors;
    QVector<int> order;
    QVector<int> originalPositions;
    cursors.reserve(positions.size());
    order.reserve(positions.size());
    for (int i = 0; i < positions.size(); ++i) {
        bool ok = false;
        const int position = positions.at(i).toInt(&ok);
        if (!ok || position < 0 || position > source.size()
            || (position > 0 && position < source.size()
                && source.at(position - 1).isHighSurrogate()
                && source.at(position).isLowSurrogate())) return {};
        QTextCursor cursor(active());
        cursor.setPosition(position);
        cursors.append(cursor);
        order.append(i);
        originalPositions.append(position);
    }
    std::sort(order.begin(), order.end(), [&originalPositions](int a, int b) {
        return originalPositions[a] > originalPositions[b];
    });
    QTextCursor transaction(active());
    transaction.beginEditBlock();
    int lastPosition = -1;
    for (int index : order) {
        if (originalPositions[index] == lastPosition) continue;
        lastPosition = originalPositions[index];
        const int position = cursors[index].position();
        transaction.setPosition(position);
        if (operation == QStringLiteral("backspace")) transaction.deletePreviousChar();
        else if (operation == QStringLiteral("delete")) transaction.deleteChar();
        else transaction.insertText(value);
        cursors[index] = transaction;
    }
    transaction.endEditBlock();
    QVariantList result;
    for (const QTextCursor &cursor : std::as_const(cursors)) result.append(cursor.position());
    return result;
}

void CodeDocument::setTokenColors(const QVariantMap &colors)
{
    if (m_tokenColors == colors) return;
    m_tokenColors = colors;
    emit tokenColorsChanged();
    refreshHighlighting();
}

const QVector<CodeDocument::HighlightSpan> &CodeDocument::spansForBlock(int blockNumber) const
{
    static const QVector<HighlightSpan> empty;
    const auto it = m_spansByBlock.constFind(blockNumber);
    return it == m_spansByBlock.cend() ? empty : it.value();
}

QColor CodeDocument::colorForKind(const QString &kind) const
{
    return m_tokenColors.value(kind).value<QColor>();
}

void CodeDocument::refreshHighlighting()
{
    if (!m_highlighter) return;
    m_rehighlighting = true;
    m_highlighter->rehighlight();
    m_rehighlighting = false;
}

} // namespace QindaTK
