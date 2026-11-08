// SPDX-License-Identifier: LGPL-3.0-or-later
// D-361: every Tk.MenuBar is published to the desktop global menu.
// Runs under dbus-run-session (a private bus) with a fake QindaQt registrar,
// so it never talks to a live desktop.
#include "global_menu_export.h"
#include "global_menu_projection_p.h"
#include "global_menu_server_p.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace QindaTK;
using GlobalMenu::Node;

namespace {

// The QindaQt registrar surface QindaTK uses (container-wm registrar_wire.h).
class FakeRegistrar : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.AppMenu.Registrar")
public:
    QSet<QString> hosted;   // "service|path"
public slots:
    Q_SCRIPTABLE bool IsMenuHosted(const QString &provider, const QDBusObjectPath &path)
    {
        return hosted.contains(provider + QLatin1Char('|') + path.path());
    }
    Q_SCRIPTABLE void RegisterWindow(uint, const QDBusObjectPath &) {}
    Q_SCRIPTABLE void UnregisterWindow(uint) {}
signals:
    Q_SCRIPTABLE void MenuHostedChanged(const QString &provider, const QDBusObjectPath &path, bool hosted);
public:
    void setHosted(const QString &provider, const QString &path, bool on)
    {
        const QString key = provider + QLatin1Char('|') + path;
        if (on)
            hosted.insert(key);
        else
            hosted.remove(key);
        emit MenuHostedChanged(provider, QDBusObjectPath(path), on);
    }
};

class AppShellStandIn : public QObject {
    Q_OBJECT
};

const char *kScene = R"QML(
import QtQuick
import QindaTK as Tk
Tk.AppWindow {
    id: win
    width: 640; height: 360
    visible: false
    property string last: ""
    property bool showExtra: false
    property alias fileMenu: fileMenu
    property alias extra: extra
    menuBar: Tk.MenuBar {
        objectName: "bar"
        Tk.Menu {
            id: fileMenu
            title: "&File"
            Tk.MenuItem { objectName: "open"; text: "&Open…"; shortcut: "Ctrl+O"; onTriggered: win.last = "open" }
            Tk.MenuSeparator { }
            Tk.MenuSeparator { }
            Tk.Menu {
                title: "Open &Recent"
                Tk.MenuItem { text: "a_b.txt"; onTriggered: win.last = "recent" }
            }
            Tk.MenuItem { id: extra; objectName: "extra"; text: "Extra"; visible: win.showExtra }
            Tk.MenuItem { text: "Gone"; enabled: false; onTriggered: win.last = "disabled" }
            Tk.MenuItem { text: "" }
            Tk.MenuSeparator { }
        }
        Tk.Menu {
            title: "&View"
            Tk.MenuItem { objectName: "grid"; text: "Grid"; checkable: true; checked: true }
            Tk.MenuItem { text: "Small"; checkable: true; radio: true; checked: true }
            Tk.MenuItem { text: "Delete"; shortcut: "Del" }
        }
        Tk.Menu { title: "" }
    }
}
)QML";

QList<Node> projectionOf(QObject *bar)
{
    GlobalMenu::Projection projection(bar);
    return projection.snapshot();
}

} // namespace

class TestGlobalMenu : public QObject {
    Q_OBJECT
private:
    QQmlEngine *m_engine = nullptr;
    FakeRegistrar *m_registrar = nullptr;

    QQuickWindow *createWindow()
    {
        QQmlComponent component(m_engine);
        component.setData(kScene, QUrl(QStringLiteral("qrc:/tst_global_menu.qml")));
        QObject *root = component.create();
        if (!root)
            qWarning() << component.errors();
        return qobject_cast<QQuickWindow *>(root);
    }
    QObject *barOf(QQuickWindow *w) { return w->property("menuBar").value<QObject *>(); }
    GlobalMenuExport *exportOf(QQuickWindow *w)
    {
        return qobject_cast<GlobalMenuExport *>(barOf(w)->property("globalMenu").value<QObject *>());
    }

private slots:
    void initTestCase()
    {
        m_engine = new QQmlEngine(this);
        m_engine->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    }

    void labelsAndShortcuts()
    {
        QCOMPARE(GlobalMenu::toDbusMenuLabel("&File"), QString("_File"));
        QCOMPARE(GlobalMenu::toDbusMenuLabel("Save && Quit"), QString("Save & Quit"));
        QCOMPARE(GlobalMenu::toDbusMenuLabel("snake_case"), QString("snake__case"));
        QCOMPARE(GlobalMenu::toDbusMenuLabel("&A&B"), QString("_AB"));
        QCOMPARE(GlobalMenu::portableShortcut("Ctrl+Shift+N"), QString("Ctrl+Shift+N"));
        QCOMPARE(GlobalMenu::portableShortcut("Del"), QString("Del"));
        QCOMPARE(GlobalMenu::portableShortcut("Alt+Return"), QString("Alt+Return"));
        QCOMPARE(GlobalMenu::toDbusMenuShortcut("Ctrl+Shift+N"),
                 (QList<QStringList> { { "Control", "Shift", "N" } }));
        QCOMPARE(GlobalMenu::toDbusMenuShortcut("Meta+K, Ctrl++"),
                 (QList<QStringList> { { "Super", "K" }, { "Control", "+" } }));
    }

    void projection()
    {
        std::unique_ptr<QQuickWindow> w(createWindow());
        QVERIFY(w);
        const QList<Node> roots = projectionOf(barOf(w.get()));
        QCOMPARE(roots.size(), 2);   // the untitled menu cannot be shown and is dropped
        const Node &file = roots.at(0);
        QCOMPARE(file.label, QString("_File"));
        QCOMPARE(file.kind, Node::Kind::Submenu);
        QStringList labels;
        for (const Node &n : file.children)
            labels << (n.kind == Node::Kind::Separator ? QStringLiteral("---") : n.label);
        // doubled and trailing separators collapse; the empty item is dropped
        QCOMPARE(labels, (QStringList { "_Open…", "---", "Open _Recent", "Extra", "Gone" }));
        QCOMPARE(file.children.at(0).shortcut, QString("Ctrl+O"));
        QCOMPARE(file.children.at(2).kind, Node::Kind::Submenu);
        QCOMPARE(file.children.at(2).children.at(0).label, QString("a__b.txt"));
        QVERIFY(!file.children.at(3).visible);   // app's visible:false, not the closed popup's
        QVERIFY(!file.children.at(4).enabled);
        const Node &view = roots.at(1);
        QVERIFY(view.children.at(0).checkable && view.children.at(0).checked && !view.children.at(0).radio);
        QVERIFY(view.children.at(1).radio);
        QCOMPARE(view.children.at(2).shortcut, QString("Del"));
    }

    void activationAndLiveUpdates()
    {
        std::unique_ptr<QQuickWindow> w(createWindow());
        QObject *bar = barOf(w.get());
        GlobalMenu::Projection projection(bar);
        QList<Node> roots = projection.snapshot();
        const QString openId = roots.at(0).children.at(0).id;
        QVERIFY(projection.activate(openId));
        QCOMPARE(w->property("last").toString(), QString("open"));
        QVERIFY(!projection.activate(roots.at(0).children.at(4).id));   // disabled
        QVERIFY(!projection.activate(QStringLiteral("nope")));
        const QString gridId = roots.at(1).children.at(0).id;
        QVERIFY(projection.activate(gridId));                            // toggles like a click
        QSignalSpy changed(&projection, &GlobalMenu::Projection::changed);
        w->setProperty("showExtra", true);
        QTRY_VERIFY(changed.count() > 0);
        roots = projection.snapshot();
        QVERIFY(roots.at(0).children.at(3).visible);
        QVERIFY(!roots.at(1).children.at(0).checked);
        QCOMPARE(roots.at(0).children.at(0).id, openId);                 // ids are stable
        // Menus added at run time appear.
        QObject *fileMenu = w->property("fileMenu").value<QObject *>();
        QQmlComponent item(m_engine);
        item.setData("import QindaTK as Tk\nTk.MenuItem { text: \"Late\" }", QUrl());
        QObject *late = item.create();
        QVERIFY(late);
        changed.clear();
        QMetaObject::invokeMethod(fileMenu, "addItem", Q_ARG(QQuickItem *, qobject_cast<QQuickItem *>(late)));
        QTRY_VERIFY(changed.count() > 0);
        bool found = false;
        for (const Node &n : projection.snapshot().at(0).children)
            found = found || n.label == QLatin1String("Late");
        QVERIFY(found);
    }

    void limitsKeepTheMenuAcceptable()
    {
        QString qml = QStringLiteral("import QtQuick\nimport QindaTK as Tk\nItem { property alias bar: bar\n"
                                     "Tk.MenuBar { id: bar\nTk.Menu { title: \"Big\"\nRepeater { model: 300\n"
                                     "delegate: Tk.MenuItem { required property int index; text: \"Item \" + index } } } } }");
        QQmlComponent component(m_engine);
        component.setData(qml.toUtf8(), QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY(root);
        QObject *bar = root->property("bar").value<QObject *>();
        const QList<Node> roots = projectionOf(bar);
        QCOMPARE(roots.size(), 1);
        QVERIFY(roots.at(0).children.size() <= GlobalMenu::kMaxChildren);
    }

    void serverEncoding()
    {
        std::unique_ptr<QQuickWindow> w(createWindow());
        GlobalMenu::Server server;
        server.publish(projectionOf(barOf(w.get())));
        uint revision = 0;
        GlobalMenu::LayoutItem layout;
        server.GetLayout(0, -1, {}, revision, layout);
        QCOMPARE(revision, 1u);
        QVERIFY(layout.properties.isEmpty());   // the panel expects a bare root
        const auto file = layout.children.at(0).value<GlobalMenu::LayoutItem>();
        QCOMPARE(file.properties.value("children-display").toString(), QString("submenu"));
        const auto open = file.children.at(0).value<GlobalMenu::LayoutItem>();
        QCOMPARE(open.properties.value("label").toString(), QString("_Open…"));
        QCOMPARE(open.properties.value("shortcut").value<GlobalMenu::ShortcutList>(),
                 (GlobalMenu::ShortcutList { { "Control", "O" } }));
        const auto view = layout.children.at(1).value<GlobalMenu::LayoutItem>();
        const auto small = view.children.at(1).value<GlobalMenu::LayoutItem>();
        QCOMPARE(small.properties.value("toggle-type").toString(), QString("radio"));
        QCOMPARE(small.properties.value("toggle-state").toInt(), 1);
        // Republishing an identical tree is not a layout change.
        QSignalSpy updated(&server, &GlobalMenu::Server::LayoutUpdated);
        server.publish(projectionOf(barOf(w.get())));
        QCOMPARE(updated.count(), 0);
        QSignalSpy activated(&server, &GlobalMenu::Server::activated);
        server.Event(open.id, QStringLiteral("clicked"), QDBusVariant(QString()), 0);
        QCOMPARE(activated.count(), 1);
        server.Event(file.id, QStringLiteral("clicked"), QDBusVariant(QString()), 0);   // a submenu is not an action
        QCOMPARE(activated.count(), 1);
    }

    void noRegistrarKeepsTheBar()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("needs a (private) session bus");
        std::unique_ptr<QQuickWindow> w(createWindow());
        GlobalMenuExport *exporter = exportOf(w.get());
        QVERIFY(exporter);
        w->show();
        QTRY_COMPARE(exporter->status(), QString("waiting-for-registrar"));
        QVERIFY(!exporter->hosted());
        QVERIFY(qobject_cast<QQuickItem *>(barOf(w.get()))->implicitHeight() > 0);
    }

    void hostedHandshake()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("needs a (private) session bus");
        qputenv("QINDATK_GLOBAL_MENU_ASSUME_IDENTITY", "1");
        m_registrar = new FakeRegistrar;
        QVERIFY(bus.registerObject("/com/canonical/AppMenu/Registrar", m_registrar, QDBusConnection::ExportScriptableContents));
        QVERIFY(bus.registerService("com.canonical.AppMenu.Registrar"));
        std::unique_ptr<QQuickWindow> w(createWindow());
        GlobalMenuExport *exporter = exportOf(w.get());
        auto *bar = qobject_cast<QQuickItem *>(barOf(w.get()));
        const qreal barHeight = bar->implicitHeight();
        QVERIFY(barHeight > 0);
        w->show();
        QTRY_COMPARE(exporter->status(), QString("published"));
        const QString path = exporter->objectPath();
        QVERIFY(path.startsWith("/org/qindatk/GlobalMenu/"));
        // The menu is reachable over the bus exactly as the panel reads it.
        QDBusInterface menu(bus.baseService(), path, "com.canonical.dbusmenu", bus);
        QDBusMessage reply = menu.call("GetLayout", 0, -1, QStringList());
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        QCOMPARE(reply.arguments().size(), 2);
        QVERIFY(!exporter->hosted());
        QVERIFY(bar->implicitHeight() > 0);
        // The panel shows it: the in-window bar collapses.
        m_registrar->setHosted(bus.baseService(), path, true);
        QTRY_VERIFY(exporter->hosted());
        QTRY_COMPARE(bar->implicitHeight(), 0.0);
        QVERIFY(bar->property("desktopHosted").toBool());
        // A click in the panel runs the item's handler.
        const auto layout = qdbus_cast<GlobalMenu::LayoutItem>(reply.arguments().at(1));
        QVERIFY2(!layout.children.isEmpty(), "GetLayout returned an empty root");
        const QVariant fileVariant = layout.children.at(0);
        const auto file = fileVariant.canConvert<QDBusArgument>()
            ? qdbus_cast<GlobalMenu::LayoutItem>(fileVariant.value<QDBusArgument>())
            : fileVariant.value<GlobalMenu::LayoutItem>();
        QVERIFY2(!file.children.isEmpty(), qPrintable(QString("file menu decoded empty: %1").arg(fileVariant.typeName())));
        const QVariant openVariant = file.children.at(0);
        const int openId = (openVariant.canConvert<QDBusArgument>()
            ? qdbus_cast<GlobalMenu::LayoutItem>(openVariant.value<QDBusArgument>())
            : openVariant.value<GlobalMenu::LayoutItem>()).id;
        menu.call("Event", openId, QStringLiteral("clicked"), QVariant::fromValue(QDBusVariant(QString())), 0u);
        QTRY_COMPARE(w->property("last").toString(), QString("open"));
        // The panel goes away: the bar comes back.
        m_registrar->setHosted(bus.baseService(), path, false);
        QTRY_VERIFY(!exporter->hosted());
        QTRY_COMPARE(bar->implicitHeight(), barHeight);
        w.reset();
        QTRY_VERIFY(!bus.objectRegisteredAt(path));
    }

    void appShellAppsKeepTheirOwnExport()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("needs a (private) session bus");
        AppShellStandIn standIn;
        QVERIFY(bus.registerObject("/org/qindaqt/AppShell/Menu", &standIn));
        std::unique_ptr<QQuickWindow> w(createWindow());
        GlobalMenuExport *exporter = exportOf(w.get());
        w->show();
        QTRY_COMPARE(exporter->status(), QString("app-shell"));
        QVERIFY(!exporter->hosted());
        bus.unregisterObject("/org/qindaqt/AppShell/Menu");
    }

    void oneDesktopMenuPerWindow()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("needs a (private) session bus");
        std::unique_ptr<QQuickWindow> w(createWindow());
        QQmlComponent second(m_engine);
        second.setData("import QindaTK as Tk\nTk.MenuBar { Tk.Menu { title: \"Other\" } }", QUrl());
        std::unique_ptr<QObject> other(second.create());
        auto *otherBar = qobject_cast<QQuickItem *>(other.get());
        QVERIFY(otherBar);
        otherBar->setParentItem(w->contentItem());
        w->show();
        GlobalMenuExport *first = exportOf(w.get());
        auto *otherExport = qobject_cast<GlobalMenuExport *>(otherBar->property("globalMenu").value<QObject *>());
        QTRY_VERIFY(first->status() != QLatin1String("idle") && otherExport->status() != QLatin1String("idle"));
        QVERIFY(first->status() == QLatin1String("duplicate") || otherExport->status() == QLatin1String("duplicate"));
    }

    void cleanupTestCase()
    {
        delete m_registrar;
    }
};

QTEST_MAIN(TestGlobalMenu)
#include "tst_global_menu.moc"
