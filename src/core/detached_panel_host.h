// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "dock_model.h"
#include "window_host.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// AGENT-CONTRACT: DockModel owns placement and persisted geometry. This host
// owns native window lifetime and seats the SAME QQuickItem content in the
// active window. DockHost registers each declaration's contentItem once.
class DetachedPanelHost : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::DockModel *model READ model WRITE setModel NOTIFY configurationChanged)
    Q_PROPERTY(QindaTK::WindowHost *windowHost READ windowHost WRITE setWindowHost NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit DetachedPanelHost(QObject *parent = nullptr);
    ~DetachedPanelHost() override;
    DockModel *model() const { return m_model; }
    void setModel(DockModel *model);
    WindowHost *windowHost() const { return m_windowHost; }
    void setWindowHost(WindowHost *host);
    QVariantList windows() const;
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool registerContent(const QString &panelId, QQuickItem *content);
    Q_INVOKABLE void unregisterContent(const QString &panelId);
    Q_INVOKABLE bool detach(const QString &panelId);
    Q_INVOKABLE bool reattach(const QString &panelId);
    Q_INVOKABLE void synchronize();
    // DockHost calls this before constructing frames after a model change.
    Q_INVOKABLE void reclaimDocked();
    Q_INVOKABLE QString windowId(const QString &panelId) const;
signals:
    void configurationChanged();
    void changed();
    void panelDetached(const QString &panelId);
    void panelReattached(const QString &panelId);
private:
    struct Entry {
        QPointer<QQuickItem> content;
        QPointer<QQuickItem> home;
        QPointer<QQuickItem> focusItem;
        QString windowId;
        bool geometryScheduled = false;
    };
    void queueSync();
    void saveGeometry(const QString &panelId);
    void rememberFocus(Entry &entry);
    void restoreFocus(const Entry &entry);
    void reclaim(const QString &panelId);
    bool open(const QString &panelId);
    bool fail(const QString &message);
    QPointer<DockModel> m_model;
    QPointer<WindowHost> m_windowHost;
    QHash<QString, Entry> m_entries;
    QString m_lastError;
    bool m_syncScheduled = false;
    bool m_synchronizing = false;
};
} // namespace QindaTK
