// SPDX-License-Identifier: LGPL-3.0-or-later
#include "focus_manager.h"

#include <QGuiApplication>
#include <QUuid>
#include <utility>

namespace QindaTK {

FocusManager::FocusManager(QObject *parent) : QObject(parent) {}

qsizetype FocusManager::indexOf(const QString &token) const
{
    for (qsizetype i = 0; i < m_frames.size(); ++i)
        if (m_frames[i].token == token) return i;
    return -1;
}

bool FocusManager::contains(const QString &token) const
{
    return !token.isEmpty() && indexOf(token) >= 0;
}

QString FocusManager::activeToken() const
{
    for (auto it = m_frames.crbegin(); it != m_frames.crend(); ++it)
        if (it->active && it->owner) return it->token;
    return {};
}

QQuickWindow *FocusManager::windowFor(const Frame &frame) const
{
    if (frame.window) return frame.window;
    if (frame.target) return frame.target->window();
    if (frame.savedWindow) return frame.savedWindow;
    return qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow());
}

QQuickItem *FocusManager::focusedItem(const Frame &frame) const
{
    if (auto *window = windowFor(frame)) return window->activeFocusItem();
    return nullptr;
}

QString FocusManager::enterSurface(QObject *owner, QQuickItem *target,
                                   QQuickWindow *window, const QString &parentToken)
{
    if (!owner || (!parentToken.isEmpty() && !contains(parentToken))) return {};
    Frame frame;
    frame.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    frame.parentToken = parentToken;
    frame.owner = owner;
    frame.target = target;
    frame.window = window;
    if (!frame.window && target) frame.window = target->window();
    if (!frame.window)
        frame.window = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow());
    frame.savedWindow = frame.window;
    if (frame.savedWindow) frame.savedItem = frame.savedWindow->activeFocusItem();
    const QString token = frame.token;
    m_frames.append(frame);
    connect(owner, &QObject::destroyed, this, [this, token] { closeSurface(token); });
    if (target && target != owner)
        connect(target, &QObject::destroyed, this,
                [this, token] { closeSurface(token); });
    emit changed();
    return token;
}

bool FocusManager::activate(const QString &token)
{
    const auto index = indexOf(token);
    if (index < 0 || !m_frames[index].owner) return false;
    auto &frame = m_frames[index];
    frame.active = true;
    if (frame.target && frame.target->isVisible() && frame.target->isEnabled())
        frame.target->forceActiveFocus(Qt::PopupFocusReason);
    emit changed();
    return true;
}

bool FocusManager::isWithinClosingTree(QQuickItem *item,
                                      const QVector<QString> &tokens) const
{
    if (!item) return false;
    for (const auto &token : tokens) {
        const auto index = indexOf(token);
        if (index < 0) continue;
        const auto *target = m_frames[index].target.data();
        if (!target) continue;
        for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem())
            if (ancestor == target) return true;
    }
    return false;
}

void FocusManager::restoreIfSafe(const Frame &frame,
                                 const QVector<QString> &closingTokens)
{
    auto *window = windowFor(frame);
    if (!window) return;
    auto *current = window->activeFocusItem();
    if (current && !isWithinClosingTree(current, closingTokens)) return;

    auto *saved = frame.savedItem.data();
    if (!saved || !saved->isVisible() || !saved->isEnabled()) return;
    // Never restore into a surface being removed, even when it is nested.
    for (auto *ancestor = saved; ancestor; ancestor = ancestor->parentItem())
        for (const auto &token : closingTokens) {
            const auto index = indexOf(token);
            if (index >= 0 && m_frames[index].target == ancestor) return;
        }
    if (saved->window() == window) saved->forceActiveFocus(Qt::PopupFocusReason);
}

bool FocusManager::deactivate(const QString &token)
{
    const auto index = indexOf(token);
    if (index < 0) return false;
    Frame frame = m_frames[index];
    if (!frame.active) return true;
    frame.active = false;
    m_frames[index].active = false;
    restoreIfSafe(frame, {token});
    emit changed();
    return true;
}

bool FocusManager::closeSurface(const QString &token)
{
    const auto root = indexOf(token);
    if (root < 0) return false;
    QVector<QString> closing{token};
    for (qsizetype cursor = 0; cursor < closing.size(); ++cursor)
        for (const auto &frame : std::as_const(m_frames))
            if (frame.parentToken == closing[cursor]) closing.append(frame.token);
    const Frame restoreFrame = m_frames[root];
    auto *current = focusedItem(restoreFrame);
    const bool currentInTree = isWithinClosingTree(current, closing);
    const bool currentCleared = current == nullptr;
    if (currentInTree || currentCleared)
        restoreIfSafe(restoreFrame, closing);
    for (qsizetype i = m_frames.size(); i > 0; --i) {
        if (!closing.contains(m_frames[i - 1].token)) continue;
        m_frames.removeAt(i - 1);
    }
    // The owner-destroyed path has no valid transient target. Only restore if
    // Qt has already cleared focus; otherwise a surviving explicit focus wins.
    for (const auto &closed : closing) emit surfaceClosed(closed);
    emit changed();
    return true;
}

} // namespace QindaTK
