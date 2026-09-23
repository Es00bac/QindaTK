// SPDX-License-Identifier: LGPL-3.0-or-later
#include "gesture_router.h"

#include <QThread>
#include <QUuid>
#include <QStringList>

namespace QindaTK {

GestureRouter::GestureRouter(QObject *parent) : QObject(parent) {}

GestureRouter::~GestureRouter()
{
    cancelAll();
}

void GestureRouter::setToolId(const QString &toolId)
{
    if (m_toolId == toolId) return;
    m_toolId = toolId;
    emit changed();
}

void GestureRouter::setTemporaryNavigation(bool enabled)
{
    if (m_temporaryNavigation == enabled) return;
    m_temporaryNavigation = enabled;
    emit changed();
}

QVariantList GestureRouter::activeGestures() const
{
    QVariantList result;
    result.reserve(m_sessions.size());
    for (const Session &session : m_sessions) {
        result.append(QVariantMap{{QStringLiteral("token"), session.token},
            {QStringLiteral("viewId"), session.viewId},
            {QStringLiteral("pointerId"), session.pointerId},
            {QStringLiteral("targetId"), session.targetId},
            {QStringLiteral("toolId"), session.toolId},
            {QStringLiteral("device"), int(session.device)},
            {QStringLiteral("intent"), int(session.intent)},
            {QStringLiteral("modifiers"), session.modifiers},
            {QStringLiteral("buttons"), session.buttons}});
    }
    return result;
}

bool GestureRouter::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}

bool GestureRouter::current(const Session &session) const
{
    return session.owner
        && m_viewGenerations.value(session.viewId) == session.viewGeneration
        && (session.intent != Edit || (session.edit && session.edit->active()));
}

QString GestureRouter::beginGesture(const QString &viewId,
                                    const QString &pointerId,
                                    const QString &targetId, Device device,
                                    Intent intent, QObject *owner,
                                    EditTransaction *edit,
                                    bool synthesizedDuplicate,
                                    int modifiers, int buttons)
{
    if (QThread::currentThread() != thread() || viewId.trimmed().isEmpty()
        || pointerId.trimmed().isEmpty() || targetId.trimmed().isEmpty()
        || !owner || owner->thread() != thread()
        || device < Mouse || device > Eraser || intent < Edit || intent > Zoom)
        return fail(QStringLiteral("Gesture context is invalid")), QString{};
    if (synthesizedDuplicate)
        return fail(QStringLiteral("Synthesized duplicate pointer input was ignored")), QString{};
    for (const Session &active : m_sessions)
        if (active.viewId == viewId)
            return fail(QStringLiteral("Another gesture owns this view")), QString{};

    const Intent effective = m_temporaryNavigation && intent == Edit ? Navigate : intent;
    if (effective == Edit && (!edit || edit->thread() != thread() || edit->active()))
        return fail(QStringLiteral("An inactive edit transaction is required")), QString{};
    if (effective == Edit && !edit->begin())
        return fail(edit->errorMessage()), QString{};

    Session session;
    session.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.viewId = viewId;
    session.pointerId = pointerId;
    session.targetId = targetId;
    session.toolId = m_toolId;
    session.device = device;
    session.intent = effective;
    session.modifiers = modifiers;
    session.buttons = buttons;
    session.owner = owner;
    session.edit = effective == Edit ? edit : nullptr;
    session.viewGeneration = m_viewGenerations.value(viewId);
    m_sessions.insert(session.token, session);
    connect(owner, &QObject::destroyed, this,
            [this, token = session.token] {
        cancelGesture(token, QStringLiteral("Gesture owner was destroyed"));
    });
    if (edit && effective == Edit) {
        connect(edit, &QObject::destroyed, this,
                [this, token = session.token] {
            cancelGesture(token, QStringLiteral("Edit transaction was destroyed"));
        });
    }
    m_lastError.clear();
    emit changed();
    emit gestureStarted(session.token, effective);
    return session.token;
}

bool GestureRouter::updateGesture(const QString &token, const QString &pointerId)
{
    const auto it = m_sessions.constFind(token);
    if (it == m_sessions.cend() || it->pointerId != pointerId)
        return fail(QStringLiteral("Unknown gesture or pointer"));
    if (!current(*it)) {
        cancelGesture(token, QStringLiteral("Gesture context became stale"));
        return false;
    }
    return true;
}

bool GestureRouter::completeGesture(const QString &token,
                                     const QString &pointerId)
{
    const auto it = m_sessions.constFind(token);
    if (it == m_sessions.cend() || it->pointerId != pointerId)
        return fail(QStringLiteral("Unknown gesture or pointer"));
    if (!current(*it)) {
        cancelGesture(token, QStringLiteral("Gesture context became stale"));
        return false;
    }
    const Session session = m_sessions.take(token); // one-shot before callbacks
    emit changed();
    if (session.edit) {
        const OperationError error = session.edit->commitEdit();
        if (error) {
            session.edit->cancel();
            emit gestureCanceled(token, error.message);
            return fail(error.message);
        }
    }
    m_lastError.clear();
    emit changed();
    emit gestureEnded(token);
    return true;
}

bool GestureRouter::cancelGesture(const QString &token, const QString &reason)
{
    if (!m_sessions.contains(token)) return false;
    const Session session = m_sessions.take(token);
    if (session.edit) session.edit->cancel();
    emit changed();
    emit gestureCanceled(token, reason);
    return true;
}

void GestureRouter::lostGrab(const QString &token)
{
    cancelGesture(token, QStringLiteral("Pointer grab was lost"));
}

void GestureRouter::invalidateView(const QString &viewId)
{
    if (viewId.isEmpty()) return;
    ++m_viewGenerations[viewId];
    QStringList tokens;
    for (const Session &session : m_sessions)
        if (session.viewId == viewId) tokens.append(session.token);
    for (const QString &token : tokens)
        cancelGesture(token, QStringLiteral("View was recycled or closed"));
}

void GestureRouter::invalidateTarget(const QString &targetId)
{
    if (targetId.isEmpty()) return;
    QStringList tokens;
    for (const Session &session : m_sessions)
        if (session.targetId == targetId) tokens.append(session.token);
    for (const QString &token : tokens)
        cancelGesture(token, QStringLiteral("Target was removed or recycled"));
}

void GestureRouter::cancelAll()
{
    for (const QString &token : m_sessions.keys())
        cancelGesture(token, QStringLiteral("Gesture router was reset"));
}

GestureRouter::Intent GestureRouter::wheelIntent(int modifiers) const
{
    if (m_temporaryNavigation) return Navigate;
    return modifiers & int(Qt::ControlModifier) ? Zoom : Scroll;
}

} // namespace QindaTK
