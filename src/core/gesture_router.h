// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "edit_transaction.h"
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// GUI-thread arbiter for pointer handlers and tool transactions. Qt handlers
// still own low-level grabs; this service owns one semantic gesture per view.
class GestureRouter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString toolId READ toolId WRITE setToolId NOTIFY changed)
    Q_PROPERTY(bool temporaryNavigation READ temporaryNavigation WRITE setTemporaryNavigation NOTIFY changed)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
    Q_PROPERTY(QVariantList activeGestures READ activeGestures NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    enum Device { Mouse, Touch, Pen, Eraser };
    Q_ENUM(Device)
    enum Intent { Edit, Navigate, Scroll, Zoom };
    Q_ENUM(Intent)

    explicit GestureRouter(QObject *parent = nullptr);
    ~GestureRouter() override;
    QString toolId() const { return m_toolId; }
    void setToolId(const QString &toolId);
    bool temporaryNavigation() const { return m_temporaryNavigation; }
    void setTemporaryNavigation(bool enabled);
    int activeCount() const { return m_sessions.size(); }
    QVariantList activeGestures() const;
    QString lastError() const { return m_lastError; }

    // EditTransaction must be configured but inactive. The router begins it
    // after winning the claim, then commits once or cancels on grab loss.
    Q_INVOKABLE QString beginGesture(const QString &viewId,
                                    const QString &pointerId,
                                    const QString &targetId, Device device,
                                    Intent intent, QObject *owner,
                                    EditTransaction *edit = nullptr,
                                    bool synthesizedDuplicate = false,
                                    int modifiers = 0, int buttons = 0);
    Q_INVOKABLE bool updateGesture(const QString &token,
                                   const QString &pointerId);
    Q_INVOKABLE bool completeGesture(const QString &token,
                                     const QString &pointerId);
    Q_INVOKABLE bool cancelGesture(const QString &token,
                                   const QString &reason = {});
    Q_INVOKABLE void lostGrab(const QString &token);
    Q_INVOKABLE void invalidateView(const QString &viewId);
    Q_INVOKABLE void invalidateTarget(const QString &targetId);
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE Intent wheelIntent(int modifiers) const;

signals:
    void changed();
    void gestureStarted(const QString &token, GestureRouter::Intent intent);
    void gestureEnded(const QString &token);
    void gestureCanceled(const QString &token, const QString &reason);

private:
    struct Session {
        QString token;
        QString viewId;
        QString pointerId;
        QString targetId;
        QString toolId;
        Device device = Mouse;
        Intent intent = Navigate;
        int modifiers = 0;
        int buttons = 0;
        quint64 viewGeneration = 0;
        QPointer<QObject> owner;
        QPointer<EditTransaction> edit;
    };
    bool current(const Session &session) const;
    bool fail(const QString &message);
    QHash<QString, Session> m_sessions;
    QHash<QString, quint64> m_viewGenerations;
    QString m_toolId;
    QString m_lastError;
    bool m_temporaryNavigation = false;
};

} // namespace QindaTK
