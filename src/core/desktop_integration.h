// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Core desktop requests. OS recent registration and second-instance IPC need
// separately qualified platform adapters and are reported unsupported here.
class DesktopIntegration : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList recentItems READ recentItems NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit DesktopIntegration(QObject *parent = nullptr);
    QVariantList recentItems() const { return m_recent; }
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool registerRecent(const QUrl &url);
    Q_INVOKABLE bool openUrl(const QUrl &url);
    Q_INVOKABLE bool reveal(const QUrl &url);
    Q_INVOKABLE bool openWith(const QUrl &url, const QString &application = {});
    Q_INVOKABLE bool activate(QQuickWindow *window);
    Q_INVOKABLE QVariantMap capabilities() const;
signals:
    void changed();
    void openDocumentRequested(const QUrl &url);
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool fail(const QString &message);
    QVariantList m_recent;
    QString m_lastError;
};
} // namespace QindaTK
