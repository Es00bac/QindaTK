// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class DocumentManager;

// Native QQuickWindow ownership is separate from DocumentManager's document
// lifetime. An application can register an AppWindow or create a blank native
// window and attach its own QML content. Geometry uses Qt logical pixels.
class WindowHost : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::DocumentManager *documentManager READ documentManager WRITE setDocumentManager NOTIFY changed)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY changed)
    Q_PROPERTY(QString activeWindowId READ activeWindowId NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit WindowHost(QObject *parent = nullptr);
    ~WindowHost() override;
    DocumentManager *documentManager() const;
    void setDocumentManager(DocumentManager *manager);
    QVariantList windows() const;
    QString activeWindowId() const { return m_activeId; }
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE QString createWindow(const QString &documentId = {}, const QString &viewId = {},
                                     const QString &role = QStringLiteral("document"),
                                     QQuickWindow *transientParent = nullptr);
    Q_INVOKABLE QString registerWindow(QQuickWindow *window, const QString &documentId = {},
                                       const QString &viewId = {},
                                       const QString &role = QStringLiteral("document"));
    Q_INVOKABLE QQuickWindow *window(const QString &id) const;
    Q_INVOKABLE bool activate(const QString &id);
    Q_INVOKABLE bool closeWindow(const QString &id, bool discardUnsaved = false);
    Q_INVOKABLE bool moveWindow(const QString &id, const QRect &logicalGeometry,
                                const QString &screenId = {});
    Q_INVOKABLE void reconcileScreens();
    Q_INVOKABLE QVariantMap capabilities() const;
signals:
    void changed();
    void closeBlocked(const QString &id, const QString &reason);
    void windowClosed(const QString &id);
private:
    struct Record {
        QString id, documentId, viewId, role;
        QPointer<QQuickWindow> window;
        bool owned = false;
    };
    bool eventFilter(QObject *watched, QEvent *event) override;
    int indexOf(const QString &id) const;
    int indexOf(const QQuickWindow *window) const;
    bool mayClose(const Record &record, bool discardUnsaved);
    void releaseView(const Record &record, bool discardUnsaved = false);
    void forgetWindow(QQuickWindow *window);
    void updateActive();
    QList<Record> m_records;
    QPointer<DocumentManager> m_documents;
    QString m_activeId;
    QString m_lastError;
    bool m_explicitClose = false;
};
} // namespace QindaTK
