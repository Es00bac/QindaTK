// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-056: exact-tick animation schedules, sampled animation frames, the
// shared slide painter and deterministic SlideShow navigation (docs/slides.md).
#include "slide_deck.h"
#include "slide_frame_view.h"
#include "slide_show.h"
#include "slide_timeline.h"

#include <QJsonDocument>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantMap box(double x, double y, double w, double h)
{
    return {{"x", x}, {"y", y}, {"width", w}, {"height", h}};
}
Slides::ObjectFrame stateOf(const Slides::SlideFrame &frame, const QString &id)
{
    for (const Slides::ObjectFrame &o : frame.objects) if (o.id == id) return o;
    return {};
}
constexpr qint64 Tps = Slides::TicksPerSecond; // 705,600,000

// Slide 1: a fade (click, 0.5 s), a fly-in "with" it after 0.25 s (1 s) and
// a zoom "after" the fly-in (0.2 s), all linear, plus a 0.3 s fade transition.
struct Fixture {
    SlideDeck deck;
    QString slide, fade, fly, zoom, fadeEffect, flyEffect, zoomEffect;
    Fixture()
    {
        slide = deck.slideIds().first();
        fade = deck.insertObject(slide, "shape", box(100, 100, 200, 100));
        fly = deck.insertObject(slide, "shape", box(400, 300, 100, 100));
        zoom = deck.insertObject(slide, "shape", box(600, 100, 100, 100));
        deck.setObjectFill(fade, "#ff0000");
        fadeEffect = deck.addEffect(fade, "fade-in");
        flyEffect = deck.addEffect(fly, "fly-in");
        zoomEffect = deck.addEffect(zoom, "zoom-in");
        deck.editEffect(fadeEffect, {{"trigger", "click"}, {"duration", 0.5}, {"easing", "linear"}});
        deck.editEffect(flyEffect, {{"trigger", "with"}, {"delay", 0.25}, {"duration", 1.0}, {"easing", "linear"}});
        deck.editEffect(zoomEffect, {{"trigger", "after"}, {"duration", 0.2}, {"easing", "linear"}});
        deck.setTransition(slide, "fade", 0.3);
    }
};
} // namespace

class SlidePlaybackTest : public QObject {
    Q_OBJECT
private slots:
    void scheduleUsesExactTicks()
    {
        Fixture f;
        QVERIFY(f.deck.lastError().isEmpty());
        const QVariantMap plan = f.deck.schedule(f.slide);
        QCOMPARE(plan.value("buildCount").toInt(), 1);
        QCOMPARE(plan.value("transitionTicks").toString(), QString::number(Tps * 3 / 10));
        QCOMPARE(plan.value("stepDurations").toStringList(),
                 (QStringList{QString::number(Tps * 3 / 10), QStringLiteral("1023120000")}));
        const QVariantList effects = plan.value("effects").toList();
        QCOMPARE(effects.size(), 3);
        QCOMPARE(effects.at(0).toMap().value("end").toString(), QStringLiteral("352800000"));
        QCOMPARE(effects.at(1).toMap().value("start").toString(), QStringLiteral("176400000"));
        QCOMPARE(effects.at(1).toMap().value("end").toString(), QStringLiteral("882000000"));
        QCOMPARE(effects.at(2).toMap().value("start").toString(), QStringLiteral("882000000"));
        QCOMPARE(effects.at(2).toMap().value("end").toString(), QStringLiteral("1023120000"));
        for (const QVariant &e : effects) QCOMPARE(e.toMap().value("step").toInt(), 1);
    }

    void sampledFramesAreExactAndDeterministic()
    {
        Fixture f;
        const Slides::DeckState &deck = f.deck.state();
        // Before the click every entry effect's target is hidden.
        Slides::SlideFrame frame = Slides::evaluate(deck, f.slide, 0, 0);
        QCOMPARE(stateOf(frame, f.fade).opacity, 0.0);
        QCOMPARE(stateOf(frame, f.fly).opacity, 0.0);
        QCOMPARE(stateOf(frame, f.zoom).opacity, 0.0);

        // 0.25 s into the build: the fade is half done, the fly-in starts.
        frame = Slides::evaluate(deck, f.slide, 1, Tps / 4);
        QCOMPARE(stateOf(frame, f.fade).opacity, 0.5);
        QCOMPARE(stateOf(frame, f.fly).opacity, 1.0);
        QCOMPARE(stateOf(frame, f.fly).dy, 540.0 - 300.0);
        QCOMPARE(stateOf(frame, f.zoom).opacity, 0.0);

        // 0.75 s: fade complete, fly-in half way, zoom not yet started.
        frame = Slides::evaluate(deck, f.slide, 1, Tps * 3 / 4);
        QCOMPARE(stateOf(frame, f.fade).opacity, 1.0);
        QCOMPARE(stateOf(frame, f.fly).dy, 120.0);
        QCOMPARE(stateOf(frame, f.zoom).opacity, 0.0);

        // 0.1 s into the zoom (half of 0.2 s).
        frame = Slides::evaluate(deck, f.slide, 1, 882000000 + Tps / 10);
        QCOMPARE(stateOf(frame, f.fly).dy, 0.0);
        QCOMPARE(stateOf(frame, f.zoom).scale, 0.5);
        QCOMPARE(stateOf(frame, f.zoom).opacity, 1.0);

        // The QML map carries the same numbers and never any notes.
        QVERIFY(f.deck.setNotes(f.slide, QStringLiteral("Private: mention the budget")));
        const QVariantMap map = f.deck.frame(f.slide, 1, QString::number(Tps / 4));
        QCOMPARE(map.value("ticks").toString(), QStringLiteral("176400000"));
        QVERIFY(!map.contains("notes"));
        QVERIFY(!QString::fromUtf8(QJsonDocument::fromVariant(map).toJson()).contains(QStringLiteral("budget")));

        // Eased curves go through CurveEvaluator: symmetric ease-in-out.
        QVERIFY(qAbs(Slides::ease("in-out-cubic", 0.5) - 0.5) < 1e-9);
        QVERIFY(Slides::ease("in-cubic", 0.5) < 0.5 && Slides::ease("out-cubic", 0.5) > 0.5);
        QCOMPARE(Slides::ease("out-cubic", 0.0), 0.0);
        QCOMPARE(Slides::ease("out-cubic", 1.0), 1.0);

        // Rendering the same position twice gives identical pixels; the
        // half-faded red shape blends 50% over the white background.
        const QImage a = f.deck.renderFrame(f.slide, 1, QString::number(Tps / 4), 960, 540);
        const QImage b = f.deck.renderFrame(f.slide, 1, QString::number(Tps / 4), 960, 540);
        QVERIFY(!a.isNull());
        QCOMPARE(a, b);
        const QColor pixel = a.pixelColor(200, 150);
        QCOMPARE(pixel.red(), 255);
        QVERIFY2(qAbs(pixel.green() - 128) <= 2 && qAbs(pixel.blue() - 128) <= 2,
                 qPrintable(pixel.name()));
        const QImage done = f.deck.renderFrame(f.slide, 1, QStringLiteral("1023120000"), 960, 540);
        QCOMPARE(done.pixelColor(200, 150), QColor(255, 0, 0));
        // Letterboxing keeps the aspect ratio: a square target has black bars.
        const QImage square = f.deck.renderFrame(f.slide, 1, QStringLiteral("1023120000"), 400, 400);
        QCOMPARE(square.pixelColor(200, 5), QColor(Qt::black));
    }

    void showNavigatesBuildsSlidesAndTransitions()
    {
        Fixture f;
        const QString hidden = f.deck.insertSlide(f.slide);
        const QString last = f.deck.insertSlide(hidden);
        QVERIFY(f.deck.setSlideHidden(hidden, true));
        QVERIFY(f.deck.setTransition(last, "push", 0.5));
        QVERIFY(f.deck.setNotes(last, QStringLiteral("Closing notes")));
        SlideShow show;
        show.setLiveClock(false);
        show.setDeck(&f.deck);
        QSignalSpy finished(&show, &SlideShow::finished);
        QVERIFY(show.start());
        QCOMPARE(show.currentSlideId(), f.slide);
        QCOMPARE(show.slideCount(), 2);
        QCOMPARE(show.buildCount(), 1);
        QCOMPARE(show.nextSlideId(), last); // hidden slides are skipped

        // Step 0 is the (absent) transition; the click starts build 1.
        QVERIFY(show.advanceMilliseconds(300));
        QVERIFY(show.next());
        QCOMPARE(show.buildStep(), 1);
        QCOMPARE(show.stepTicks(), qint64(0));
        QVERIFY(show.advanceMilliseconds(250));
        QCOMPARE(show.stepTicks(), Tps / 4);
        QCOMPARE(stateOf(show.currentFrame(), f.fade).opacity, 0.5);
        QVERIFY(show.animating());
        QVERIFY(show.next()); // completes the running build
        QCOMPARE(show.stepTicks(), qint64(1023120000));
        QVERIFY(!show.animating());

        QVERIFY(show.next());
        QCOMPARE(show.currentSlideId(), last);
        QCOMPARE(show.currentIndex(), 1);
        QCOMPARE(show.speakerNotes(), QStringLiteral("Closing notes"));
        Slides::SlideFrame frame = show.currentFrame();
        QCOMPARE(frame.transitionKind, QStringLiteral("push"));
        QCOMPARE(frame.fromSlideId, f.slide);
        QCOMPARE(frame.transitionProgress, 0.0);
        QVERIFY(show.advanceMilliseconds(250));
        QCOMPARE(show.currentFrame().transitionProgress, 0.5);
        QVERIFY(show.advanceMilliseconds(1000)); // saturates at the step end
        QCOMPARE(show.stepTicks(), Tps / 2);
        QCOMPARE(show.currentFrame().transitionKind, QStringLiteral("none"));

        QVERIFY(!show.next());
        QVERIFY(show.ended());
        QCOMPARE(finished.count(), 1);
        QVERIFY(show.currentFrame().blackedOut);
        QVERIFY(show.previous()); // back from the end
        QVERIFY(show.previous());
        QCOMPARE(show.currentSlideId(), f.slide);
        QCOMPARE(show.buildStep(), 1); // the previous slide shows complete
        QCOMPARE(stateOf(show.currentFrame(), f.zoom).scale, 1.0);

        // Explicit navigation can show a hidden slide.
        QVERIFY(show.goToSlide(hidden));
        QCOMPARE(show.currentSlideId(), hidden);
        QVERIFY(show.goToIndex(1));
        QCOMPARE(show.currentSlideId(), last);
    }

    void clockBlankingAndLiveEdits()
    {
        Fixture f;
        SlideShow show;
        show.setLiveClock(false);
        show.setDeck(&f.deck);
        QVERIFY(show.start());
        QCOMPARE(show.timerState(), QStringLiteral("running"));
        QVERIFY(show.advanceMilliseconds(61000));
        QCOMPARE(show.elapsedText(), QStringLiteral("01:01"));
        QVERIFY(show.timerAction("pause"));
        QVERIFY(show.advanceMilliseconds(5000));
        QCOMPARE(show.elapsedTicks(), Tps * 61);
        QVERIFY(show.timerAction("start")); // resumes
        QVERIFY(show.advanceMilliseconds(1000));
        QCOMPARE(show.elapsedText(), QStringLiteral("01:02"));
        QVERIFY(show.timerAction("reset"));
        QCOMPARE(show.elapsedText(), QStringLiteral("00:00"));
        QVERIFY(!show.timerAction("rewind"));

        show.setBlackedOut(true);
        QVERIFY(show.currentFrame().blackedOut);
        const QImage black = show.renderFrame(96, 54);
        QCOMPARE(black.pixelColor(48, 27), QColor(Qt::black));
        show.setBlackedOut(false);

        // Deleting the shown slide continues at the first shown slide.
        const QString added = f.deck.insertSlide(f.slide);
        QVERIFY(show.goToSlide(added));
        QVERIFY(f.deck.removeSlide(added));
        QCOMPARE(show.currentSlideId(), f.slide);
        QVERIFY(!show.advance(QStringLiteral("-1")));

        // The live clock moves time from monotonic nanoseconds.
        SlideShow live;
        live.setDeck(&f.deck);
        QVERIFY(live.start());
        QTRY_VERIFY(live.elapsedTicks() > 0);
        live.stop();
        QCOMPARE(live.timerState(), QStringLiteral("stopped"));
    }

    void frameViewPaintsTheShowFrame()
    {
        Fixture f;
        SlideShow show;
        show.setLiveClock(false);
        show.setDeck(&f.deck);
        QVERIFY(show.start());
        QVERIFY(show.next());
        QVERIFY(show.advanceMilliseconds(250));
        SlideFrameView view;
        view.setShow(&show);
        QCOMPARE(view.grabFrame(480, 270), show.renderFrame(480, 270));
        SlideFrameView still;
        still.setDeck(&f.deck);
        still.setSlideId(f.slide);
        QCOMPARE(still.grabFrame(480, 270),
                 f.deck.renderFrame(f.slide, 1, QStringLiteral("1023120000"), 480, 270)); // step -1: complete
    }
};

QTEST_MAIN(SlidePlaybackTest)
#include "tst_slide_playback.moc"
