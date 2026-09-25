// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class SlideDeck;

// AGENT-CONTRACT (docs/slides.md#preview, OFF-22): the AnimationPane's
// preview. start(effectId) plays the build step that holds the effect from
// its start, through the same Slides::evaluate the show and the exported
// playback use, so the same ticks give the same pixels. Bind a
// Tk.SlideFrameView to `deck`, `slideId`, `step` and `ticks`.
// state: idle (no preview; step -1 shows the slide complete), running,
// finished (the step's end), canceled. AGENT-GUARD: cancel() lands on the
// step's completed frame, the same state SlideShow.next() gives when the
// presenter clicks during a running build and the last frame
// SlideDeck.exportPlayback writes for that step.
class SlidePreview : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("slide_deck.h")
    Q_PROPERTY(QindaTK::SlideDeck *deck READ deck WRITE setDeck NOTIFY changed)
    Q_PROPERTY(bool liveClock READ liveClock WRITE setLiveClock NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString effectId READ effectId NOTIFY changed)
    Q_PROPERTY(QString slideId READ slideId NOTIFY changed)
    Q_PROPERTY(int step READ step NOTIFY changed)
    Q_PROPERTY(QString ticks READ ticksText NOTIFY frameChanged)
    Q_PROPERTY(QString stepEndTicks READ stepEndText NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit SlidePreview(QObject *parent = nullptr);
    SlideDeck *deck() const { return m_deck; }
    void setDeck(SlideDeck *deck);
    bool liveClock() const { return m_liveClock; }
    void setLiveClock(bool live);
    QString state() const { return m_state; }
    QString effectId() const { return m_effectId; }
    QString slideId() const { return m_slideId; }
    int step() const { return m_state == QStringLiteral("idle") ? -1 : m_step; }
    qint64 ticks() const { return m_ticks; }
    QString ticksText() const { return QString::number(m_ticks); }
    QString stepEndText() const { return QString::number(m_end); }
    QString lastError() const { return m_error; }

    Q_INVOKABLE bool start(const QString &effectId);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();
    // Exact time step in decimal ticks (tests, exporters, the live clock).
    Q_INVOKABLE bool advance(const QString &ticks);
    bool advanceTicks(qint64 ticks);

signals:
    void changed();
    void frameChanged();
    void finished(bool canceled);

private:
    void setState(const QString &state);
    void tick();
    bool fail(const QString &message);
    QPointer<SlideDeck> m_deck;
    QMetaObject::Connection m_deckConnection;
    QString m_state = QStringLiteral("idle");
    QString m_effectId;
    QString m_slideId;
    int m_step = 0;
    qint64 m_ticks = 0;
    qint64 m_end = 0;
    bool m_liveClock = true;
    QTimer m_timer;
    QElapsedTimer m_monotonic;
    qint64 m_lastNanoseconds = 0;
    QString m_error;
};

} // namespace QindaTK
