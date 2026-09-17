// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <limits>

class QQuickItem;

namespace QindaTK {

// Sizing constraints and ordering shared by the children of Flex, Grid and
// Stack. Each container exposes it as its own attached type (Flex.minWidth,
// Grid.minWidth) so a child reads naturally next to its container-specific
// keys. Defaults are CSS's dense-friendly ones: min 0 (the `min-w-0` every
// dense layout wants), max unbounded.
class LayoutAttachedBase : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal minWidth READ minWidth WRITE setMinWidth NOTIFY changed)
    Q_PROPERTY(qreal maxWidth READ maxWidth WRITE setMaxWidth NOTIFY changed)
    Q_PROPERTY(qreal minHeight READ minHeight WRITE setMinHeight NOTIFY changed)
    Q_PROPERTY(qreal maxHeight READ maxHeight WRITE setMaxHeight NOTIFY changed)
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY changed)
    // An ignored child keeps its own geometry (overlays, absolutely placed
    // decorations) and takes no slot.
    Q_PROPERTY(bool ignore READ ignore WRITE setIgnore NOTIFY changed)
    Q_PROPERTY(int alignSelf READ alignSelf WRITE setAlignSelf NOTIFY changed)

public:
    static constexpr qreal unbounded = std::numeric_limits<qreal>::infinity();

    explicit LayoutAttachedBase(QObject *parent = nullptr);

    [[nodiscard]] qreal minWidth() const { return m_minWidth; }
    void setMinWidth(qreal value) { assign(m_minWidth, value); }
    [[nodiscard]] qreal maxWidth() const { return m_maxWidth; }
    void setMaxWidth(qreal value) { assign(m_maxWidth, value); }
    [[nodiscard]] qreal minHeight() const { return m_minHeight; }
    void setMinHeight(qreal value) { assign(m_minHeight, value); }
    [[nodiscard]] qreal maxHeight() const { return m_maxHeight; }
    void setMaxHeight(qreal value) { assign(m_maxHeight, value); }
    [[nodiscard]] int order() const { return m_order; }
    void setOrder(int value) { assign(m_order, value); }
    [[nodiscard]] bool ignore() const { return m_ignore; }
    void setIgnore(bool value) { assign(m_ignore, value); }
    [[nodiscard]] int alignSelf() const { return m_alignSelf; }
    void setAlignSelf(int value) { assign(m_alignSelf, value); }

    // The QQuickItem this object is attached to, or null.
    [[nodiscard]] QQuickItem *attachee() const;

signals:
    void changed();

protected:
    template <typename T>
    void assign(T &member, const T &value)
    {
        if (member == value) {
            return;
        }
        member = value;
        emit changed();
        relayoutParent();
    }
    // Asks the attachee's parent container to re-run its layout.
    void relayoutParent();

private:
    qreal m_minWidth = 0;
    qreal m_maxWidth = unbounded;
    qreal m_minHeight = 0;
    qreal m_maxHeight = unbounded;
    int m_order = 0;
    int m_alignSelf = 0;
    bool m_ignore = false;
};

// Implemented by every container so attached-property edits can reach it
// without each attached type knowing the container classes.
class LayoutContainer {
public:
    virtual ~LayoutContainer() = default;
    virtual void invalidateLayout() = 0;
};

} // namespace QindaTK
