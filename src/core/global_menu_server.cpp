// SPDX-License-Identifier: LGPL-3.0-or-later
#include "global_menu_server_p.h"
#include <QDBusMetaType>
#include <QGuiApplication>

namespace QindaTK::GlobalMenu {

QDBusArgument &operator<<(QDBusArgument &a, const LayoutItem &v)
{
    a.beginStructure();
    a << v.id << v.properties << v.children;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, LayoutItem &v)
{
    a.beginStructure();
    a >> v.id >> v.properties >> v.children;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const PropertyEntry &v)
{
    a.beginStructure();
    a << v.id << v.properties;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, PropertyEntry &v)
{
    a.beginStructure();
    a >> v.id >> v.properties;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const RemovedPropertyEntry &v)
{
    a.beginStructure();
    a << v.id << v.names;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, RemovedPropertyEntry &v)
{
    a.beginStructure();
    a >> v.id >> v.names;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const EventEntry &v)
{
    a.beginStructure();
    a << v.id << v.eventId << v.data << v.timestamp;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, EventEntry &v)
{
    a.beginStructure();
    a >> v.id >> v.eventId >> v.data >> v.timestamp;
    a.endStructure();
    return a;
}

void registerWireTypes()
{
    static const bool done = [] {
        qRegisterMetaType<LayoutItem>("QindaTK::GlobalMenu::LayoutItem");
        qRegisterMetaType<PropertyEntryList>("QindaTK::GlobalMenu::PropertyEntryList");
        qRegisterMetaType<RemovedPropertyEntryList>("QindaTK::GlobalMenu::RemovedPropertyEntryList");
        qRegisterMetaType<EventEntryList>("QindaTK::GlobalMenu::EventEntryList");
        qDBusRegisterMetaType<LayoutItem>();
        qDBusRegisterMetaType<PropertyEntry>();
        qDBusRegisterMetaType<RemovedPropertyEntry>();
        qDBusRegisterMetaType<EventEntry>();
        qDBusRegisterMetaType<PropertyEntryList>();
        qDBusRegisterMetaType<RemovedPropertyEntryList>();
        qDBusRegisterMetaType<EventEntryList>();
        qDBusRegisterMetaType<ShortcutList>();
        return true;
    }();
    Q_UNUSED(done)
}

namespace {

QVariantMap selected(const QVariantMap &all, const QStringList &names)
{
    if (names.isEmpty())
        return all;
    QVariantMap out;
    for (const QString &n : names)
        if (const auto it = all.constFind(n); it != all.cend())
            out.insert(n, *it);
    return out;
}

LayoutItem bounded(const LayoutItem &source, int depth, const QStringList &names)
{
    LayoutItem out { source.id, selected(source.properties, names), {} };
    if (depth == 0)
        return out;
    for (const QVariant &child : source.children)
        out.children << QVariant::fromValue(bounded(child.value<LayoutItem>(), depth < 0 ? -1 : depth - 1, names));
    return out;
}

void collect(const LayoutItem &node, const QStringList &names, PropertyEntryList &out)
{
    out << PropertyEntry { node.id, selected(node.properties, names) };
    for (const QVariant &child : node.children)
        collect(child.value<LayoutItem>(), names, out);
}

QVariantMap propertiesFor(const Node &n)
{
    if (n.kind == Node::Kind::Separator) {
        QVariantMap p { { QStringLiteral("type"), QStringLiteral("separator") } };
        if (!n.visible)
            p.insert(QStringLiteral("visible"), false);
        return p;
    }
    QVariantMap p { { QStringLiteral("label"), n.label },
                    { QStringLiteral("enabled"), n.enabled },
                    { QStringLiteral("visible"), n.visible } };
    if (n.kind == Node::Kind::Submenu)
        p.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
    if (!n.shortcut.isEmpty()) {
        const ShortcutList shortcut = toDbusMenuShortcut(n.shortcut);
        if (!shortcut.isEmpty())
            p.insert(QStringLiteral("shortcut"), QVariant::fromValue(shortcut));
    }
    if (n.checkable) {
        p.insert(QStringLiteral("toggle-type"), n.radio ? QStringLiteral("radio") : QStringLiteral("checkmark"));
        p.insert(QStringLiteral("toggle-state"), n.checked ? 1 : 0);
    }
    return p;
}

} // namespace

Server::Server(QObject *parent)
    : QObject(parent)
{
    registerWireTypes();
}

QString Server::textDirection() const
{
    return QGuiApplication::layoutDirection() == Qt::RightToLeft ? QStringLiteral("rtl") : QStringLiteral("ltr");
}

qint32 Server::transportId(const QString &stableId)
{
    if (const auto it = m_ids.constFind(stableId); it != m_ids.cend())
        return *it;
    const qint32 id = m_nextId++;
    m_ids.insert(stableId, id);
    return id;
}

LayoutItem Server::encode(const Node &node)
{
    LayoutItem item { transportId(node.id), propertiesFor(node), {} };
    if (node.kind == Node::Kind::Action)
        m_actions.insert(item.id, node.id);
    for (const Node &child : node.children)
        item.children << QVariant::fromValue(encode(child));
    return item;
}

void Server::publish(const QList<Node> &roots)
{
    if (m_revision > 0 && sameTree(roots, m_roots))
        return;
    m_roots = roots;
    m_actions.clear();
    LayoutItem root { 0, {}, {} };   // the panel expects a bare root
    for (const Node &node : roots)
        root.children << QVariant::fromValue(encode(node));
    m_layout = root;
    ++m_revision;
    emit LayoutUpdated(m_revision, 0);
}

const LayoutItem *Server::find(const LayoutItem &node, qint32 id) const
{
    if (node.id == id)
        return &node;
    for (const QVariant &child : node.children) {
        // children hold copies; find through a stable reference into the variant
        const LayoutItem *item = static_cast<const LayoutItem *>(child.constData());
        if (const LayoutItem *hit = find(*item, id))
            return hit;
    }
    return nullptr;
}

void Server::GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames, uint &revision,
                       LayoutItem &layout) const
{
    revision = m_revision;
    const LayoutItem *parent = find(m_layout, parentId);
    layout = parent ? bounded(*parent, recursionDepth, propertyNames) : LayoutItem {};
}

PropertyEntryList Server::GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames) const
{
    PropertyEntryList out;
    if (ids.isEmpty()) {
        for (const QVariant &child : m_layout.children)
            collect(child.value<LayoutItem>(), propertyNames, out);
        return out;
    }
    for (int id : ids)
        if (const LayoutItem *item = find(m_layout, id))
            out << PropertyEntry { id, selected(item->properties, propertyNames) };
    return out;
}

QDBusVariant Server::GetProperty(int id, const QString &name) const
{
    const LayoutItem *item = find(m_layout, id);
    return QDBusVariant(item ? item->properties.value(name) : QVariant());
}

void Server::Event(int id, const QString &eventId, const QDBusVariant &, uint timestamp)
{
    if (eventId != QLatin1String("clicked"))
        return;
    const auto action = m_actions.constFind(id);
    if (action == m_actions.cend())
        return;
    emit ItemActivationRequested(id, timestamp);
    emit activated(*action);
}

QList<int> Server::EventGroup(const EventEntryList &events)
{
    QList<int> errors;
    for (const EventEntry &e : events) {
        if (!find(m_layout, e.id)) {
            errors << e.id;
            continue;
        }
        Event(e.id, e.eventId, e.data, e.timestamp);
    }
    return errors;
}

bool Server::AboutToShow(int) const { return false; }

QList<int> Server::AboutToShowGroup(const QList<int> &ids, QList<int> &idErrors) const
{
    idErrors.clear();
    for (int id : ids)
        if (!find(m_layout, id))
            idErrors << id;
    return {};
}

} // namespace QindaTK::GlobalMenu
