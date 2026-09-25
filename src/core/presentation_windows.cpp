// SPDX-License-Identifier: LGPL-3.0-or-later
#include "presentation_windows.h"
#include "slide_deck.h"
#include "slide_frame_view.h"
#include "slide_show.h"
#include "window_host.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>

namespace QindaTK {

PresentationWindows::PresentationWindows(QObject *parent) : QObject(parent)
{
    m_ownHost = new WindowHost(this);
    auto queue = [this] {
        if (m_reconcileQueued) return;
        m_reconcileQueued = true;
        // AGENT-NOTE: queued so Qt has finished moving windows off a removed
        // screen (and deleted it) before placement reads QGuiApplication::screens().
        QTimer::singleShot(0, this, &PresentationWindows::reconcile);
    };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, queue);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, queue);
}

PresentationWindows::~PresentationWindows()
{
    m_closing = true;
    disconnect(m_hostConnection);
    if (WindowHost *host = windowHost()) {
        if (!m_audienceId.isEmpty()) host->closeWindow(m_audienceId, true);
        if (!m_presenterId.isEmpty()) host->closeWindow(m_presenterId, true);
    }
}

WindowHost *PresentationWindows::windowHost() const { return m_host ? m_host.data() : m_ownHost; }

void PresentationWindows::setWindowHost(WindowHost *host)
{
    if (m_host == host) return;
    if (m_active) { fail(QStringLiteral("Close the presentation before changing its window host")); return; }
    m_host = host;
    emit configurationChanged();
}

void PresentationWindows::setShow(SlideShow *show)
{
    if (m_show == show) return;
    if (m_active) { fail(QStringLiteral("Close the presentation before changing its show")); return; }
    m_show = show;
    emit configurationChanged();
}

void PresentationWindows::setPresenterDelegate(QQmlComponent *component)
{
    if (m_presenterDelegate == component) return;
    m_presenterDelegate = component;
    emit configurationChanged();
}

void PresentationWindows::setAudienceScreenId(const QString &id)
{
    if (m_audienceScreenId == id) return;
    m_audienceScreenId = id;
    if (m_active) reconcile(); else emit changed();
}

void PresentationWindows::setPresenterScreenId(const QString &id)
{
    if (m_presenterScreenId == id) return;
    m_presenterScreenId = id;
    if (m_active) reconcile(); else emit changed();
}

void PresentationWindows::setFullScreen(bool fullScreen)
{
    if (m_fullScreen == fullScreen) return;
    m_fullScreen = fullScreen;
    if (m_active) reconcile(); else emit changed();
}

QQuickWindow *PresentationWindows::audienceWindow() const
{
    return m_audienceId.isEmpty() ? nullptr : windowHost()->window(m_audienceId);
}

QQuickWindow *PresentationWindows::presenterWindow() const
{
    return m_presenterId.isEmpty() ? nullptr : windowHost()->window(m_presenterId);
}

QString PresentationWindows::audienceScreen() const
{
    QQuickWindow *window = audienceWindow();
    return window && window->screen() ? window->screen()->name() : QString();
}

QScreen *PresentationWindows::screenNamed(const QString &name) const
{
    if (name.isEmpty()) return nullptr;
    for (QScreen *screen : QGuiApplication::screens()) if (screen->name() == name) return screen;
    return nullptr;
}

QScreen *PresentationWindows::presenterScreen() const
{
    if (QScreen *chosen = screenNamed(m_presenterScreenId)) return chosen;
    // Prefer the primary screen unless the audience claimed it.
    QScreen *primary = QGuiApplication::primaryScreen();
    if (primary && (m_audienceScreenId.isEmpty() || primary->name() != m_audienceScreenId)) return primary;
    for (QScreen *screen : QGuiApplication::screens())
        if (screen->name() != m_audienceScreenId) return screen;
    return primary;
}

QVariantList PresentationWindows::displays() const
{
    QVariantList out;
    const QString audience = audienceScreen();
    QQuickWindow *presenter = presenterWindow();
    const QString presenterName = presenter && presenter->screen() ? presenter->screen()->name() : QString();
    bool chosenPresent = false;
    for (QScreen *screen : QGuiApplication::screens()) {
        const QString model = QStringList{screen->manufacturer(), screen->model()}.join(u' ').trimmed();
        const QString label = QStringLiteral("%1%2 (%3×%4)").arg(screen->name(), model.isEmpty() ? QString() : QStringLiteral(" · ") + model)
                                  .arg(screen->geometry().width()).arg(screen->geometry().height());
        const QString role = screen->name() == audience ? QStringLiteral("audience")
                           : screen->name() == presenterName ? QStringLiteral("presenter") : QString();
        chosenPresent = chosenPresent || screen->name() == m_audienceScreenId;
        out << QVariantMap{{"id", screen->name()}, {"label", label}, {"connected", true}, {"role", role}};
    }
    if (!m_audienceScreenId.isEmpty() && !chosenPresent)
        out << QVariantMap{{"id", m_audienceScreenId}, {"label", tr("%1 (disconnected)").arg(m_audienceScreenId)},
                           {"connected", false}, {"role", QStringLiteral("audience")}};
    return out;
}

bool PresentationWindows::fail(const QString &message)
{
    m_error = message;
    emit changed();
    return false;
}

void PresentationWindows::setStatus(const QString &status)
{
    if (m_status == status) return;
    const QString before = m_status;
    m_status = status;
    if (before == QStringLiteral("connected") && status == QStringLiteral("waiting"))
        emit audienceDisconnected(m_audienceScreenId);
    if (before == QStringLiteral("waiting") && status == QStringLiteral("connected"))
        emit audienceReconnected(audienceScreen());
}

void PresentationWindows::fill(QQuickWindow *window, QQuickItem *content)
{
    QQuickItem *root = window->contentItem();
    content->setParentItem(root);
    const QPointer<QQuickItem> guarded(content);
    auto resize = [guarded, root] { if (guarded) guarded->setSize(root->size()); };
    connect(root, &QQuickItem::widthChanged, content, resize);
    connect(root, &QQuickItem::heightChanged, content, resize);
    resize();
}

bool PresentationWindows::openPresenter()
{
    WindowHost *host = windowHost();
    m_presenterId = host->createWindow({}, {}, QStringLiteral("slide-presenter"));
    QQuickWindow *window = presenterWindow();
    if (!window) return fail(host->lastError());
    window->setTitle(tr("Presenter view"));
    QQuickItem *content = nullptr;
    if (m_presenterDelegate) {
        QQmlContext *context = m_presenterDelegate->creationContext() ? m_presenterDelegate->creationContext()
                                                                      : qmlContext(this);
        QObject *object = m_presenterDelegate->createWithInitialProperties(
            {{QStringLiteral("show"), QVariant::fromValue(m_show.data())},
             {QStringLiteral("windows"), QVariant::fromValue(this)}}, context);
        content = qobject_cast<QQuickItem *>(object);
        if (!content) {
            delete object;
            return fail(tr("The presenter delegate is not an Item: %1").arg(m_presenterDelegate->errorString()));
        }
    } else {
        auto *preview = new SlideFrameView;
        preview->setShow(m_show);
        content = preview;
    }
    content->setObjectName(content->objectName().isEmpty() ? QStringLiteral("slidePresenterContent") : content->objectName());
    content->setParent(window); // destroyed with the native window
    fill(window, content);
    m_presenterContent = content;
    return true;
}

bool PresentationWindows::openAudience()
{
    WindowHost *host = windowHost();
    m_audienceId = host->createWindow({}, {}, QStringLiteral("slide-audience"));
    QQuickWindow *window = audienceWindow();
    if (!window) return fail(host->lastError());
    window->setTitle(tr("Audience"));
    window->setColor(Qt::black);
    auto *view = new SlideFrameView;
    view->setObjectName(QStringLiteral("slideAudienceOutput"));
    view->setShow(m_show);
    view->setParent(window);
    fill(window, view);
    m_audienceView = view;
    return true;
}

bool PresentationWindows::open()
{
    if (m_active) return true;
    if (!m_show) return fail(tr("No slide show to present"));
    WindowHost *host = windowHost();
    m_hostConnection = connect(host, &WindowHost::windowClosed, this, &PresentationWindows::windowClosed);
    m_active = true;
    if (!openPresenter() || !openAudience()) {
        const QString error = m_error;
        close();
        return fail(error);
    }
    if (!m_show->running() && !m_show->start()) {
        const QString error = m_show->lastError();
        close();
        return fail(error);
    }
    m_error.clear();
    reconcile();
    return true;
}

void PresentationWindows::close()
{
    m_closing = true;
    disconnect(m_hostConnection);
    WindowHost *host = windowHost();
    if (!m_audienceId.isEmpty()) host->closeWindow(m_audienceId, true);
    if (!m_presenterId.isEmpty()) host->closeWindow(m_presenterId, true);
    m_audienceId.clear();
    m_presenterId.clear();
    m_audienceView = nullptr;
    m_presenterContent = nullptr;
    const bool wasActive = m_active;
    m_active = false;
    m_closing = false;
    setStatus(QStringLiteral("closed"));
    if (wasActive && m_show) m_show->stop();
    emit changed();
}

void PresentationWindows::windowClosed(const QString &windowId)
{
    if (m_closing) return;
    if (windowId == m_audienceId) {
        m_audienceId.clear();
        m_audienceView = nullptr;
        setStatus(QStringLiteral("closed"));
        emit changed();
    } else if (windowId == m_presenterId) {
        m_presenterId.clear();
        m_presenterContent = nullptr;
        close(); // closing the presenter ends the presentation
    }
}

bool PresentationWindows::reconnect(const QString &screenId)
{
    if (!m_active) return fail(tr("Open the presentation first"));
    if (!screenId.isEmpty()) m_audienceScreenId = screenId;
    if (m_audienceId.isEmpty() && !openAudience()) return false;
    reconcile();
    return m_status == QStringLiteral("connected") || m_status == QStringLiteral("shared");
}

bool PresentationWindows::swapDisplays()
{
    QQuickWindow *presenter = presenterWindow();
    const QString audience = audienceScreen();
    if (!m_active || !presenter || !presenter->screen() || audience.isEmpty())
        return fail(tr("Both presentation windows must be open"));
    const QString presenterName = presenter->screen()->name();
    m_audienceScreenId = presenterName;
    m_presenterScreenId = audience;
    reconcile();
    return true;
}

void PresentationWindows::placePresenter()
{
    QQuickWindow *window = presenterWindow();
    QScreen *screen = presenterScreen();
    if (!window || !screen) return;
    if (window->visibility() == QWindow::FullScreen) window->showNormal();
    windowHost()->moveWindow(m_presenterId, screen->availableGeometry(), screen->name());
    window->show();
}

void PresentationWindows::placeAudience()
{
    QQuickWindow *window = audienceWindow();
    if (!window) { setStatus(QStringLiteral("closed")); return; }
    QScreen *presenter = presenterScreen();
    QScreen *target = screenNamed(m_audienceScreenId);
    if (m_audienceScreenId.isEmpty())
        for (QScreen *screen : QGuiApplication::screens())
            if (screen != presenter) { target = screen; break; }
    if (target) {
        if (window->visibility() == QWindow::FullScreen && window->screen() != target) window->showNormal();
        windowHost()->moveWindow(m_audienceId, target->geometry(), target->name());
        if (m_fullScreen) window->showFullScreen(); else window->show();
        setStatus(QStringLiteral("connected"));
        return;
    }
    QScreen *fallback = presenter ? presenter : QGuiApplication::primaryScreen();
    if (!fallback) return;
    if (window->visibility() == QWindow::FullScreen) window->showNormal();
    const QRect available = fallback->availableGeometry();
    const QSize size(std::max(320, available.width() / 2), std::max(180, available.height() / 2));
    windowHost()->moveWindow(m_audienceId, QRect(available.topLeft() + QPoint(24, 24), size), fallback->name());
    window->show();
    setStatus(m_audienceScreenId.isEmpty() ? QStringLiteral("shared") : QStringLiteral("waiting"));
}

void PresentationWindows::reconcile()
{
    m_reconcileQueued = false;
    if (m_active) {
        placePresenter();
        placeAudience();
    }
    emit changed();
}

} // namespace QindaTK
