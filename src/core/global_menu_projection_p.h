// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
// Projection of a live Tk.MenuBar (a QtQuick.Templates MenuBar) into a
// plain menu tree for the desktop global menu (D-361, docs/native-integration.md).
// AGENT-CONTRACT: the tree is exactly what the QindaQt global-menu panel's
// dbusmenu decoder accepts (container-wm src/shell/global_menu/dbusmenu,
// dbusmenu_decoder.cpp): every action/submenu has a non-empty label, depth
// <= kMaxDepth, <= kMaxChildren per item, <= kMaxItems in total. One invalid
// item makes the panel refuse the whole menu, so the projection drops or
// truncates instead of emitting it.
// No D-Bus here: the projection is testable everywhere.
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>

namespace QindaTK::GlobalMenu {

inline constexpr int kMaxDepth = 6;
inline constexpr int kMaxChildren = 128;
inline constexpr int kMaxItems = 1000;     // the panel allows 1024 incl. the root
inline constexpr int kMaxLabelBytes = 500; // the panel allows 512

struct Node {
    enum class Kind { Action, Separator, Submenu };
    QString id;                // stable for the lifetime of the menu object
    Kind kind = Kind::Action;
    QString label;             // dbusmenu form: mnemonic marked with '_'
    QString shortcut;          // portable text, e.g. "Ctrl+Shift+N" (may be empty)
    bool enabled = true;
    bool visible = true;
    bool checkable = false;
    bool checked = false;
    bool radio = false;
    QList<Node> children;
};

// Deep equality of two trees (QList<Node>'s operator== cannot see a
// recursive struct's comparison).
bool sameTree(const QList<Node> &a, const QList<Node> &b);

// "&File" -> "_File", "&&" -> "&", "_" -> "__".
QString toDbusMenuLabel(const QString &qtText);
// "Ctrl+Shift+N, Ctrl+K" -> [["Control","Shift","N"],["Control","K"]].
QList<QStringList> toDbusMenuShortcut(const QString &portable);
// Display shortcut strings ("Ctrl+O", "Del", "Alt+Return") in portable form.
QString portableShortcut(const QString &display);

class Projection : public QObject {
    Q_OBJECT
public:
    explicit Projection(QObject *menuBar, QObject *parent = nullptr);

    // Rebuilds the tree from the live objects and (re)subscribes to their
    // change signals. The bar's own "»" overflow menu is replaced by the
    // menus it holds, so the desktop always sees the full menu set.
    QList<Node> snapshot();
    // Clicks the menu item behind `id` (runs its handlers and action).
    // false when unknown, gone or disabled.
    bool activate(const QString &id);

signals:
    void changed();            // debounced; re-take snapshot()

private slots:
    void scheduleChange();

private:
    QList<QObject *> logicalMenus() const;
    void visitMenu(QObject *menu, QList<Node> &out, int depth, int &total);
    bool visitItem(QObject *item, QList<Node> &out, int depth, int &total);
    QString idFor(QObject *object);
    void watch(QObject *object, const QStringList &properties);

    QPointer<QObject> m_bar;
    QHash<QObject *, QString> m_ids;
    QHash<QString, QPointer<QObject>> m_targets;
    QSet<QObject *> m_watched;
    QTimer m_debounce;
    quint64 m_nextId = 1;
};

} // namespace QindaTK::GlobalMenu
