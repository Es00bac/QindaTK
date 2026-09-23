// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_registry.h"
#include "shortcut_map.h"
#include <QThread>
#include <utility>

namespace QindaTK {
namespace {
CommandPresentation unavailable(const QString &id, const QString &reason)
{
    CommandPresentation result;
    result.descriptor.id = id;
    result.reason = reason;
    return result;
}
OperationError rejectedError(const QString &message, ErrorCode code = ErrorCode::Unavailable)
{
    return {code, message, {}, {}};
}
} // namespace

QVariantMap CommandPresentation::toQml() const
{
    return {{QStringLiteral("id"), descriptor.id},
            {QStringLiteral("label"), descriptor.label},
            {QStringLiteral("iconName"), descriptor.iconName},
            {QStringLiteral("section"), descriptor.section},
            {QStringLiteral("keywords"), descriptor.keywords},
            {QStringLiteral("shortcut"), descriptor.defaultShortcut},
            {QStringLiteral("checkable"), descriptor.checkable},
            {QStringLiteral("checked"), checked},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("visible"), visible},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("scopeToken"), scopeToken}};
}

void CommandRegistry::setContext(CommandContext *context)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_context == context) return;
    QObject::disconnect(m_contextChange);
    QObject::disconnect(m_scopeRemoved);
    QObject::disconnect(m_contextDestroyed);
    m_context = context;
    m_handlers.clear(); // tokens belonged to the previous context tree
    if (context) {
        m_contextChange = connect(context, &CommandContext::changed, this,
                                  [this] { invalidate(); });
        m_scopeRemoved = connect(context, &CommandContext::scopeRemoved, this,
                                 [this](const QString &token) {
            for (qsizetype i = m_handlers.size(); i > 0; --i)
                if (m_handlers[i - 1].scopeToken == token) m_handlers.removeAt(i - 1);
            invalidate();
        });
        m_contextDestroyed = connect(context, &QObject::destroyed, this, [this] {
            m_context = nullptr;
            m_handlers.clear();
            emit contextChanged();
            invalidate();
        });
    }
    emit contextChanged();
    invalidate();
}

const CommandDescriptor *CommandRegistry::descriptor(const QString &id) const
{
    for (const auto &entry : m_commands)
        if (entry.id == id) return &entry;
    return nullptr;
}

bool CommandRegistry::registerCommand(const CommandDescriptor &entry)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (entry.id.isEmpty() || entry.label.isEmpty()) return false;
    for (auto &existing : m_commands) {
        if (existing.id != entry.id) continue;
        if (existing == entry) return true;
        existing = entry;
        invalidate(entry.id);
        return true;
    }
    m_commands.append(entry);
    invalidate(entry.id);
    return true;
}

bool CommandRegistry::removeCommand(const QString &id)
{
    Q_ASSERT(thread() == QThread::currentThread());
    for (qsizetype i = 0; i < m_commands.size(); ++i) {
        if (m_commands[i].id != id) continue;
        m_commands.removeAt(i);
        for (qsizetype j = m_handlers.size(); j > 0; --j)
            if (m_handlers[j - 1].commandId == id) m_handlers.removeAt(j - 1);
        invalidate(id);
        return true;
    }
    return false;
}

bool CommandRegistry::registerHandler(const QString &scopeToken, const QString &commandId,
                                      QObject *owner, CommandInvokeFunction invoke,
                                      CommandStateFunction state)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_context || !m_context->contains(scopeToken) || !descriptor(commandId)
        || !owner || !invoke) return false;
    for (qsizetype i = m_handlers.size(); i > 0; --i)
        if (m_handlers[i - 1].scopeToken == scopeToken
            && m_handlers[i - 1].commandId == commandId) m_handlers.removeAt(i - 1);
    const quint64 serial = ++m_nextHandlerSerial;
    m_handlers.append({scopeToken, commandId, owner, std::move(invoke),
                       std::move(state), serial});
    connect(owner, &QObject::destroyed, this, [this, serial] {
        for (qsizetype i = m_handlers.size(); i > 0; --i) {
            if (m_handlers[i - 1].serial != serial) continue;
            m_handlers.removeAt(i - 1);
            invalidate();
            break;
        }
    });
    invalidate(commandId);
    return true;
}

bool CommandRegistry::removeHandler(const QString &scopeToken, const QString &commandId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    for (qsizetype i = 0; i < m_handlers.size(); ++i) {
        if (m_handlers[i].scopeToken != scopeToken
            || m_handlers[i].commandId != commandId) continue;
        m_handlers.removeAt(i);
        invalidate(commandId);
        return true;
    }
    return false;
}

void CommandRegistry::invalidate(const QString &commandId)
{
    Q_UNUSED(commandId)
    Q_ASSERT(thread() == QThread::currentThread());
    ++m_generation;
    emit changed();
}

const CommandRegistry::Handler *CommandRegistry::resolvedHandler(const QString &id) const
{
    if (!m_context) return nullptr;
    for (const auto &token : m_context->activePath())
        for (const auto &handler : m_handlers)
            if (handler.scopeToken == token && handler.commandId == id && handler.owner)
                return &handler;
    return nullptr;
}

CommandPresentation CommandRegistry::resolve(const QString &id) const
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto *entry = descriptor(id);
    if (!entry) return unavailable(id, QStringLiteral("Unknown command"));
    CommandPresentation result;
    result.descriptor = *entry;
    if (!m_context) {
        result.reason = QStringLiteral("No active command context");
        return result;
    }
    const auto *handler = resolvedHandler(id);
    if (!handler) {
        result.reason = QStringLiteral("No handler in the active context");
        return result;
    }
    result.scopeToken = handler->scopeToken;
    const CommandState state = handler->state ? handler->state(m_context->snapshot())
                                               : CommandState{};
    result.enabled = state.enabled;
    result.checked = entry->checkable && state.checked;
    result.visible = state.visible;
    result.reason = state.enabled ? QString{}
                   : state.disabledReason.isEmpty() ? QStringLiteral("Unavailable in this context")
                                                     : state.disabledReason;
    return result;
}

OperationError CommandRegistry::invoke(const QString &id, const QVariantMap &arguments)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto presentation = resolve(id);
    if (!presentation.enabled) {
        const auto error = rejectedError(presentation.reason);
        emit rejected(id, error.message);
        return error;
    }
    const auto *current = resolvedHandler(id);
    if (!current || current->scopeToken != presentation.scopeToken) {
        const auto error = rejectedError(QStringLiteral("Command target changed"),
                                         ErrorCode::StaleGeneration);
        emit rejected(id, error.message);
        return error;
    }
    const CommandInvokeFunction operation = current->invoke;
    auto contextSnapshot = m_context->snapshot();
    contextSnapshot.request.operationId = id;
    if (contextSnapshot.request.cancellation.isCancelled()) {
        const auto error = rejectedError(QStringLiteral("Command target closed"),
                                         ErrorCode::Cancelled);
        emit rejected(id, error.message);
        return error;
    }
    const OperationError result = operation(contextSnapshot, arguments);
    invalidate(id);
    if (result) {
        emit rejected(id, result.message);
        return result;
    }
    emit invoked(id, presentation.scopeToken);
    return {};
}

QVariantMap CommandRegistry::query(const QString &id) const
{
    return resolve(id).toQml();
}

QVariantList CommandRegistry::paletteCommands() const
{
    QVariantList result;
    for (const auto &entry : m_commands) {
        const auto row = resolve(entry.id);
        if (row.visible) result.append(row.toQml());
    }
    return result;
}

bool CommandRegistry::trigger(const QString &id)
{
    return !invoke(id);
}

void CommandBinding::setRegistry(CommandRegistry *registry)
{
    if (m_registry == registry) return;
    QObject::disconnect(m_changeConnection);
    QObject::disconnect(m_destroyConnection);
    m_registry = registry;
    if (registry) {
        m_changeConnection = connect(registry, &CommandRegistry::changed,
                                     this, [this] { refresh(); });
        m_destroyConnection = connect(registry, &QObject::destroyed, this, [this] {
            m_registry = nullptr;
            emit registryChanged();
            refresh();
        });
    }
    emit registryChanged();
    refresh();
}

QObject *CommandBinding::shortcutMap() const
{
    return m_shortcutMap;
}

void CommandBinding::setShortcutMap(QObject *object)
{
    auto *map = qobject_cast<ShortcutMap *>(object);
    if (object && !map) return;
    if (m_shortcutMap == map) return;
    QObject::disconnect(m_shortcutChange);
    QObject::disconnect(m_shortcutDestroyed);
    m_shortcutMap = map;
    if (map) {
        m_shortcutChange = connect(map, &ShortcutMap::changed, this,
                                  [this] { emit changed(); });
        m_shortcutDestroyed = connect(map, &QObject::destroyed, this, [this] {
            m_shortcutMap = nullptr;
            emit shortcutMapChanged();
            emit changed();
        });
    }
    emit shortcutMapChanged();
    emit changed();
}

QString CommandBinding::shortcut() const
{
    if (m_shortcutMap) {
        const auto *map = static_cast<ShortcutMap *>(m_shortcutMap.data());
        const QString display = map->displaySequence(m_commandId);
        if (map->hasBinding(m_commandId))
            return display;
    }
    return m_presentation.descriptor.defaultShortcut;
}

void CommandBinding::setCommandId(const QString &id)
{
    if (m_commandId == id) return;
    m_commandId = id;
    emit commandIdChanged();
    refresh();
}

void CommandBinding::refresh()
{
    const auto next = m_registry ? m_registry->resolve(m_commandId)
                                 : unavailable(m_commandId, QStringLiteral("No command registry"));
    m_presentation = next;
    emit changed();
}

bool CommandBinding::trigger()
{
    return m_registry && m_registry->trigger(m_commandId);
}

} // namespace QindaTK
