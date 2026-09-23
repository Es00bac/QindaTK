// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "command_context.h"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVector>
#include <functional>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class ShortcutMap;

struct CommandDescriptor {
    QString id;
    QString label;
    QString iconName;
    QString section;
    QString keywords;
    QString defaultShortcut;
    bool checkable = false;
    friend bool operator==(const CommandDescriptor &,
                           const CommandDescriptor &) = default;
};

struct CommandState {
    bool enabled = true;
    bool checked = false;
    bool visible = true;
    QString disabledReason;
};

struct CommandPresentation {
    CommandDescriptor descriptor;
    QString scopeToken;
    bool enabled = false;
    bool checked = false;
    bool visible = true;
    QString reason;
    QVariantMap toQml() const;
    friend bool operator==(const CommandPresentation &,
                           const CommandPresentation &) = default;
};

using CommandStateFunction = std::function<CommandState(const CommandInvocationContext &)>;
using CommandInvokeFunction = std::function<OperationError(
    const CommandInvocationContext &, const QVariantMap &)>;

// Descriptors are global to an application; handlers are owned by live scope
// instances. One invocation resolves one most-specific handler and executes it
// once, regardless of whether the request came from menu, button or palette.
class CommandRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(CommandContext *context READ context WRITE setContext NOTIFY contextChanged)
    Q_PROPERTY(QString generation READ generation NOTIFY changed)
public:
    explicit CommandRegistry(QObject *parent = nullptr) : QObject(parent) {}
    CommandContext *context() const { return m_context; }
    void setContext(CommandContext *context);
    QString generation() const { return QString::number(m_generation); }

    bool registerCommand(const CommandDescriptor &descriptor);
    bool removeCommand(const QString &id);
    bool registerHandler(const QString &scopeToken, const QString &commandId,
                         QObject *owner, CommandInvokeFunction invoke,
                         CommandStateFunction state = {});
    bool removeHandler(const QString &scopeToken, const QString &commandId);
    void invalidate(const QString &commandId = {});
    CommandPresentation resolve(const QString &commandId) const;
    OperationError invoke(const QString &commandId, const QVariantMap &arguments = {});

    Q_INVOKABLE QVariantMap query(const QString &commandId) const;
    Q_INVOKABLE QVariantList paletteCommands() const;
    Q_INVOKABLE bool trigger(const QString &commandId);
signals:
    void contextChanged();
    void changed();
    void invoked(const QString &commandId, const QString &scopeToken);
    void rejected(const QString &commandId, const QString &reason);
private:
    struct Handler {
        QString scopeToken;
        QString commandId;
        QPointer<QObject> owner;
        CommandInvokeFunction invoke;
        CommandStateFunction state;
        quint64 serial = 0;
    };
    const CommandDescriptor *descriptor(const QString &id) const;
    const Handler *resolvedHandler(const QString &id) const;
    QPointer<CommandContext> m_context;
    QVector<CommandDescriptor> m_commands;
    QVector<Handler> m_handlers;
    quint64 m_generation = 0;
    quint64 m_nextHandlerSerial = 0;
    QMetaObject::Connection m_contextChange;
    QMetaObject::Connection m_scopeRemoved;
    QMetaObject::Connection m_contextDestroyed;
};

// QML-facing projection of one semantic action. Presentation controls bind to
// its live label, enabled/checked state and reason, then call trigger().
class CommandBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CommandRegistry *registry READ registry WRITE setRegistry NOTIFY registryChanged)
    Q_PROPERTY(QObject *shortcutMap READ shortcutMap WRITE setShortcutMap NOTIFY shortcutMapChanged)
    Q_PROPERTY(QString commandId READ commandId WRITE setCommandId NOTIFY commandIdChanged)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(bool checkable READ checkable NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
public:
    explicit CommandBinding(QObject *parent = nullptr) : QObject(parent) {}
    CommandRegistry *registry() const { return m_registry; }
    void setRegistry(CommandRegistry *registry);
    QObject *shortcutMap() const;
    void setShortcutMap(QObject *map);
    QString commandId() const { return m_commandId; }
    void setCommandId(const QString &commandId);
    QString label() const { return m_presentation.descriptor.label; }
    QString iconName() const { return m_presentation.descriptor.iconName; }
    QString shortcut() const;
    bool enabled() const { return m_presentation.enabled; }
    bool checked() const { return m_presentation.checked; }
    bool checkable() const { return m_presentation.descriptor.checkable; }
    QString reason() const { return m_presentation.reason; }
    Q_INVOKABLE bool trigger();
signals:
    void registryChanged();
    void shortcutMapChanged();
    void commandIdChanged();
    void changed();
private:
    void refresh();
    QPointer<CommandRegistry> m_registry;
    QPointer<QObject> m_shortcutMap;
    QString m_commandId;
    CommandPresentation m_presentation;
    QMetaObject::Connection m_changeConnection;
    QMetaObject::Connection m_destroyConnection;
    QMetaObject::Connection m_shortcutChange;
    QMetaObject::Connection m_shortcutDestroyed;
};

} // namespace QindaTK
