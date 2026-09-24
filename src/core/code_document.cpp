// SPDX-License-Identifier: LGPL-3.0-or-later
#include "code_document.h"

#include <QFile>
#include <QQuickTextDocument>
#include <QSaveFile>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>

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
        // Qt emits contentsChanged for highlighter formatting too. Compare
        // plain text so provider output never invalidates itself.
        if (current == m_lastText) return;
        m_lastText = current;
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
    m_storage.setPlainText(current);
    m_storage.setModified(wasModified);
    watch(&m_storage);
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
