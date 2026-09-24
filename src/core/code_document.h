// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QHash>
#include <QColor>
#include <QTextDocument>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QSyntaxHighlighter;

namespace QindaTK {

// AGENT-CONTRACT: CodeEditor attaches its native TextEdit document here. All
// mutations then pass through that same QTextDocument, retaining Qt's IME,
// selection, and undo behavior. Provider results carry this model's revision.
class CodeDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(int revision READ revision NOTIFY textChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY textChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY textChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QVariantList syntaxTokens READ syntaxTokens NOTIFY syntaxTokensChanged)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(QVariantList completions READ completions NOTIFY completionsChanged)
    Q_PROPERTY(int completionEnd READ completionEnd NOTIFY completionsChanged)
    Q_PROPERTY(QVariantList folds READ folds NOTIFY foldsChanged)
    Q_PROPERTY(int visualLineCount READ visualLineCount NOTIFY foldsChanged)
    Q_PROPERTY(QVariantMap tokenColors READ tokenColors WRITE setTokenColors NOTIFY tokenColorsChanged)
public:
    struct HighlightSpan { int start; int length; QString kind; };
    explicit CodeDocument(QObject *parent = nullptr);
    ~CodeDocument() override;

    QString text() const;
    void setText(const QString &value);
    int revision() const { return m_revision; }
    int lineCount() const;
    bool modified() const;
    QString filePath() const { return m_filePath; }
    QString errorString() const { return m_error; }
    QVariantList syntaxTokens() const { return m_tokens; }
    QVariantList diagnostics() const { return m_diagnostics; }
    QVariantList completions() const { return m_completions; }
    int completionEnd() const { return m_completionEnd; }
    QVariantList folds() const { return m_folds; }
    int visualLineCount() const;
    QVariantMap tokenColors() const { return m_tokenColors; }
    void setTokenColors(const QVariantMap &colors);
    const QVector<HighlightSpan> &spansForBlock(int blockNumber) const;
    QColor colorForKind(const QString &kind) const;

    Q_INVOKABLE void attach(QQuickTextDocument *quickDocument);
    Q_INVOKABLE void detach();
    Q_INVOKABLE bool replace(int start, int length, const QString &value);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QVariantMap findNext(const QString &needle, int from, bool caseSensitive = false) const;
    Q_INVOKABLE QVariantMap lineColumn(int position) const;
    Q_INVOKABLE int positionForLineColumn(int line, int column) const;
    Q_INVOKABLE bool load(const QString &path);
    Q_INVOKABLE bool save(const QString &path = {});
    Q_INVOKABLE bool publishSyntaxTokens(int sourceRevision, const QVariantList &tokens);
    Q_INVOKABLE bool publishDiagnostics(int sourceRevision, const QVariantList &diagnostics);
    Q_INVOKABLE bool publishCompletions(int sourceRevision, int start, int end,
                                        const QVariantList &candidates);
    Q_INVOKABLE bool applyCompletion(int index, int cursorPosition);
    Q_INVOKABLE QVariantList editCursors(const QVariantList &positions, const QString &value,
                                         const QString &operation = QStringLiteral("insert"));
    Q_INVOKABLE bool foldLines(int firstLine, int lastLine);
    Q_INVOKABLE bool unfoldLines(int firstLine);
    Q_INVOKABLE int lineForVisualRow(int row) const;

signals:
    void textChanged();
    void filePathChanged();
    void errorChanged();
    void syntaxTokensChanged();
    void diagnosticsChanged();
    void completionsChanged();
    void foldsChanged();
    void tokenColorsChanged();

private:
    QTextDocument *active() const;
    void watch(QTextDocument *document);
    void clearProviderResults();
    void setError(const QString &error);
    void refreshHighlighting();

    QTextDocument m_storage;
    QPointer<QTextDocument> m_view;
    QMetaObject::Connection m_changeConnection;
    QMetaObject::Connection m_viewDestroyConnection;
    QSyntaxHighlighter *m_highlighter = nullptr;
    int m_revision = 0;
    QString m_filePath;
    QString m_error;
    QVariantList m_tokens;
    QHash<int, QVector<HighlightSpan>> m_spansByBlock;
    QVariantList m_diagnostics;
    QVariantList m_completions;
    int m_completionRevision = -1;
    int m_completionStart = -1;
    int m_completionEnd = -1;
    QVariantList m_folds;
    QVariantMap m_tokenColors;
    QString m_lastText;
    bool m_rehighlighting = false;
};

} // namespace QindaTK
