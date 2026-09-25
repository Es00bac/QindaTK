// SPDX-License-Identifier: LGPL-3.0-or-later
// ClapInstance: the plugin editor (clap.gui, embedded x11 or floating; D-193)
// and the main-thread services an editor needs: timers (clap.timer-support)
// and file descriptors (clap.posix-fd-support), run by this process's Qt
// event loop.
#include "plugin_host_clap.h"

#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>

namespace QindaTK::PluginHost {

QJsonObject ClapInstance::openEditor(const QJsonObject &request, QString *error)
{
    if (!m_gui) {
        *error = QStringLiteral("The plugin has no editor");
        return {};
    }
    if (m_guiCreated) closeEditor();
    const QByteArray api = request.value(QStringLiteral("api")).toString(QStringLiteral("x11")).toUtf8();
    bool floating = request.value(QStringLiteral("floating")).toBool();
    if (!m_gui->is_api_supported(m_plugin, api.constData(), floating)) {
        if (!floating && m_gui->is_api_supported(m_plugin, api.constData(), true)) {
            floating = true;
        } else {
            *error = QStringLiteral("The plugin editor does not support the %1 window system").arg(QString::fromUtf8(api));
            return {};
        }
    }
    if (!m_gui->create(m_plugin, api.constData(), floating)) {
        *error = QStringLiteral("The plugin could not create its editor");
        return {};
    }
    m_guiCreated = true;
    m_guiFloating = floating;
    const QByteArray title = request.value(QStringLiteral("title")).toString().toUtf8();
    const auto window = clap_xwnd(request.value(QStringLiteral("window")).toInteger());
    uint32_t width = 0;
    uint32_t height = 0;
    if (!floating) {
        m_gui->set_scale(m_plugin, request.value(QStringLiteral("scale")).toDouble(1.0));
        m_gui->get_size(m_plugin, &width, &height);
        clap_window_t parent{};
        parent.api = api.constData();
        parent.x11 = window;
        if (!m_gui->set_parent(m_plugin, &parent)) {
            closeEditor();
            *error = QStringLiteral("The plugin editor could not be embedded");
            return {};
        }
    } else {
        if (window) {
            clap_window_t transient{};
            transient.api = api.constData();
            transient.x11 = window;
            m_gui->set_transient(m_plugin, &transient);
        }
        if (!title.isEmpty()) m_gui->suggest_title(m_plugin, title.constData());
    }
    if (!m_gui->show(m_plugin)) {
        closeEditor();
        *error = QStringLiteral("The plugin editor could not be shown");
        return {};
    }
    if (!floating) m_gui->get_size(m_plugin, &width, &height);
    return {{QStringLiteral("floating"), floating},
            {QStringLiteral("width"), int(width)},
            {QStringLiteral("height"), int(height)},
            {QStringLiteral("resizable"), m_gui->can_resize(m_plugin)}};
}

bool ClapInstance::closeEditor()
{
    if (!m_gui || !m_guiCreated) return false;
    m_gui->hide(m_plugin);
    m_gui->destroy(m_plugin);
    m_guiCreated = false;
    return true;
}

bool ClapInstance::setEditorSize(quint32 width, quint32 height)
{
    if (!m_gui || !m_guiCreated || m_guiFloating || !m_gui->can_resize(m_plugin)) return false;
    m_gui->adjust_size(m_plugin, &width, &height);
    return m_gui->set_size(m_plugin, width, height);
}

bool ClapInstance::registerTimer(quint32 periodMs, clap_id *id)
{
    if (!isMainThread() || !m_timerExt || !id) return false;
    auto *timer = new QTimer;
    const clap_id key = m_nextTimer++;
    timer->setInterval(int(std::max<quint32>(1, periodMs)));
    QObject::connect(timer, &QTimer::timeout, [this, key] {
        if (m_timerExt) m_timerExt->on_timer(m_plugin, key);
    });
    timer->start();
    m_timers.insert(key, timer);
    *id = key;
    return true;
}

bool ClapInstance::unregisterTimer(clap_id id)
{
    QTimer *timer = m_timers.take(id);
    if (!timer) return false;
    timer->stop();
    timer->deleteLater();
    return true;
}

bool ClapInstance::registerFd(int fd, clap_posix_fd_flags_t flags)
{
    if (!isMainThread() || !m_fdExt || m_fds.contains(fd)) return false;
    QVector<QSocketNotifier *> notifiers;
    const auto watch = [&](QSocketNotifier::Type type, clap_posix_fd_flags_t flag) {
        auto *notifier = new QSocketNotifier(qintptr(fd), type);
        QObject::connect(notifier, &QSocketNotifier::activated, [this, fd, flag] {
            if (m_fdExt) m_fdExt->on_fd(m_plugin, fd, flag);
        });
        notifiers.push_back(notifier);
    };
    if (flags & CLAP_POSIX_FD_READ) watch(QSocketNotifier::Read, CLAP_POSIX_FD_READ);
    if (flags & CLAP_POSIX_FD_WRITE) watch(QSocketNotifier::Write, CLAP_POSIX_FD_WRITE);
    if (flags & CLAP_POSIX_FD_ERROR) watch(QSocketNotifier::Exception, CLAP_POSIX_FD_ERROR);
    m_fds.insert(fd, notifiers);
    return true;
}

bool ClapInstance::unregisterFd(int fd)
{
    if (!m_fds.contains(fd)) return false;
    for (QSocketNotifier *notifier : m_fds.take(fd)) {
        notifier->setEnabled(false);
        notifier->deleteLater();
    }
    return true;
}

} // namespace QindaTK::PluginHost
