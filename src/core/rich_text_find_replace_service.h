// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>

namespace QindaTK {

// FindReplaceBar adapter for the structured RichTextDocument. Search offsets
// are UTF-16 positions in RichTextArea's accessible-text projection;
// replacement uses paragraph identity and offsets so formatting survives.
class RichTextFindReplaceService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RichTextDocument *document READ document WRITE setDocument NOTIFY stateChanged)
    Q_PROPERTY(int revision READ revision NOTIFY stateChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY stateChanged)
    Q_PROPERTY(bool composing READ composing WRITE setComposing NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY stateChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY stateChanged)
    Q_PROPERTY(int currentMatch READ currentMatch NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit RichTextFindReplaceService(QObject *parent = nullptr);
    RichTextDocument *document() const { return m_document; }
    void setDocument(RichTextDocument *document);
    int revision() const;
    bool readOnly() const { return !m_document || m_document->readOnly(); }
    bool composing() const { return m_composing; }
    void setComposing(bool composing);
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
    struct Match {
        QUuid paragraph;
        int offset = 0;
        int rawLength = 0;
    };
    void invalidate();
    void replyLater(const QString &requestId);
    QVariantMap replace(bool all, int expectedRevision, const QString &replacement);
    QPointer<RichTextDocument> m_document;
    QVariantList m_results;
    QVector<Match> m_matches;
    QString m_activeRequest;
    QString m_lastError;
    QSharedPointer<std::atomic_bool> m_searchCancel;
    int m_currentMatch = -1;
    int m_resultsRevision = -1;
    quint64 m_nextRequest = 1;
    bool m_busy = false;
    bool m_composing = false;
};

} // namespace QindaTK
