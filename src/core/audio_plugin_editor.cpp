// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_plugin_editor.h"

#include <QCloseEvent>
#include <QResizeEvent>

namespace QindaTK {

AudioPluginEditorWindow::AudioPluginEditorWindow(QWindow *owner)
{
    setObjectName(QStringLiteral("pluginEditorWindow"));
    setFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    if (owner) setTransientParent(owner);
}

void AudioPluginEditorWindow::applyPluginSize(const QSize &size, bool resizable)
{
    if (!size.isValid() || size.isEmpty()) return;
    m_applying = true;
    if (!resizable) {
        setMinimumSize(size);
        setMaximumSize(size);
    } else {
        setMinimumSize(QSize(1, 1));
        setMaximumSize(QSize(16777215, 16777215)); // Qt's QWINDOWSIZE_MAX
    }
    resize(size);
    m_applying = false;
}

// The window stays until the plugin has let go of its child window: the
// instance closes the editor first and then deletes this window.
void AudioPluginEditorWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    emit closeRequested();
}

void AudioPluginEditorWindow::resizeEvent(QResizeEvent *event)
{
    QWindow::resizeEvent(event);
    if (!m_applying) emit userResized(event->size());
}

} // namespace QindaTK
