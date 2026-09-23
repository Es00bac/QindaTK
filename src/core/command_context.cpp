// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_context.h"
#include <QThread>
#include <QVariantList>
#include <utility>

namespace QindaTK {

qsizetype CommandContext::indexOf(const QString &token) const
{
    for (qsizetype i = 0; i < m_frames.size(); ++i)
        if (m_frames[i].token == token) return i;
    return -1;
}

void CommandContext::publishChange()
{
    ++m_generation;
    emit changed();
}

QString CommandContext::pushScope(const QString &scopeId, QObject *owner,
                                  const QString &parentToken)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (scopeId.isEmpty() || !owner || (!parentToken.isEmpty() && !contains(parentToken)))
        return {};
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_frames.append({token, parentToken, scopeId, owner, {}, {}, false, false, false});
    connect(owner, &QObject::destroyed, this, [this, token] { removeScope(token); });
    if (m_activeToken.isEmpty()) m_activeToken = token;
    publishChange();
    return token;
}

bool CommandContext::contains(const QString &token) const
{
    return !token.isEmpty() && indexOf(token) >= 0;
}

bool CommandContext::activate(const QString &token)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!contains(token)) return false;
    if (m_activeToken == token) return true;
    m_activeToken = token;
    publishChange();
    return true;
}

bool CommandContext::removeScope(const QString &token)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto rootIndex = indexOf(token);
    if (rootIndex < 0) return false;
    const QString parent = m_frames[rootIndex].parentToken;
    QStringList removed{token};
    for (qsizetype cursor = 0; cursor < removed.size(); ++cursor)
        for (const auto &frame : std::as_const(m_frames))
            if (frame.parentToken == removed[cursor]) removed.append(frame.token);
    const bool activeRemoved = removed.contains(m_activeToken);
    for (qsizetype i = m_frames.size(); i > 0; --i) {
        if (!removed.contains(m_frames[i - 1].token)) continue;
        m_frames[i - 1].cancellation.cancel();
        m_frames.removeAt(i - 1);
    }
    if (activeRemoved)
        m_activeToken = contains(parent) ? parent
                      : m_frames.isEmpty() ? QString{} : m_frames.first().token;
    for (const auto &removedToken : removed) emit scopeRemoved(removedToken);
    publishChange();
    return true;
}

QString CommandContext::activeScopeId() const
{
    const auto i = indexOf(m_activeToken);
    return i >= 0 ? m_frames[i].scopeId : QString{};
}

QStringList CommandContext::activePath() const
{
    QStringList path;
    QString token = m_activeToken;
    while (!token.isEmpty()) {
        const auto i = indexOf(token);
        if (i < 0) break;
        path.append(token);
        token = m_frames[i].parentToken;
    }
    return path;
}

CommandInvocationContext CommandContext::snapshot() const
{
    CommandInvocationContext result;
    const auto path = activePath();
    for (auto it = path.crbegin(); it != path.crend(); ++it) {
        const auto &frame = m_frames[indexOf(*it)];
        if (frame.values.request.expected.documentId.isValid())
            result.request.expected = frame.values.request.expected;
        if (frame.hasFocus) result.focusedView = frame.values.focusedView;
        if (frame.hasSelection) result.selection = frame.values.selection;
        if (frame.hasTool) result.toolId = frame.values.toolId;
        result.focusScopeId = frame.scopeId;
        result.request.cancellation = frame.cancellation.token();
    }
    result.request.targets = result.selection;
    return result;
}

bool CommandContext::updateScope(const QString &token, const QVariantMap &values)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto i = indexOf(token);
    if (i < 0) return false;
    auto next = m_frames[i].values;
    bool hasFocus = m_frames[i].hasFocus;
    bool hasSelection = m_frames[i].hasSelection;
    bool hasTool = m_frames[i].hasTool;
    if (values.contains(QStringLiteral("snapshotHeader"))) {
        const QVariant input = values.value(QStringLiteral("snapshotHeader"));
        if (input.metaType().id() != QMetaType::QVariantMap) return false;
        const auto header = ContractCodec::snapshotHeaderFromQml(input.toMap());
        if (!header) return false;
        next.request.expected = *header;
    }
    if (values.contains(QStringLiteral("focusedViewId"))) {
        const QVariant input = values.value(QStringLiteral("focusedViewId"));
        if (input.metaType().id() != QMetaType::QString) return false;
        const QString text = input.toString();
        next.focusedView = text.isEmpty() ? EntityId{} : EntityId::parse(text);
        if (!text.isEmpty() && !next.focusedView.isValid()) return false;
        hasFocus = true;
    }
    if (values.contains(QStringLiteral("selectionIds"))) {
        const QVariant input = values.value(QStringLiteral("selectionIds"));
        if (input.metaType().id() != QMetaType::QStringList
            && input.metaType().id() != QMetaType::QVariantList) return false;
        QVector<EntityId> selection;
        for (const QVariant &item : input.toList()) {
            if (item.metaType().id() != QMetaType::QString) return false;
            const auto id = EntityId::parse(item.toString());
            if (!id.isValid()) return false;
            selection.append(id);
        }
        next.selection = selection;
        hasSelection = true;
    }
    if (values.contains(QStringLiteral("toolId"))) {
        const QVariant input = values.value(QStringLiteral("toolId"));
        if (input.metaType().id() != QMetaType::QString) return false;
        next.toolId = input.toString();
        hasTool = true;
    }
    m_frames[i].values = next;
    m_frames[i].hasFocus = hasFocus;
    m_frames[i].hasSelection = hasSelection;
    m_frames[i].hasTool = hasTool;
    publishChange();
    return true;
}

} // namespace QindaTK
