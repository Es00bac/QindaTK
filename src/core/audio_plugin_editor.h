// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QSize>
#include <QWindow>

namespace QindaTK {

// AGENT-CONTRACT (WP-066, D-193, docs/audio-plugins.md): the toolkit-owned
// top-level window a plugin editor is embedded into (x11 under xcb). The
// toolkit owns the window: its title, its transient parent (the application
// window that asked), its position, closing it and its lifetime. The plugin's
// process owns the child window inside it and that child's keyboard focus.
// Closing the window closes the editor (AudioPluginInstance::closeEditor);
// a failed plugin closes its window.
class AudioPluginEditorWindow : public QWindow {
    Q_OBJECT
public:
    explicit AudioPluginEditorWindow(QWindow *owner = nullptr);
    // The plugin's size; a fixed-size editor also fixes the window.
    void applyPluginSize(const QSize &size, bool resizable);
    bool applyingPluginSize() const { return m_applying; }

signals:
    void closeRequested();
    void userResized(const QSize &size);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool m_applying = false;
};

} // namespace QindaTK
