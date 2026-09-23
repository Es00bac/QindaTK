// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// FocusManager tracks transient UI surfaces such as menus, dialogs, and
// popovers. enterSurface() returns an opaque restoration token and records
// the currently focused item/window through weak pointers. activate() may
// focus the surface target. closeSurface() removes that surface and nested
// surfaces; focus returns to the saved target only if focus is still inside
// the closing surface tree (or was cleared). This guard prevents a late popup
// close from stealing focus after the user explicitly moved elsewhere. A
// destroyed owner or target is treated as closed, and a destroyed saved item
// is never restored. QML can pass Item targets directly and retain tokens as
// strings; applications should keep one manager per window.
class FocusManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString activeToken READ activeToken NOTIFY changed)
public:
    explicit FocusManager(QObject *parent = nullptr);

    Q_INVOKABLE QString enterSurface(QObject *owner, QQuickItem *target = nullptr,
                                     QQuickWindow *window = nullptr,
                                     const QString &parentToken = {});
    Q_INVOKABLE bool activate(const QString &token);
    Q_INVOKABLE bool deactivate(const QString &token);
    Q_INVOKABLE bool closeSurface(const QString &token);
    Q_INVOKABLE bool contains(const QString &token) const;
    QString activeToken() const;

signals:
    void changed();
    void surfaceClosed(const QString &token);

private:
    struct Frame {
        QString token;
        QString parentToken;
        QPointer<QObject> owner;
        QPointer<QQuickItem> target;
        QPointer<QQuickWindow> window;
        QPointer<QQuickItem> savedItem;
        QPointer<QQuickWindow> savedWindow;
        bool active = false;
    };
    qsizetype indexOf(const QString &token) const;
    QQuickWindow *windowFor(const Frame &frame) const;
    QQuickItem *focusedItem(const Frame &frame) const;
    bool isWithinClosingTree(QQuickItem *item, const QVector<QString> &tokens) const;
    void restoreIfSafe(const Frame &frame, const QVector<QString> &closingTokens);
    QVector<Frame> m_frames;
};

} // namespace QindaTK
