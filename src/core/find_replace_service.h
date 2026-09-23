// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSharedPointer>
#include <QTextDocument>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>

namespace QindaTK {

// Search adapter for a real Qt text document. Positions and scope lengths are
// UTF-16 QTextCursor offsets; the host owns the editor and document lifetime.
class FindReplaceService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY stateChanged)
    Q_PROPERTY(int revision READ revision NOTIFY stateChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY stateChanged)
    Q_PROPERTY(bool composing READ composing WRITE setComposing NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY stateChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY stateChanged)
    Q_PROPERTY(int currentMatch READ currentMatch NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit FindReplaceService(QObject *parent = nullptr);
    QQuickTextDocument *textDocument() const { return m_wrapper; }
    void setTextDocument(QQuickTextDocument *wrapper);
    int revision() const;
    bool readOnly() const { return m_readOnly; }
    void setReadOnly(bool value);
    bool composing() const { return m_composing; }
    void setComposing(bool value);
    bool busy() const { return m_busy; }
    QVariantList results() const { return m_results; }
    int matchCount() const { return m_results.size(); }
    int currentMatch() const { return m_currentMatch; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QString search(const QString &query, const QVariantMap &options = {},
                               const QVariantMap &scope = {});
    Q_INVOKABLE bool cancelSearch(const QString &requestId);
    Q_INVOKABLE bool setCurrentMatch(int index);
    Q_INVOKABLE QVariantMap replaceCurrent(int expectedRevision, const QString &replacement);
    Q_INVOKABLE QVariantMap replaceAll(int expectedRevision, const QString &replacement);

signals:
    void stateChanged();
    void resultsReady(const QString &requestId, int revision, const QVariantList &results,
                      int currentMatch, const QString &error);
    void operationFinished(const QString &requestId, const QVariantMap &result);

private:
    void invalidate();
    QVariantMap replace(bool all, int expectedRevision, const QString &replacement);
    QPointer<QQuickTextDocument> m_wrapper;
    QPointer<QTextDocument> m_document;
    QVariantList m_results;
    QString m_activeRequest;
    QString m_lastError;
    QSharedPointer<std::atomic_bool> m_searchCancel;
    int m_currentMatch = -1;
    int m_resultsRevision = -1;
    quint64 m_nextRequest = 1;
    bool m_busy = false;
    bool m_readOnly = false;
    bool m_composing = false;
};

} // namespace QindaTK
