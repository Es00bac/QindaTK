// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class SlideDeck;
class SlideShow;

// Paints slide output with the shared slide painter (slide_render.h). With
// `show` set it follows the live presentation frame (audience output and
// presenter preview); otherwise it paints `deck` at `slideId`/`step`/
// `ticks` (a static preview; step -1 means every build complete).
// AGENT-GUARD: this item paints only what a SlideFrame carries; it has no
// notes property, so an audience window built from it cannot leak notes.
class SlideFrameView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("slide_deck.h")
    Q_MOC_INCLUDE("slide_show.h")
    Q_PROPERTY(QindaTK::SlideShow *show READ show WRITE setShow NOTIFY sourceChanged)
    Q_PROPERTY(QindaTK::SlideDeck *deck READ deck WRITE setDeck NOTIFY sourceChanged)
    Q_PROPERTY(QString slideId READ slideId WRITE setSlideId NOTIFY sourceChanged)
    Q_PROPERTY(int step READ step WRITE setStep NOTIFY sourceChanged)
    Q_PROPERTY(QString ticks READ ticks WRITE setTicks NOTIFY sourceChanged)
    Q_PROPERTY(QString displayedSlideId READ displayedSlideId NOTIFY painted)
    Q_PROPERTY(int paintCount READ paintCount NOTIFY painted)
public:
    explicit SlideFrameView(QQuickItem *parent = nullptr);
    SlideShow *show() const { return m_show; }
    void setShow(SlideShow *show);
    SlideDeck *deck() const { return m_deck; }
    void setDeck(SlideDeck *deck);
    QString slideId() const { return m_slideId; }
    void setSlideId(const QString &id);
    int step() const { return m_step; }
    void setStep(int step);
    QString ticks() const { return m_ticks; }
    void setTicks(const QString &ticks);
    QString displayedSlideId() const { return m_displayed; }
    int paintCount() const { return m_paintCount; }
    void paint(QPainter *painter) override;
    // The exact image paint() would produce at `size` (tests and captures).
    Q_INVOKABLE QImage grabFrame(int width, int height) const;
signals:
    void sourceChanged();
    void painted();
private:
    void reconnect();
    QPointer<SlideShow> m_show;
    QPointer<SlideDeck> m_deck;
    QString m_slideId;
    int m_step = -1;
    QString m_ticks = QStringLiteral("0");
    QString m_displayed;
    int m_paintCount = 0;
    QList<QMetaObject::Connection> m_connections;
};

} // namespace QindaTK
