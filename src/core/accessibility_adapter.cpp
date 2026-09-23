// SPDX-License-Identifier: LGPL-3.0-or-later
#include "accessibility_adapter.h"
#include <QAccessibleObject>
#include <QQuickWindow>
#include <QTimer>
#include <QSet>
#include <cmath>
#include <utility>

namespace QindaTK {
namespace {
class SemanticInterface final : public QAccessibleInterface,
                                public QAccessibleActionInterface,
                                public QAccessibleValueInterface {
public:
    SemanticInterface(AccessibilityAdapter *adapter, QString id = {})
        : m_adapter(adapter), m_id(std::move(id)) {}
    bool isValid() const override { return m_adapter && (m_id.isEmpty() || node()); }
    QObject *object() const override { return m_id.isEmpty() ? m_adapter.data() : nullptr; }
    QWindow *window() const override { return m_adapter ? m_adapter->window() : nullptr; }
    QAccessibleInterface *parent() const override {
        if (!m_adapter) return nullptr;
        if (!m_id.isEmpty()) return QAccessible::queryAccessibleInterface(m_adapter);
        auto *item = m_adapter->parentItem();
        while (item) {
            auto *iface = QAccessible::queryAccessibleInterface(item);
            if (iface && iface->indexOfChild(this) >= 0) return iface;
            item = item->parentItem();
        }
        return window() ? QAccessible::queryAccessibleInterface(window()) : nullptr;
    }
    int childCount() const override { return isValid() && m_id.isEmpty() ? m_adapter->nodeIds().size() : 0; }
    QAccessibleInterface *child(int index) const override {
        if (index < 0 || index >= childCount()) return nullptr;
        return m_adapter->interfaceFor(m_adapter->nodeIds().at(index));
    }
    int indexOfChild(const QAccessibleInterface *iface) const override {
        auto *other = dynamic_cast<const SemanticInterface *>(iface);
        return other && other->m_adapter == m_adapter && m_id.isEmpty()
            ? m_adapter->nodeIds().indexOf(other->m_id) : -1;
    }
    QAccessibleInterface *childAt(int x, int y) const override {
        for (int i = childCount() - 1; i >= 0; --i) {
            auto *candidate = child(i);
            if (!candidate->state().offscreen && candidate->rect().contains(x, y)) return candidate;
        }
        return nullptr;
    }
    QAccessibleInterface *focusChild() const override {
        for (int i = 0; i < childCount(); ++i) if (child(i)->state().focused) return child(i);
        return nullptr;
    }
    QString text(QAccessible::Text type) const override {
        if (!isValid()) return {};
        if (m_id.isEmpty()) return type == QAccessible::Name ? m_adapter->accessibleName() : QString{};
        if (type == QAccessible::Name) return node()->name;
        if (type == QAccessible::Description) return node()->description;
        if (type == QAccessible::Value) return node()->value.toString();
        if (type == QAccessible::Identifier) return m_id;
        return {};
    }
    void setText(QAccessible::Text type, const QString &value) override {
        if (type == QAccessible::Value) setCurrentValue(value);
    }
    QRect rect() const override {
        if (!isValid()) return {};
        const QRectF local = node() ? node()->bounds : m_adapter->boundingRect();
        const QPointF origin = m_adapter->mapToGlobal(local.topLeft());
        const QPointF end = m_adapter->mapToGlobal(local.bottomRight());
        return QRectF(origin, end).normalized().toAlignedRect();
    }
    QAccessible::Role role() const override { return node() ? node()->role : QAccessible::Pane; }
    QAccessible::State state() const override {
        QAccessible::State result;
        if (!isValid()) { result.invalid = true; return result; }
        result.disabled = !m_adapter->isEnabled() || (node() && !node()->enabled);
        result.invisible = !m_adapter->isVisible();
        if (const auto *n = node()) {
            result.selectable = true;
            result.selected = m_adapter->selection() ? m_adapter->selection()->selectedIds().contains(m_id) : n->selected;
            result.focusable = n->commands.contains(QAccessibleActionInterface::setFocusAction());
            result.focused = m_adapter->selection() ? m_adapter->selection()->currentId() == m_id : n->focused;
            result.offscreen = n->offscreen;
            result.readOnly = n->readOnly;
        }
        return result;
    }
    void *interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::ActionInterface && node()) return static_cast<QAccessibleActionInterface *>(this);
        if (type == QAccessible::ValueInterface && node() && node()->value.isValid()) return static_cast<QAccessibleValueInterface *>(this);
        return nullptr;
    }
    QStringList actionNames() const override {
        QStringList names;
        if (!node() || state().disabled || !m_adapter->commands()) return names;
        for (auto it = node()->commands.cbegin(); it != node()->commands.cend(); ++it) {
            const auto command = m_adapter->commands()->resolve(it.value());
            if (command.enabled && command.visible
                && !(it.key() == QStringLiteral("setValue") && node()->readOnly)) names.append(it.key());
        }
        return names;
    }
    void doAction(const QString &action) override { if (m_adapter) m_adapter->invoke(m_id, action); }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }
    QVariant currentValue() const override { return node() ? node()->value : QVariant{}; }
    QVariant maximumValue() const override { return node() ? node()->maximum : QVariant{}; }
    QVariant minimumValue() const override { return node() ? node()->minimum : QVariant{}; }
    QVariant minimumStepSize() const override { return node() ? node()->step : QVariant{}; }
    void setCurrentValue(const QVariant &value) override {
        if (m_adapter) m_adapter->invoke(m_id, QStringLiteral("setValue"), value);
    }
private:
    const AccessibleNode *node() const { return m_adapter ? m_adapter->node(m_id) : nullptr; }
    QPointer<AccessibilityAdapter> m_adapter;
    QString m_id;
};
QAccessibleInterface *factory(const QString &, QObject *object)
{
    if (auto *adapter = qobject_cast<AccessibilityAdapter *>(object)) return new SemanticInterface(adapter);
    return nullptr;
}
void sendAccessibleEvent(QAccessibleInterface *iface, QAccessible::Event type)
{
    if (!iface) return;
    QAccessibleEvent changed(iface, type);
    QAccessible::updateAccessibility(&changed);
}
}
AccessibilityAdapter::AccessibilityAdapter(QQuickItem *parent) : QQuickItem(parent)
{
    static const bool installed = [] { QAccessible::installFactory(factory); return true; }();
    Q_UNUSED(installed);
}
AccessibilityAdapter::~AccessibilityAdapter()
{
    for (auto id : std::as_const(m_interfaces)) QAccessible::deleteAccessibleInterface(id);
}
void AccessibilityAdapter::setAccessibleName(const QString &name)
{
    if (m_name == name) return;
    m_name = name;
    emit semanticsChanged();
    sendAccessibleEvent(QAccessible::queryAccessibleInterface(this), QAccessible::NameChanged);
}
void AccessibilityAdapter::setCommands(CommandRegistry *commands)
{
    if (m_commands == commands) return;
    m_commands = commands;
    emit commandsChanged();
}
void AccessibilityAdapter::setSelection(SelectionModel *selection)
{
    if (m_selection == selection) return;
    if (m_selection) disconnect(m_selection, nullptr, this, nullptr);
    m_selection = selection;
    const auto changed = [this] {
        sendAccessibleEvent(QAccessible::queryAccessibleInterface(this), QAccessible::SelectionWithin);
        for (const auto &id : m_order) {
            auto *iface = interfaceFor(id);
            QAccessible::State flags; flags.selected = true; flags.focused = true;
            QAccessibleStateChangeEvent changed(iface, flags);
            QAccessible::updateAccessibility(&changed);
            if (iface->state().focused) sendAccessibleEvent(iface, QAccessible::Focus);
        }
    };
    if (selection) {
        connect(selection, &SelectionModel::selectionChanged, this, changed);
        connect(selection, &SelectionModel::currentChanged, this, changed);
    }
    emit selectionChanged();
}
const AccessibleNode *AccessibilityAdapter::node(const QString &id) const
{
    auto it = m_nodes.constFind(id);
    return it == m_nodes.cend() ? nullptr : &it.value();
}
QAccessibleInterface *AccessibilityAdapter::interfaceFor(const QString &id) const
{
    if (!m_nodes.contains(id)) return nullptr;
    if (!m_interfaces.contains(id)) m_interfaces.insert(id,
        QAccessible::registerAccessibleInterface(new SemanticInterface(const_cast<AccessibilityAdapter *>(this), id)));
    return QAccessible::accessibleInterface(m_interfaces.value(id));
}
bool AccessibilityAdapter::replaceNodes(const QVector<AccessibleNode> &nodes)
{
    if (nodes.size() > 10000) return false;
    QHash<QString, AccessibleNode> next;
    QStringList order;
    for (const auto &n : nodes) {
        const auto &r = n.bounds;
        const QSet<QAccessible::Role> roles = {QAccessible::ListItem, QAccessible::Cell,
            QAccessible::Row, QAccessible::Graphic, QAccessible::Slider,
            QAccessible::StaticText, QAccessible::EditableText, QAccessible::Button,
            QAccessible::CheckBox, QAccessible::TreeItem, QAccessible::Grouping};
        if (n.id.isEmpty() || n.id.size() > 256 || n.name.size() > 4096
            || n.description.size() > 16384 || next.contains(n.id) || !roles.contains(n.role)
            || !std::isfinite(r.x()) || !std::isfinite(r.y())
            || !std::isfinite(r.width()) || !std::isfinite(r.height())
            || r.width() < 0 || r.height() < 0 || n.commands.size() > 32) return false;
        for (auto it = n.commands.cbegin(); it != n.commands.cend(); ++it)
            if (it.key().isEmpty() || it.key().size() > 256 || it.value().isEmpty()
                || it.value().size() > 256) return false;
        next.insert(n.id, n); order.append(n.id);
    }
    for (const auto &id : m_order) if (!next.contains(id) && m_interfaces.contains(id)) {
        sendAccessibleEvent(QAccessible::accessibleInterface(m_interfaces.value(id)), QAccessible::ObjectDestroyed);
        QAccessible::deleteAccessibleInterface(m_interfaces.take(id));
    }
    m_nodes = std::move(next); m_order = std::move(order);
    emit semanticsChanged();
    sendAccessibleEvent(QAccessible::queryAccessibleInterface(this), QAccessible::ObjectReorder);
    for (const auto &id : m_order) if (m_interfaces.contains(id)) {
        auto *iface = interfaceFor(id);
        sendAccessibleEvent(iface, QAccessible::NameChanged);
        sendAccessibleEvent(iface, QAccessible::DescriptionChanged);
        sendAccessibleEvent(iface, QAccessible::LocationChanged);
        QAccessible::State flags;
        flags.disabled = flags.selected = flags.focused = flags.offscreen = flags.readOnly = true;
        QAccessibleStateChangeEvent stateChanged(iface, flags);
        QAccessible::updateAccessibility(&stateChanged);
        QAccessibleValueChangeEvent valueChanged(iface, node(id)->value);
        QAccessible::updateAccessibility(&valueChanged);
    }
    return true;
}
bool AccessibilityAdapter::setNodes(const QVariantList &nodes)
{
    if (nodes.size() > 10000) return false;
    QVector<AccessibleNode> parsed;
    for (const auto &value : nodes) {
        const auto map = value.toMap();
        AccessibleNode n;
        n.id = map.value(QStringLiteral("id")).toString();
        n.name = map.value(QStringLiteral("name")).toString();
        n.description = map.value(QStringLiteral("description")).toString();
        n.role = static_cast<QAccessible::Role>(map.value(QStringLiteral("role"), QAccessible::ListItem).toInt());
        n.bounds = map.value(QStringLiteral("bounds")).toRectF();
        n.value = map.value(QStringLiteral("value"));
        n.minimum = map.value(QStringLiteral("minimum")); n.maximum = map.value(QStringLiteral("maximum"));
        n.step = map.value(QStringLiteral("step"));
        n.enabled = map.value(QStringLiteral("enabled"), true).toBool();
        n.selected = map.value(QStringLiteral("selected"), false).toBool();
        n.focused = map.value(QStringLiteral("focused"), false).toBool();
        n.offscreen = map.value(QStringLiteral("offscreen"), false).toBool();
        n.readOnly = map.value(QStringLiteral("readOnly"), true).toBool();
        const auto commands = map.value(QStringLiteral("commands")).toMap();
        for (auto it = commands.cbegin(); it != commands.cend(); ++it) n.commands.insert(it.key(), it.value().toString());
        parsed.append(n);
    }
    return replaceNodes(parsed);
}
bool AccessibilityAdapter::invoke(const QString &id, const QString &action, const QVariant &value)
{
    const auto *n = node(id);
    QString reason;
    if (!n) reason = tr("Accessible object no longer exists");
    else if (!isEnabled() || !n->enabled) reason = tr("Accessible object is disabled");
    else if (action == QStringLiteral("setValue") && n->readOnly) reason = tr("Accessible value is read only");
    else if (!m_commands || !n->commands.contains(action)) reason = tr("Accessible action is unavailable");
    else {
        const QString command = n->commands.value(action);
        const auto error = m_commands->invoke(command, {{QStringLiteral("semanticId"), id},
             {QStringLiteral("action"), action}, {QStringLiteral("value"), value}});
        if (!error) return true;
        reason = error.message;
    }
    emit actionRejected(id, reason);
    return false;
}
void AccessibilityAdapter::announce(const QString &message)
{
    if (message.isEmpty()) return;
    // Bounded FIFO, coalescing adjacent duplicates, delivered on the UI thread.
    if (m_announcements.isEmpty() || m_announcements.last() != message) {
        if (m_announcements.size() == 32) m_announcements.removeFirst();
        m_announcements.append(message);
    }
    if (m_announcementPending) return;
    m_announcementPending = true;
    QTimer::singleShot(0, this, [this] {
        const auto messages = std::exchange(m_announcements, {});
        m_announcementPending = false;
        for (const auto &message : messages) {
            QAccessibleAnnouncementEvent announcement(this, message);
            QAccessible::updateAccessibility(&announcement);
            emit announcementDelivered(message);
        }
    });
}
} // namespace QindaTK
