// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
// com.canonical.dbusmenu server for one exported menu bar (D-361).
// AGENT-NOTE: wire types and encoding follow the QindaQt global-menu stack
// (container-wm src/shell/global_menu/dbusmenu, LGPL-3.0-or-later) that the
// panel decodes; keep property names/types identical to that decoder.
// Built only with QINDATK_HAVE_DBUS.
#include "global_menu_projection_p.h"
#include <QDBusArgument>
#include <QDBusVariant>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace QindaTK::GlobalMenu {

struct LayoutItem {
    qint32 id = 0;
    QVariantMap properties;
    QVariantList children;
};
struct PropertyEntry {
    qint32 id = 0;
    QVariantMap properties;
};
struct RemovedPropertyEntry {
    qint32 id = 0;
    QStringList names;
};
struct EventEntry {
    qint32 id = 0;
    QString eventId;
    QDBusVariant data;
    quint32 timestamp = 0;
};
using PropertyEntryList = QList<PropertyEntry>;
using RemovedPropertyEntryList = QList<RemovedPropertyEntry>;
using EventEntryList = QList<EventEntry>;
using ShortcutList = QList<QStringList>;

QDBusArgument &operator<<(QDBusArgument &a, const LayoutItem &v);
const QDBusArgument &operator>>(const QDBusArgument &a, LayoutItem &v);
QDBusArgument &operator<<(QDBusArgument &a, const PropertyEntry &v);
const QDBusArgument &operator>>(const QDBusArgument &a, PropertyEntry &v);
QDBusArgument &operator<<(QDBusArgument &a, const RemovedPropertyEntry &v);
const QDBusArgument &operator>>(const QDBusArgument &a, RemovedPropertyEntry &v);
QDBusArgument &operator<<(QDBusArgument &a, const EventEntry &v);
const QDBusArgument &operator>>(const QDBusArgument &a, EventEntry &v);
void registerWireTypes();

class Server : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version SCRIPTABLE true)
    Q_PROPERTY(QString TextDirection READ textDirection SCRIPTABLE true)
    Q_PROPERTY(QString Status READ status SCRIPTABLE true)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath SCRIPTABLE true)
public:
    explicit Server(QObject *parent = nullptr);
    uint version() const { return 4; }
    QString textDirection() const;
    QString status() const { return QStringLiteral("normal"); }
    QStringList iconThemePath() const { return {}; }
    // Replaces the published tree; emits LayoutUpdated only when it changed.
    void publish(const QList<Node> &roots);
    quint32 revision() const { return m_revision; }
    const LayoutItem &layout() const { return m_layout; }

public slots:
    Q_SCRIPTABLE void GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames,
                                uint &revision, QindaTK::GlobalMenu::LayoutItem &layout) const;
    Q_SCRIPTABLE QindaTK::GlobalMenu::PropertyEntryList GetGroupProperties(
        const QList<int> &ids, const QStringList &propertyNames) const;
    Q_SCRIPTABLE QDBusVariant GetProperty(int id, const QString &name) const;
    Q_SCRIPTABLE void Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp);
    Q_SCRIPTABLE QList<int> EventGroup(const QindaTK::GlobalMenu::EventEntryList &events);
    Q_SCRIPTABLE bool AboutToShow(int id) const;
    Q_SCRIPTABLE QList<int> AboutToShowGroup(const QList<int> &ids, QList<int> &idErrors) const;

signals:
    Q_SCRIPTABLE void LayoutUpdated(uint revision, int parent);
    Q_SCRIPTABLE void ItemsPropertiesUpdated(QindaTK::GlobalMenu::PropertyEntryList updatedProps,
                                             QindaTK::GlobalMenu::RemovedPropertyEntryList removedProps);
    Q_SCRIPTABLE void ItemActivationRequested(int id, uint timestamp);
    void activated(const QString &stableId);

private:
    LayoutItem encode(const Node &node);
    qint32 transportId(const QString &stableId);
    const LayoutItem *find(const LayoutItem &node, qint32 id) const;
    QList<Node> m_roots;
    LayoutItem m_layout;
    QHash<QString, qint32> m_ids;
    QHash<qint32, QString> m_actions;
    qint32 m_nextId = 1;
    quint32 m_revision = 0;
};

} // namespace QindaTK::GlobalMenu

Q_DECLARE_METATYPE(QindaTK::GlobalMenu::LayoutItem)
Q_DECLARE_METATYPE(QindaTK::GlobalMenu::PropertyEntry)
Q_DECLARE_METATYPE(QindaTK::GlobalMenu::RemovedPropertyEntry)
Q_DECLARE_METATYPE(QindaTK::GlobalMenu::EventEntry)
Q_DECLARE_METATYPE(QindaTK::GlobalMenu::PropertyEntryList)
Q_DECLARE_METATYPE(QindaTK::GlobalMenu::RemovedPropertyEntryList)
Q_DECLARE_METATYPE(QindaTK::GlobalMenu::EventEntryList)
Q_DECLARE_METATYPE(QindaTK::GlobalMenu::ShortcutList)
