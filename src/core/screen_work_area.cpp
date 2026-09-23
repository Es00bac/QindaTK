// SPDX-License-Identifier: LGPL-3.0-or-later
#include "screen_work_area.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>

namespace QindaTK {
ScreenWorkArea::ScreenWorkArea(QObject *parent) : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &ScreenWorkArea::reconnect);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &ScreenWorkArea::reconnect);
}
void ScreenWorkArea::setAnchor(QQuickItem *anchor)
{
    if (m_anchor == anchor) return;
    m_anchor = anchor;
    reconnect();
    emit anchorChanged();
}
void ScreenWorkArea::setAnchorPoint(QPointF point)
{
    if (m_point == point) return;
    m_point = point;
    refresh();
    emit anchorPointChanged();
}
void ScreenWorkArea::setTrackTransforms(bool track)
{
    if (m_trackTransforms == track) return;
    m_trackTransforms = track;
    reconnect();
    emit trackTransformsChanged();
}
void ScreenWorkArea::reconnect()
{
    for (const auto &connection : m_connections) disconnect(connection);
    m_connections.clear();
    for (auto *item = m_anchor.data(); item; item = item->parentItem()) {
        m_connections << connect(item, &QQuickItem::xChanged, this, &ScreenWorkArea::refresh)
                      << connect(item, &QQuickItem::yChanged, this, &ScreenWorkArea::refresh)
                      << connect(item, &QQuickItem::scaleChanged, this, &ScreenWorkArea::refresh)
                      << connect(item, &QQuickItem::rotationChanged, this, &ScreenWorkArea::refresh)
                      << connect(item, &QQuickItem::parentChanged, this, &ScreenWorkArea::reconnect)
                      << connect(item, &QQuickItem::windowChanged, this, &ScreenWorkArea::reconnect);
    }
    if (m_anchor) {
        m_connections << connect(m_anchor, &QObject::destroyed, this, [this] {
            m_anchor = nullptr;
            reconnect();
            emit anchorChanged();
        });
    }
    if (auto *window = m_anchor ? m_anchor->window() : nullptr) {
        m_connections << connect(window, &QWindow::xChanged, this, &ScreenWorkArea::refresh)
                      << connect(window, &QWindow::yChanged, this, &ScreenWorkArea::refresh)
                      << connect(window, &QWindow::screenChanged, this, &ScreenWorkArea::refresh);
        // Arbitrary ancestor transforms need a frame check only while a popup
        // is visible; large menu bars can contain many closed menus.
        if (m_trackTransforms)
            m_connections << connect(window, &QQuickWindow::afterAnimating, this, &ScreenWorkArea::refresh);
    }
    for (auto *screen : QGuiApplication::screens()) {
        m_connections << connect(screen, &QScreen::availableGeometryChanged, this, &ScreenWorkArea::refresh)
                      << connect(screen, &QScreen::geometryChanged, this, &ScreenWorkArea::refresh)
                      << connect(screen, &QScreen::logicalDotsPerInchChanged, this, &ScreenWorkArea::refresh)
                      << connect(screen, &QScreen::physicalDotsPerInchChanged, this, &ScreenWorkArea::refresh);
    }
    refresh();
}
void ScreenWorkArea::refresh()
{
    QScreen *screen = nullptr;
    if (auto *window = m_anchor ? m_anchor->window() : nullptr) {
        const QPoint global = m_anchor->mapToGlobal(m_point).toPoint();
        // AGENT-CONTRACT: Menu.qml supplies its parent-local popup position.
        // Prefer that monitor, including a window spanning multiple screens.
        // A gap/off-screen anchor falls back to the owning window's screen.
        screen = window->screen() ? window->screen()->virtualSiblingAt(global) : nullptr;
        if (!screen) screen = window->screen();
    }
    const QRect geometry = screen ? screen->availableGeometry() : QRect();
    if (m_geometry == geometry) return;
    m_geometry = geometry;
    emit availableGeometryChanged();
}
}
