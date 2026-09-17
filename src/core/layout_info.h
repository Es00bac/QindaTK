// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQuickItem;
class QQmlEngine;
class QJSEngine;

namespace QindaTK {

// Facts about an item's own layout intent that QML cannot read itself:
// whether it anchors, whether its width/height were set explicitly.
// Box and Scroll use them to stretch only children that have not placed
// themselves. Backed by QQuickItemPrivate (Qt6::QuickPrivate).
class LayoutInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    [[nodiscard]] static LayoutInfo *create(QQmlEngine *engine, QJSEngine *jsEngine);

    Q_INVOKABLE [[nodiscard]] bool hasAnchors(QQuickItem *item) const;
    Q_INVOKABLE [[nodiscard]] bool hasExplicitWidth(QQuickItem *item) const;
    Q_INVOKABLE [[nodiscard]] bool hasExplicitHeight(QQuickItem *item) const;
    // Item positions and sizes as one object, for QML-side diagnostics.
    Q_INVOKABLE [[nodiscard]] QString describe(QQuickItem *item) const;
};

} // namespace QindaTK
