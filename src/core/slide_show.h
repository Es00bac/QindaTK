// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "slide_timeline.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class SlideDeck;

// Deterministic presentation playback over a SlideDeck (docs/slides.md#playback).
// The position is (slide, build step, ticks into the step); frame() is a
// pure evaluation of that position, so the same position always gives the
// same audience output. Time only moves through advance(): a live show
// (liveClock) calls it from a frame timer with measured monotonic time, a
// test or an exporter calls it with exact tick counts. Navigation skips
// hidden slides; goToSlide can still show one explicitly.
// AGENT-CONTRACT: audience outputs read frame()/currentFrame() only; the
// presenter additionally reads speakerNotes. A frame never carries notes.
class SlideShow : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("slide_deck.h")
    Q_PROPERTY(QindaTK::SlideDeck *deck READ deck WRITE setDeck NOTIFY deckChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool ended READ ended NOTIFY stateChanged)
    Q_PROPERTY(bool blackedOut READ blackedOut WRITE setBlackedOut NOTIFY stateChanged)
    Q_PROPERTY(bool liveClock READ liveClock WRITE setLiveClock NOTIFY stateChanged)
    Q_PROPERTY(QString currentSlideId READ currentSlideId NOTIFY positionChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY positionChanged)
    Q_PROPERTY(int slideCount READ slideCount NOTIFY positionChanged)
    Q_PROPERTY(QString previousSlideId READ previousSlideId NOTIFY positionChanged)
    Q_PROPERTY(QString nextSlideId READ nextSlideId NOTIFY positionChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY positionChanged)
    Q_PROPERTY(QString nextTitle READ nextTitle NOTIFY positionChanged)
    Q_PROPERTY(QString speakerNotes READ speakerNotes NOTIFY positionChanged)
    Q_PROPERTY(int buildStep READ buildStep NOTIFY positionChanged)
    Q_PROPERTY(int buildCount READ buildCount NOTIFY positionChanged)
    Q_PROPERTY(QString stepTicks READ stepTicksText NOTIFY frameChanged)
    Q_PROPERTY(bool animating READ animating NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap frame READ frame NOTIFY frameChanged)
    Q_PROPERTY(QString elapsedTicks READ elapsedTicksText NOTIFY clockChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY clockChanged)
    Q_PROPERTY(QString timerState READ timerState NOTIFY clockChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    // Presenter annotation commands (OFF-23): "pointer", "pen" or "highlight".
    Q_PROPERTY(QString annotationTool READ annotationTool WRITE setAnnotationTool NOTIFY annotationsChanged)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY annotationsChanged)
    Q_PROPERTY(bool pointerVisible READ pointerVisible NOTIFY annotationsChanged)
public:
    explicit SlideShow(QObject *parent = nullptr);
    SlideDeck *deck() const { return m_deck; }
    void setDeck(SlideDeck *deck);
    bool running() const { return m_running; }
    bool ended() const { return m_ended; }
    bool blackedOut() const { return m_blackedOut; }
    void setBlackedOut(bool blackedOut);
    bool liveClock() const { return m_liveClock; }
    void setLiveClock(bool live);
    QString currentSlideId() const { return m_slideId; }
    int currentIndex() const;
    int slideCount() const { return shownSlides().size(); }
    QString previousSlideId() const;
    QString nextSlideId() const;
    QString currentTitle() const;
    QString nextTitle() const;
    QString speakerNotes() const;
    int buildStep() const { return m_step; }
    int buildCount() const;
    qint64 stepTicks() const { return m_ticks; }
    QString stepTicksText() const { return QString::number(m_ticks); }
    bool animating() const;
    QVariantMap frame() const { return Slides::frameToVariant(currentFrame()); }
    Slides::SlideFrame currentFrame() const;
    qint64 elapsedTicks() const { return m_elapsed; }
    QString elapsedTicksText() const { return QString::number(m_elapsed); }
    QString elapsedText() const;
    QString timerState() const { return m_timerState; }
    QString lastError() const { return m_error; }
    static constexpr int MaximumStrokes = 500;         // per slide
    static constexpr int MaximumStrokePoints = 4096;
    QString annotationTool() const { return m_annotationTool; }
    void setAnnotationTool(const QString &tool);
    int annotationCount() const { return int(m_annotations.value(m_slideId).size()); }
    bool pointerVisible() const { return m_pointerVisible; }

    Q_INVOKABLE bool start(const QString &slideId = {});
    Q_INVOKABLE void stop();
    // Next build; completes a running build first; then the next shown slide.
    Q_INVOKABLE bool next();
    // Previous build (shown complete); then the previous slide, complete.
    Q_INVOKABLE bool previous();
    Q_INVOKABLE bool goToSlide(const QString &slideId);
    Q_INVOKABLE bool goToIndex(int index);
    // Exact time step, decimal ticks (>= 0).
    Q_INVOKABLE bool advance(const QString &ticks);
    Q_INVOKABLE bool advanceMilliseconds(int milliseconds);
    bool advanceTicks(qint64 ticks);
    // "start" (resumes when paused), "pause", "resume", "reset".
    Q_INVOKABLE bool timerAction(const QString &action);
    Q_INVOKABLE QImage renderFrame(int width, int height) const;
    // Ink on the current slide with the pen or highlight tool; points are
    // slide pixels ({x, y} maps or points). Kept per slide until the show
    // stops; visible in every output of the show, never saved or exported.
    Q_INVOKABLE bool addAnnotationStroke(const QVariantList &points);
    Q_INVOKABLE void clearAnnotations();
    // The pointer tool's dot at a slide point; hidden on navigation.
    Q_INVOKABLE bool setPointer(qreal x, qreal y);
    Q_INVOKABLE void hidePointer();

signals:
    void deckChanged();
    void stateChanged();
    void positionChanged();
    void frameChanged();
    void clockChanged();
    void finished();
    void annotationsChanged();

private:
    QStringList shownSlides() const;
    // By value: callers pass m_slideId as `from` while slideId is being replaced.
    void place(QString slideId, int step, qint64 ticks, QString from);
    void deckContentChanged();
    void updateTimer();
    void tick();
    bool fail(const QString &message);
    QPointer<SlideDeck> m_deck;
    QMetaObject::Connection m_deckConnection;
    QString m_slideId;
    QString m_fromSlideId;
    int m_step = 0;
    qint64 m_ticks = 0;
    qint64 m_elapsed = 0;
    QString m_timerState = QStringLiteral("stopped");
    bool m_running = false;
    bool m_ended = false;
    bool m_blackedOut = false;
    bool m_liveClock = true;
    QTimer m_frameTimer;
    QElapsedTimer m_monotonic;
    qint64 m_lastNanoseconds = 0;
    QString m_error;
    QString m_annotationTool = QStringLiteral("pointer");
    QHash<QString, QVector<Slides::AnnotationStroke>> m_annotations;
    bool m_pointerVisible = false;
    QPointF m_pointer;
};

} // namespace QindaTK
