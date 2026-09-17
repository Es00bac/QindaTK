// SPDX-License-Identifier: LGPL-3.0-or-later
#include "layout_attached.h"

#include <QQuickItem>

namespace QindaTK {

LayoutAttachedBase::LayoutAttachedBase(QObject *parent)
    : QObject(parent)
{
}

QQuickItem *LayoutAttachedBase::attachee() const
{
    return qobject_cast<QQuickItem *>(parent());
}

void LayoutAttachedBase::relayoutParent()
{
    QQuickItem *item = attachee();
    if (item == nullptr || item->parentItem() == nullptr) {
        return;
    }
    if (auto *container = dynamic_cast<LayoutContainer *>(item->parentItem())) {
        container->invalidateLayout();
    }
}

} // namespace QindaTK
