// SPDX-License-Identifier: LGPL-3.0-or-later
#include "global_menu_projection_p.h"
#include <QKeySequence>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQmlListReference>
#include <QQuickItem>
#include <private/qquickitem_p.h>

namespace QindaTK::GlobalMenu {

namespace {

bool inherits(const QObject *object, const char *className)
{
    for (const QMetaObject *mo = object ? object->metaObject() : nullptr; mo; mo = mo->superClass())
        if (qstrcmp(mo->className(), className) == 0)
            return true;
    return false;
}

bool isMenu(const QObject *o) { return inherits(o, "QQuickMenu"); }
bool isMenuItem(const QObject *o) { return inherits(o, "QQuickMenuItem"); }
bool isSeparator(const QObject *o) { return inherits(o, "QQuickMenuSeparator"); }

QObject *objectProperty(const QObject *object, const char *name)
{
    return object ? object->property(name).value<QObject *>() : nullptr;
}

// The app's own visible: setting, not the effective visibility (every item
// of a closed popup is effectively invisible).
bool explicitlyVisible(QObject *object)
{
    if (auto *item = qobject_cast<QQuickItem *>(object))
        return QQuickItemPrivate::get(item)->explicitVisible;
    return true;
}

QString boundedLabel(const QString &label)
{
    if (label.toUtf8().size() <= kMaxLabelBytes)
        return label;
    QString cut = label;
    while (cut.toUtf8().size() > kMaxLabelBytes - 3)
        cut.chop(1);
    return cut + QStringLiteral("…");
}

QString plainText(const QString &dbusLabel)
{
    QString out;
    for (qsizetype i = 0; i < dbusLabel.size(); ++i) {
        if (dbusLabel.at(i) == u'_' && i + 1 < dbusLabel.size() && dbusLabel.at(i + 1) == u'_')
            out.append(u'_'), ++i;
        else if (dbusLabel.at(i) != u'_')
            out.append(dbusLabel.at(i));
    }
    return out;
}

} // namespace

bool sameTree(const QList<Node> &a, const QList<Node> &b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        const Node &x = a.at(i), &y = b.at(i);
        if (x.id != y.id || x.kind != y.kind || x.label != y.label || x.shortcut != y.shortcut
            || x.enabled != y.enabled || x.visible != y.visible || x.checkable != y.checkable
            || x.checked != y.checked || x.radio != y.radio || !sameTree(x.children, y.children))
            return false;
    }
    return true;
}

QString toDbusMenuLabel(const QString &qtText)
{
    QString out;
    out.reserve(qtText.size() + 2);
    bool mnemonicDone = false;
    for (qsizetype i = 0; i < qtText.size(); ++i) {
        const QChar c = qtText.at(i);
        if (c == u'&') {
            if (i + 1 < qtText.size() && qtText.at(i + 1) == u'&') {
                out.append(u'&');
                ++i;
            } else if (i + 1 < qtText.size() && !mnemonicDone) {
                out.append(u'_');
                mnemonicDone = true;
            }
            continue;
        }
        if (c == u'_')
            out.append(QStringLiteral("__"));
        else
            out.append(c);
    }
    return out;
}

QString portableShortcut(const QString &display)
{
    const QString trimmed = display.trimmed();
    if (trimmed.isEmpty())
        return {};
    QKeySequence seq = QKeySequence::fromString(trimmed, QKeySequence::PortableText);
    if (seq.isEmpty())
        seq = QKeySequence::fromString(trimmed, QKeySequence::NativeText);
    return seq.isEmpty() ? QString() : seq.toString(QKeySequence::PortableText);
}

QList<QStringList> toDbusMenuShortcut(const QString &portable)
{
    QList<QStringList> sequences;
    for (const QString &sequence : portable.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
        QStringList tokens;
        // "Ctrl++" style keys: a trailing "+" after a '+' separator is the key itself.
        QString rest = sequence.trimmed();
        while (!rest.isEmpty()) {
            const qsizetype plus = rest.indexOf(u'+', 1);
            QString token = plus < 0 ? rest : rest.left(plus);
            rest = plus < 0 ? QString() : rest.mid(plus + 1);
            if (token == QLatin1String("Ctrl"))
                token = QStringLiteral("Control");
            else if (token == QLatin1String("Meta"))
                token = QStringLiteral("Super");
            tokens << token;
        }
        if (!tokens.isEmpty() && tokens.size() <= 5)
            sequences << tokens;
        if (sequences.size() == 4)
            break;
    }
    return sequences;
}

Projection::Projection(QObject *menuBar, QObject *parent)
    : QObject(parent)
    , m_bar(menuBar)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(0);
    connect(&m_debounce, &QTimer::timeout, this, &Projection::changed);
}

void Projection::scheduleChange()
{
    m_debounce.start();
}

QString Projection::idFor(QObject *object)
{
    auto it = m_ids.constFind(object);
    if (it != m_ids.cend())
        return *it;
    const QString id = QStringLiteral("m%1").arg(m_nextId++);
    m_ids.insert(object, id);
    connect(object, &QObject::destroyed, this, [this, object] {
        m_targets.remove(m_ids.take(object));
        m_watched.remove(object);
        scheduleChange();
    });
    return id;
}

void Projection::watch(QObject *object, const QStringList &properties)
{
    if (!object || m_watched.contains(object))
        return;
    m_watched.insert(object);
    const QMetaMethod slot = metaObject()->method(metaObject()->indexOfSlot("scheduleChange()"));
    const QMetaObject *mo = object->metaObject();
    for (const QString &name : properties) {
        const int index = mo->indexOfProperty(name.toLatin1().constData());
        if (index < 0)
            continue;
        const QMetaProperty property = mo->property(index);
        if (property.hasNotifySignal())
            connect(object, property.notifySignal(), this, slot);
    }
}

QList<QObject *> Projection::logicalMenus() const
{
    QList<QObject *> out;
    if (!m_bar)
        return out;
    QObject *overflow = objectProperty(m_bar, "overflowMenu");
    QQmlListReference menus(m_bar, "menus");
    for (qsizetype i = 0; menus.isValid() && i < menus.count(); ++i) {
        QObject *menu = menus.at(i);
        if (overflow && menu == overflow) {
            // Tk.MenuBar (D-315) parks trailing menus in "»" when narrow.
            for (const QVariant &moved : m_bar->property("overflowMenus").toList())
                if (QObject *m = moved.value<QObject *>())
                    out << m;
            continue;
        }
        if (menu)
            out << menu;
    }
    return out;
}

QList<Node> Projection::snapshot()
{
    QList<Node> roots;
    if (!m_bar)
        return roots;
    watch(m_bar, { QStringLiteral("count"), QStringLiteral("menus"), QStringLiteral("overflowMenus") });
    int total = 0;
    for (QObject *menu : logicalMenus()) {
        if (roots.size() >= kMaxChildren || total >= kMaxItems)
            break;
        watch(menu, { QStringLiteral("title"), QStringLiteral("count"), QStringLiteral("enabled") });
        const QString label = boundedLabel(toDbusMenuLabel(menu->property("title").toString()));
        if (plainText(label).trimmed().isEmpty())
            continue;   // an untitled menu cannot be shown in a menu bar
        Node node;
        node.id = idFor(menu);
        node.kind = Node::Kind::Submenu;
        node.label = label;
        node.enabled = menu->property("enabled").toBool();
        ++total;
        visitMenu(menu, node.children, 2, total);
        roots << node;
    }
    return roots;
}

void Projection::visitMenu(QObject *menu, QList<Node> &out, int depth, int &total)
{
    const int count = menu->property("count").toInt();
    for (int i = 0; i < count && out.size() < kMaxChildren && total < kMaxItems; ++i) {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
        if (item)
            visitItem(item, out, depth, total);
    }
    // No leading, trailing or doubled separators (hidden items included).
    auto shown = [](const Node &n) { return n.visible; };
    for (qsizetype i = 0; i < out.size();) {
        const bool sep = out[i].kind == Node::Kind::Separator;
        qsizetype prev = i - 1;
        while (prev >= 0 && !shown(out[prev])) --prev;
        const bool atStart = prev < 0 || out[prev].kind == Node::Kind::Separator;
        if (sep && atStart) {
            out.removeAt(i);
            continue;
        }
        ++i;
    }
    while (!out.isEmpty() && out.last().kind == Node::Kind::Separator)
        out.removeLast();
}

bool Projection::visitItem(QObject *item, QList<Node> &out, int depth, int &total)
{
    if (depth > kMaxDepth)
        return false;
    if (isSeparator(item)) {
        Node sep;
        sep.id = idFor(item);
        sep.kind = Node::Kind::Separator;
        sep.visible = explicitlyVisible(item);
        out << sep;
        ++total;
        return true;
    }
    if (!isMenuItem(item))
        return false;   // custom content inside a menu has no desktop form
    watch(item, { QStringLiteral("text"), QStringLiteral("enabled"), QStringLiteral("visible"),
                  QStringLiteral("checkable"), QStringLiteral("checked"), QStringLiteral("shortcut"),
                  QStringLiteral("subMenu"), QStringLiteral("radio"), QStringLiteral("action") });
    QObject *subMenu = objectProperty(item, "subMenu");
    QString text = item->property("text").toString();
    if (subMenu && text.isEmpty())
        text = subMenu->property("title").toString();
    const QString label = boundedLabel(toDbusMenuLabel(text));
    if (plainText(label).trimmed().isEmpty())
        return false;
    Node node;
    node.id = idFor(item);
    node.label = label;
    node.enabled = item->property("enabled").toBool();
    node.visible = explicitlyVisible(item);
    ++total;
    if (subMenu && isMenu(subMenu)) {
        watch(subMenu, { QStringLiteral("title"), QStringLiteral("count"), QStringLiteral("enabled") });
        node.kind = Node::Kind::Submenu;
        if (depth < kMaxDepth)
            visitMenu(subMenu, node.children, depth + 1, total);
        out << node;
        return true;
    }
    node.checkable = item->property("checkable").toBool();
    node.checked = node.checkable && item->property("checked").toBool();
    node.radio = node.checkable
        && (item->property("radio").toBool() || item->property("autoExclusive").toBool());
    QString shortcut = item->property("shortcut").toString();   // Tk.MenuItem display text
    if (shortcut.isEmpty())
        if (QObject *action = objectProperty(item, "action"))
            shortcut = action->property("shortcut").value<QKeySequence>().toString(QKeySequence::PortableText);
    node.shortcut = portableShortcut(shortcut);
    m_targets.insert(node.id, item);
    out << node;
    return true;
}

bool Projection::activate(const QString &id)
{
    QObject *target = m_targets.value(id);
    if (!target || !isMenuItem(target) || !target->property("enabled").toBool())
        return false;
    // AbstractButton::click() (Qt 6.8) toggles checkable items and emits
    // clicked -> MenuItem.triggered -> its action, exactly like a pointer click.
    return QMetaObject::invokeMethod(target, "click");
}

} // namespace QindaTK::GlobalMenu
