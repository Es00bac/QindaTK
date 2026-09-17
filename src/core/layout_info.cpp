// SPDX-License-Identifier: LGPL-3.0-or-later
#include "layout_info.h"

#include <QJSEngine>
#include <QQuickItem>
#include <QtQuick/private/qquickanchors_p.h>
#include <QtQuick/private/qquickitem_p.h>

namespace QindaTK {

LayoutInfo *LayoutInfo::create(QQmlEngine *, QJSEngine *)
{
    static LayoutInfo *info = new LayoutInfo();
    QJSEngine::setObjectOwnership(info, QJSEngine::CppOwnership);
    return info;
}

bool LayoutInfo::hasAnchors(QQuickItem *item) const
{
    if (item == nullptr) {
        return false;
    }
    const QQuickItemPrivate *d = QQuickItemPrivate::get(item);
    return d->_anchors != nullptr && d->_anchors->usedAnchors() != QQuickAnchors::Anchors();
}

bool LayoutInfo::hasExplicitWidth(QQuickItem *item) const
{
    return item != nullptr && QQuickItemPrivate::get(item)->widthValid();
}

bool LayoutInfo::hasExplicitHeight(QQuickItem *item) const
{
    return item != nullptr && QQuickItemPrivate::get(item)->heightValid();
}

QString LayoutInfo::describe(QQuickItem *item) const
{
    if (item == nullptr) {
        return QStringLiteral("null");
    }
    return QStringLiteral("%1 %2,%3 %4x%5 implicit %6x%7%8")
        .arg(QString::fromLatin1(item->metaObject()->className()))
        .arg(item->x()).arg(item->y()).arg(item->width()).arg(item->height())
        .arg(item->implicitWidth()).arg(item->implicitHeight())
        .arg(hasAnchors(item) ? QStringLiteral(" anchored") : QString());
}

} // namespace QindaTK
