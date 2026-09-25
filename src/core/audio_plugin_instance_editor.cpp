// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioPluginInstance: the plugin editor's lifecycle (D-193).
#include "audio_plugin_instance.h"

#include "audio_plugin_editor.h"

#include <QGuiApplication>
#include <QProcess>

namespace QindaTK {

bool AudioPluginInstance::editorAvailable() const
{
    return live() && m_hasEditor;
}

AudioPluginEditorWindow *AudioPluginInstance::editorWindow() const
{
    return m_editor.data();
}

// AGENT-NOTE (D-193): under xcb the editor is embedded (CLAP x11, not
// floating) into a toolkit window we own; the offscreen and minimal
// platforms take the same path, so tests exercise the whole lifecycle with
// the window IDs those platforms hand out. Elsewhere (Wayland) the plugin
// shows a floating window of its own, because an x11 child cannot be
// embedded into a Wayland surface from another process.
bool AudioPluginInstance::openEditor(QWindow *owner, QString *error)
{
    if (!live()) {
        *error = tr("%1 is not running").arg(m_descriptor.name);
        return false;
    }
    if (!m_hasEditor) {
        *error = tr("%1 has no editor").arg(m_descriptor.name);
        return false;
    }
    if (m_editorOpen) {
        if (m_editor) m_editor->requestActivate();
        return true;
    }
    const QString platform = QGuiApplication::platformName();
    const bool embed = platform == QLatin1String("xcb") || platform == QLatin1String("offscreen")
                       || platform == QLatin1String("minimal");
    QJsonObject message{{QStringLiteral("op"), QStringLiteral("gui-open")},
                        {QStringLiteral("api"), QStringLiteral("x11")},
                        {QStringLiteral("floating"), !embed},
                        {QStringLiteral("title"), m_descriptor.name}};
    if (embed) {
        auto *window = new AudioPluginEditorWindow(owner);
        window->setTitle(m_descriptor.name.isEmpty() ? m_pluginId : m_descriptor.name);
        window->create();
        m_editor = window;
        message.insert(QStringLiteral("window"), qint64(window->winId()));
        message.insert(QStringLiteral("scale"), window->devicePixelRatio());
        connect(window, &AudioPluginEditorWindow::closeRequested, this, [this] {
            QMetaObject::invokeMethod(this, [this] { closeEditor(); }, Qt::QueuedConnection);
        });
        connect(window, &AudioPluginEditorWindow::userResized, this, [this](const QSize &size) {
            if (!m_editorResizable || !m_editorOpen) return;
            QString ignored;
            request({{QStringLiteral("op"), QStringLiteral("gui-size")},
                     {QStringLiteral("width"), size.width()},
                     {QStringLiteral("height"), size.height()}},
                    m_timeouts.requestMs, &ignored);
        });
    }
    const auto reply = request(message, m_timeouts.requestMs, error);
    if (!reply) {
        dropEditorWindow();
        if (m_timedOut) fail(*error);
        return false;
    }
    m_editorFloating = reply->value(QStringLiteral("floating")).toBool();
    m_editorResizable = reply->value(QStringLiteral("resizable")).toBool();
    if (m_editorFloating) {
        dropEditorWindow(); // the plugin shows a window of its own
    } else if (m_editor) {
        m_editor->applyPluginSize(QSize(reply->value(QStringLiteral("width")).toInt(),
                                        reply->value(QStringLiteral("height")).toInt()),
                                  m_editorResizable);
        m_editor->show();
        m_editor->requestActivate();
    }
    m_editorOpen = true;
    emit editorChanged();
    emit changed();
    return true;
}

void AudioPluginInstance::closeEditor()
{
    if (!m_editorOpen && !m_editor) return;
    if (m_editorOpen && m_process && m_process->state() == QProcess::Running) {
        QString error;
        request({{QStringLiteral("op"), QStringLiteral("gui-close")}}, m_timeouts.requestMs, &error);
    }
    // The plugin has destroyed its child window: the parent can go now.
    dropEditorWindow();
    m_editorOpen = false;
    m_editorFloating = false;
    emit editorChanged();
    emit changed();
}

void AudioPluginInstance::dropEditorWindow()
{
    if (!m_editor) return;
    AudioPluginEditorWindow *window = m_editor;
    m_editor = nullptr;
    window->disconnect(this);
    window->hide();
    window->deleteLater();
}

void AudioPluginInstance::editorEvent(const QJsonObject &event)
{
    const QString kind = event.value(QStringLiteral("event")).toString();
    if (kind == QLatin1String("gui-resize")) {
        if (m_editor)
            m_editor->applyPluginSize(QSize(event.value(QStringLiteral("width")).toInt(),
                                            event.value(QStringLiteral("height")).toInt()),
                                      m_editorResizable);
    } else if (kind == QLatin1String("gui-show")) {
        if (m_editor) m_editor->show();
    } else if (kind == QLatin1String("gui-hide")) {
        if (m_editor) m_editor->hide();
    } else if (kind == QLatin1String("gui-closed")) {
        // The plugin closed its (floating) editor itself.
        dropEditorWindow();
        if (!m_editorOpen) return;
        m_editorOpen = false;
        m_editorFloating = false;
        emit editorChanged();
        emit changed();
    }
}

} // namespace QindaTK
