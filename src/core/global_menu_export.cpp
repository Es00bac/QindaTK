// SPDX-License-Identifier: LGPL-3.0-or-later
#include "global_menu_export.h"
#include "global_menu_projection_p.h"
#include <QLoggingCategory>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <functional>
#include <optional>
#ifdef QINDATK_HAVE_DBUS
#include "global_menu_hosted_p.h"
#include "global_menu_server_p.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QXmlStreamReader>
#include <QtGui/private/qdesktopunixservices_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformintegration.h>
#include <QtGui/qpa/qplatformwindow_p.h>
#endif

namespace QindaTK {

// QT_LOGGING_RULES="qindatk.globalmenu.debug=true" traces every export step.
Q_LOGGING_CATEGORY(lcGlobalMenu, "qindatk.globalmenu", QtWarningMsg)

#ifdef QINDATK_HAVE_DBUS
namespace {

constexpr auto kRegistrarService = "com.canonical.AppMenu.Registrar";
constexpr auto kRegistrarPath = "/com/canonical/AppMenu/Registrar";
constexpr auto kRegistrarInterface = "com.canonical.AppMenu.Registrar";
// AGENT-CONTRACT: the QindaQt AppShell export path (container-wm
// src/app_shell/menu_export); a fast path of appOwnsDesktopMenu().
constexpr auto kAppShellMenuPath = "/org/qindaqt/AppShell/Menu";
constexpr auto kDbusMenuInterface = "com.canonical.dbusmenu";
constexpr auto kWindowClaim = "_qindatk_globalMenuOwner";
int g_pathSerial = 0;

QString s(const char *latin) { return QString::fromLatin1(latin); }

// True when this process already serves com.canonical.dbusmenu at any path
// other than QindaTK's own exports: the application publishes its desktop
// menu itself (QindaQt AppShell, QindaOffice's exporter, anything else).
// Walks the connection's own object tree by local introspection (QtDBus
// answers calls to its own unique name in-process).
bool appOwnsDesktopMenu(const QDBusConnection &bus)
{
    if (bus.objectRegisteredAt(QString::fromLatin1(kAppShellMenuPath)))
        return true;
    int budget = 256;
    std::function<bool(const QString &, int)> walk = [&](const QString &path, int depth) -> bool {
        if (--budget < 0 || depth > 12)
            return false;
        QDBusMessage call = QDBusMessage::createMethodCall(bus.baseService(), path,
            QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
        const QDBusMessage reply = bus.call(call, QDBus::Block, 500);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return false;
        QXmlStreamReader xml(reply.arguments().constFirst().toString());
        QStringList children;
        bool menuHere = false;
        int level = 0;
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                ++level;
                if (xml.name() == QLatin1String("interface")
                    && xml.attributes().value(QLatin1String("name")) == QLatin1String(kDbusMenuInterface))
                    menuHere = true;
                else if (xml.name() == QLatin1String("node") && level == 2)
                    children << xml.attributes().value(QLatin1String("name")).toString();
            } else if (token == QXmlStreamReader::EndElement) {
                --level;
            }
        }
        if (menuHere && !path.startsWith(QLatin1String("/org/qindatk/GlobalMenu/")))
            return true;
        for (const QString &child : std::as_const(children)) {
            if (child.isEmpty())
                continue;
            if (walk(path == QLatin1String("/") ? QLatin1Char('/') + child : path + QLatin1Char('/') + child,
                     depth + 1))
                return true;
        }
        return false;
    };
    return walk(QStringLiteral("/"), 0);
}

QDesktopUnixServices *unixServices()
{
    QPlatformIntegration *integration = QGuiApplicationPrivate::platformIntegration();
    return integration ? dynamic_cast<QDesktopUnixServices *>(integration->services()) : nullptr;
}

} // namespace
#endif

struct GlobalMenuExport::Private {
    GlobalMenuExport *q;
    QPointer<QObject> bar;
    QPointer<QQuickWindow> window;
    bool completed = false;
    bool hosted = false;
    QString status;
    std::unique_ptr<GlobalMenu::Projection> projection;
    QTimer startTimer;
#ifdef QINDATK_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    std::unique_ptr<GlobalMenu::Server> server;
    std::unique_ptr<QDBusServiceWatcher> watcher;
    GlobalMenu::HostedSignal hostedSignal;
    QString path;
    QString registrarOwner;
    bool started = false;
    bool surfaceDestroyed = false;
    bool waylandPublished = false;
    std::optional<quint32> x11Window;
    quint64 serial = 0;
#endif

    void setStatus(const QString &value)
    {
        if (status == value)
            return;
        status = value;
        qCDebug(lcGlobalMenu) << "status" << value << (window ? window->title() : QString());
        emit q->statusChanged();
    }
    void setHosted(bool value)
    {
        if (hosted == value)
            return;
        hosted = value;
        qCDebug(lcGlobalMenu) << "hosted" << value << (window ? window->title() : QString());
        emit q->hostedChanged();
    }
    void attach();
#ifdef QINDATK_HAVE_DBUS
    void start();
    void stop();
    void handleOwner(const QString &owner);
    void publishIdentity();
    void retireIdentity();
    void queryHosted();
    void subscribeHosted(bool on);
    void standAside();
#endif
};

void GlobalMenuExport::Private::attach()
{
    auto *item = qobject_cast<QQuickItem *>(bar.data());
    QQuickWindow *w = item ? item->window() : nullptr;
    if (w == window)
        return;
#ifdef QINDATK_HAVE_DBUS
    stop();
#endif
    if (window)
        window->removeEventFilter(q);
    window = w;
    if (!window)
        return;
    window->installEventFilter(q);
    QObject::connect(window, &QWindow::visibleChanged, q, [this](bool visible) {
        if (visible)
            startTimer.start();
    });
    if (window->isVisible())
        startTimer.start();
}

#ifdef QINDATK_HAVE_DBUS
void GlobalMenuExport::Private::start()
{
    if (started || !window || !bar)
        return;
    if (!bus.isConnected()) {
        setStatus(s("no-session-bus"));
        return;
    }
    if (appOwnsDesktopMenu(bus)) {
        setStatus(s("app-menu"));    // the application publishes its desktop menu itself
        return;
    }
    QObject *claim = window->property(kWindowClaim).value<QObject *>();
    if (claim && claim != q) {
        setStatus(s("duplicate"));   // another Tk.MenuBar already speaks for this window
        return;
    }
    projection = std::make_unique<GlobalMenu::Projection>(bar);
    server = std::make_unique<GlobalMenu::Server>();
    server->publish(projection->snapshot());
    QObject::connect(projection.get(), &GlobalMenu::Projection::changed, q, [this] {
        if (server && projection)
            server->publish(projection->snapshot());
    });
    // Queued: the D-Bus Event reply returns before application code runs.
    QObject::connect(server.get(), &GlobalMenu::Server::activated, q,
                     [this](const QString &id) { if (projection) projection->activate(id); },
                     Qt::QueuedConnection);
    path = QStringLiteral("/org/qindatk/GlobalMenu/%1").arg(++g_pathSerial);
    if (!bus.registerObject(path, server.get(),
                            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals
                                | QDBusConnection::ExportScriptableProperties)) {
        server.reset();
        projection.reset();
        setStatus(s("register-failed"));
        return;
    }
    window->setProperty(kWindowClaim, QVariant::fromValue<QObject *>(q));
    started = true;
    hostedSignal.callback = [this](const QString &provider, const QString &menuPath, bool isHosted) {
        if (provider == bus.baseService() && menuPath == path)
            setHosted(isHosted && status == QLatin1String("published"));
    };
    watcher = std::make_unique<QDBusServiceWatcher>(s(kRegistrarService), bus,
                                                    QDBusServiceWatcher::WatchForOwnerChange);
    QObject::connect(watcher.get(), &QDBusServiceWatcher::serviceOwnerChanged, q,
                     [this](const QString &, const QString &, const QString &owner) { handleOwner(owner); });
    setStatus(s("waiting-for-registrar"));
    // Applications that start their own export a little later still win:
    // look again shortly after publishing (see standAside()).
    for (int delay : { 1000, 4000 })
        QTimer::singleShot(delay, q, [this] {
            if (started && appOwnsDesktopMenu(bus))
                standAside();
        });
    QDBusMessage call = QDBusMessage::createMethodCall(s("org.freedesktop.DBus"), s("/org/freedesktop/DBus"),
                                                       s("org.freedesktop.DBus"), s("GetNameOwner"));
    call << s(kRegistrarService);
    const quint64 expected = ++serial;
    auto *pending = new QDBusPendingCallWatcher(bus.asyncCall(call, 2000), q);
    QObject::connect(pending, &QDBusPendingCallWatcher::finished, q, [this, pending, expected] {
        const QDBusPendingReply<QString> reply = *pending;
        pending->deleteLater();
        if (started && serial == expected)
            handleOwner(reply.isValid() ? reply.value() : QString());
    });
}

void GlobalMenuExport::Private::subscribeHosted(bool on)
{
    if (registrarOwner.isEmpty())
        return;
    if (on)
        bus.connect(registrarOwner, s(kRegistrarPath), s(kRegistrarInterface), s("MenuHostedChanged"),
                    &hostedSignal, SLOT(menuHostedChanged(QString,QDBusObjectPath,bool)));
    else
        bus.disconnect(registrarOwner, s(kRegistrarPath), s(kRegistrarInterface), s("MenuHostedChanged"),
                       &hostedSignal, SLOT(menuHostedChanged(QString,QDBusObjectPath,bool)));
}

void GlobalMenuExport::Private::handleOwner(const QString &owner)
{
    if (!started || owner == registrarOwner)
        return;
    qCDebug(lcGlobalMenu) << "registrar owner" << owner;
    subscribeHosted(false);
    retireIdentity();
    registrarOwner = owner;
    ++serial;
    setHosted(false);
    if (registrarOwner.isEmpty()) {
        setStatus(s("waiting-for-registrar"));   // no global-menu panel right now
        return;
    }
    subscribeHosted(true);
    publishIdentity();
}

void GlobalMenuExport::Private::publishIdentity()
{
    if (!window || surfaceDestroyed || registrarOwner.isEmpty())
        return;
    const QString platform = QGuiApplication::platformName();
    if (platform.startsWith(QLatin1String("wayland"))) {
        const auto *native = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        QDesktopUnixServices *services = unixServices();
        if (!native || !native->surface() || !services) {
            setStatus(s("unsupported-platform"));
            return;
        }
        // Qt's org_kde_kwin_appmenu announcement for this surface.
        services->registerDBusMenuForWindow(window, bus.baseService(), path);
        waylandPublished = true;
        setStatus(s("published"));
        queryHosted();
        return;
    }
    if (platform == QLatin1String("xcb")) {
        const WId id = window->winId();
        if (id == 0 || id > 0xffffffffu) {
            setStatus(s("unsupported-platform"));
            return;
        }
        x11Window = quint32(id);   // registrar state from now on (see retireIdentity)
        QDBusMessage call = QDBusMessage::createMethodCall(registrarOwner, s(kRegistrarPath),
                                                           s(kRegistrarInterface), s("RegisterWindow"));
        call << quint32(id) << QVariant::fromValue(QDBusObjectPath(path));
        const quint64 expected = ++serial;
        auto *pending = new QDBusPendingCallWatcher(bus.asyncCall(call, 2000), q);
        QObject::connect(pending, &QDBusPendingCallWatcher::finished, q, [this, pending, expected] {
            const bool ok = !pending->isError();
            pending->deleteLater();
            if (!started || serial != expected)
                return;
            setStatus(ok ? s("published") : s("waiting-for-registrar"));
            if (ok)
                queryHosted();
        });
        return;
    }
    // AGENT-NOTE: test seam for tests/cpp/tst_global_menu.cpp, which runs
    // offscreen on a private bus with a fake registrar. Only window-less
    // platforms honour it, so it can never fake a real desktop announcement.
    if ((platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
        && qEnvironmentVariableIntValue("QINDATK_GLOBAL_MENU_ASSUME_IDENTITY") == 1) {
        setStatus(s("published"));
        queryHosted();
        return;
    }
    setStatus(s("unsupported-platform"));   // offscreen, eglfs, ...: keep the in-window bar
}

void GlobalMenuExport::Private::retireIdentity()
{
    if (waylandPublished && window)
        if (QDesktopUnixServices *services = unixServices())
            services->unregisterDBusMenuForWindow(window);
    waylandPublished = false;
    if (x11Window && !registrarOwner.isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(registrarOwner, s(kRegistrarPath),
                                                           s(kRegistrarInterface), s("UnregisterWindow"));
        call << *x11Window;
        bus.asyncCall(call, 2000);
    }
    x11Window.reset();
}

void GlobalMenuExport::Private::queryHosted()
{
    QDBusMessage call = QDBusMessage::createMethodCall(registrarOwner, s(kRegistrarPath), s(kRegistrarInterface),
                                                       s("IsMenuHosted"));
    call << bus.baseService() << QVariant::fromValue(QDBusObjectPath(path));
    const quint64 expected = serial;
    auto *pending = new QDBusPendingCallWatcher(bus.asyncCall(call, 2000), q);
    QObject::connect(pending, &QDBusPendingCallWatcher::finished, q, [this, pending, expected] {
        const QDBusPendingReply<bool> reply = *pending;
        pending->deleteLater();
        if (started && serial == expected && status == QLatin1String("published"))
            setHosted(reply.isValid() && reply.value());
    });
}

void GlobalMenuExport::Private::standAside()
{
    // AGENT-GUARD: the application's own export announced the window after
    // ours, so the window's announcement is theirs now. Withdrawing it here
    // (unregisterDBusMenuForWindow / UnregisterWindow) would erase their
    // menu; only our D-Bus object goes.
    waylandPublished = false;
    x11Window.reset();
    stop();
    setStatus(s("app-menu"));
}

void GlobalMenuExport::Private::stop()
{
    startTimer.stop();
    if (started) {
        subscribeHosted(false);
        retireIdentity();
        bus.unregisterObject(path);
        watcher.reset();
        server.reset();
        projection.reset();
        started = false;
        registrarOwner.clear();
        ++serial;
        if (window && window->property(kWindowClaim).value<QObject *>() == q)
            window->setProperty(kWindowClaim, QVariant());
    }
    setHosted(false);
    setStatus(s("idle"));
}
#endif

GlobalMenuExport::GlobalMenuExport(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
#ifdef QINDATK_HAVE_DBUS
    d->status = QStringLiteral("idle");
    d->startTimer.setSingleShot(true);
    d->startTimer.setInterval(0);   // after the window shows and AppShell exports have registered
    connect(&d->startTimer, &QTimer::timeout, this, [this] { d->start(); });
#else
    d->status = QStringLiteral("unavailable");
#endif
}

GlobalMenuExport::~GlobalMenuExport()
{
#ifdef QINDATK_HAVE_DBUS
    d->stop();
#endif
    if (d->window)
        d->window->removeEventFilter(this);
}

QObject *GlobalMenuExport::menuBar() const { return d->bar; }

void GlobalMenuExport::setMenuBar(QObject *menuBar)
{
    if (d->bar == menuBar)
        return;
    if (auto *old = qobject_cast<QQuickItem *>(d->bar.data()))
        disconnect(old, &QQuickItem::windowChanged, this, nullptr);
    d->bar = menuBar;
    if (auto *item = qobject_cast<QQuickItem *>(menuBar))
        connect(item, &QQuickItem::windowChanged, this, [this] { d->attach(); });
    emit menuBarChanged();
    if (d->completed)
        d->attach();
}

void GlobalMenuExport::componentComplete()
{
    d->completed = true;
    d->attach();
}

bool GlobalMenuExport::hosted() const { return d->hosted; }
QString GlobalMenuExport::status() const { return d->status; }

QString GlobalMenuExport::objectPath() const
{
#ifdef QINDATK_HAVE_DBUS
    return d->started ? d->path : QString();
#else
    return {};
#endif
}

bool GlobalMenuExport::activate(const QString &stableId)
{
#ifdef QINDATK_HAVE_DBUS
    return d->projection && d->projection->activate(stableId);
#else
    Q_UNUSED(stableId)
    return false;
#endif
}

bool GlobalMenuExport::eventFilter(QObject *watched, QEvent *event)
{
#ifdef QINDATK_HAVE_DBUS
    if (watched == d->window && event->type() == QEvent::PlatformSurface && d->started) {
        const auto type = static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType();
        if (type == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            // The Wayland withdrawal needs the still-live surface; never reuse it after.
            d->surfaceDestroyed = true;
            ++d->serial;
            d->retireIdentity();
            d->setHosted(false);
            d->setStatus(QStringLiteral("waiting-for-registrar"));
        } else if (type == QPlatformSurfaceEvent::SurfaceCreated) {
            d->surfaceDestroyed = false;
            d->publishIdentity();
        }
    }
#endif
    return QObject::eventFilter(watched, event);
}

} // namespace QindaTK
