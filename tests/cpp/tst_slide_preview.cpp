// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-056 / OFF-22 and OFF-23: the AnimationPane preview (SlidePreview),
// exported playback (SlideDeck::exportPlayback) and the live show agree on
// sequence and cancellation state; presenter annotation commands draw on
// the audience output without touching the deck (docs/slides.md).
#include "slide_deck.h"
#include "slide_preview.h"
#include "slide_show.h"
#include "slide_timeline.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantMap box(double x, double y, double w, double h)
{
    return {{"x", x}, {"y", y}, {"width", w}, {"height", h}};
}
constexpr qint64 Tps = Slides::TicksPerSecond;

// One click build: a fade (0.5 s), a fly-in "with" it after 0.25 s (1 s)
// and a zoom "after" the fly-in (0.2 s); a second slide follows.
struct Fixture {
    SlideDeck deck;
    QString slide, next, fade, fly, zoom, fadeEffect, flyEffect, zoomEffect;
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
        next = deck.insertSlide(slide);
    }
    QImage at(int step, qint64 ticks) const
    {
        return deck.renderFrame(slide, step, QString::number(ticks), 960, 540);
    }
};
} // namespace

class SlidePreviewTest : public QObject {
    Q_OBJECT
private slots:
    void previewShowAndExportShareSequenceAndCancellation()
    {
        Fixture f;
        SlidePreview preview;
        preview.setLiveClock(false);
        preview.setDeck(&f.deck);
        QCOMPARE(preview.state(), QStringLiteral("idle"));
        QCOMPARE(preview.step(), -1);
        // Previewing the "with" fly-in plays its whole build step from 0.
        QVERIFY(preview.start(f.flyEffect));
        QCOMPARE(preview.state(), QStringLiteral("running"));
        QCOMPARE(preview.slideId(), f.slide);
        QCOMPARE(preview.step(), 1);
        QCOMPARE(preview.stepEndText(), QStringLiteral("1023120000"));

        SlideShow show;
        show.setLiveClock(false);
        show.setDeck(&f.deck);
        QVERIFY(show.start(f.slide));
        QVERIFY(show.next()); // the click build
        for (const qint64 t : {Tps / 4, Tps / 2, Tps * 3 / 4}) {
            QVERIFY(preview.advanceTicks(t - preview.ticks()));
            QVERIFY(show.advanceTicks(t - show.stepTicks()));
            QCOMPARE(f.at(preview.step(), preview.ticks()), show.renderFrame(960, 540));
        }
        // Cancel mid-build: the build shows complete, exactly as a presenter
        // click during a running build leaves the show.
        QSignalSpy finished(&preview, &SlidePreview::finished);
        preview.cancel();
        QCOMPARE(preview.state(), QStringLiteral("canceled"));
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(preview.ticks(), qint64(1023120000));
        QVERIFY(show.animating());
        QVERIFY(show.next());
        QVERIFY(!show.animating());
        const QImage completed = show.renderFrame(960, 540);
        QCOMPARE(f.at(preview.step(), preview.ticks()), completed);

        // Exported playback: the same positions, closed by the same frame.
        QTemporaryDir dir;
        const QVector<Slides::PlaybackPosition> positions = Slides::playbackPositions(f.deck.state(), f.slide, 25);
        const int frames = f.deck.exportPlayback(f.slide, dir.path(), 25);
        QCOMPARE(frames, int(positions.size()));
        // Step 0 is a single frame (no transition or entry animation), then
        // the build: 1.45 s at 25 fps is 37 samples plus its completed frame.
        QCOMPARE(frames, 1 + 37 + 1);
        QCOMPARE(positions.last().step, 1);
        QCOMPARE(positions.last().ticks, qint64(1023120000));
        const QImage last(QDir(dir.path()).filePath(QStringLiteral("frame-%1.png").arg(frames - 1, 5, 10, QLatin1Char('0'))));
        QCOMPARE(last.convertToFormat(QImage::Format_RGB32), completed.convertToFormat(QImage::Format_RGB32));
        // Sample 8 of the build (0.32 s, file 9) equals the preview at that tick.
        const QImage sampled(QDir(dir.path()).filePath(QStringLiteral("frame-00009.png")));
        QCOMPARE(positions.at(9).ticks, Tps * 8 / 25);
        QVERIFY(preview.start(f.flyEffect));
        QVERIFY(preview.advanceTicks(Tps * 8 / 25));
        QCOMPARE(sampled.convertToFormat(QImage::Format_RGB32),
                 f.at(preview.step(), preview.ticks()).convertToFormat(QImage::Format_RGB32));
        QCOMPARE(f.deck.exportPlayback(f.slide, dir.path(), 0), -1);
    }

    void previewStatesFollowTheDeck()
    {
        Fixture f;
        SlidePreview preview;
        preview.setLiveClock(false);
        preview.setDeck(&f.deck);
        QVERIFY(!preview.start(QStringLiteral("effect-missing")));
        QVERIFY(!preview.lastError().isEmpty());
        QVERIFY(preview.start(f.zoomEffect));
        QVERIFY(preview.advance(QStringLiteral("5000000000")));
        QCOMPARE(preview.state(), QStringLiteral("finished"));
        QCOMPARE(preview.ticks(), qint64(1023120000));
        preview.reset();
        QCOMPARE(preview.step(), -1);
        // Editing the deck while a preview runs stops it at the complete build.
        QVERIFY(preview.start(f.fadeEffect));
        QVERIFY(f.deck.editEffect(f.fadeEffect, {{"duration", 0.6}}));
        QCOMPARE(preview.state(), QStringLiteral("canceled"));
        // A live preview advances by itself.
        SlidePreview live;
        live.setDeck(&f.deck);
        QVERIFY(live.start(f.zoomEffect));
        QTRY_COMPARE_WITH_TIMEOUT(live.state(), QStringLiteral("finished"), 5000);
    }

    void annotationsReachTheAudienceButNotTheDeck()
    {
        Fixture f;
        SlideShow show;
        show.setLiveClock(false);
        show.setDeck(&f.deck);
        QVERIFY(show.start(f.next));
        const QString saved = f.deck.toJson();
        const QImage clean = show.renderFrame(960, 540);
        QVERIFY(!show.addAnnotationStroke({QVariantMap{{"x", 10}, {"y", 10}}})); // pointer does not draw
        show.setAnnotationTool(QStringLiteral("pen"));
        QVERIFY(show.addAnnotationStroke({QVariantMap{{"x", 100}, {"y", 270}}, QVariantMap{{"x", 860}, {"y", 270}}}));
        QCOMPARE(show.annotationCount(), 1);
        QCOMPARE(show.frame().value("annotationCount").toInt(), 1);
        const QImage inked = show.renderFrame(960, 540);
        QVERIFY(inked != clean);
        const QColor ink = inked.pixelColor(480, 270);
        QVERIFY2(ink.red() > 180 && ink.green() < 120, qPrintable(ink.name()));
        QVERIFY(!show.addAnnotationStroke({QVariantMap{{"x", qQNaN()}, {"y", 1}}}));

        // Ink stays with its slide during the show, never in the document.
        QVERIFY(show.goToSlide(f.slide));
        QCOMPARE(show.annotationCount(), 0);
        QVERIFY(show.goToSlide(f.next));
        QCOMPARE(show.annotationCount(), 1);
        QCOMPARE(f.deck.toJson(), saved);
        QCOMPARE(f.deck.renderFrame(f.next, -1, "0", 960, 540), clean);

        // The pointer shows on the output and hides on navigation.
        show.setAnnotationTool(QStringLiteral("pointer"));
        QVERIFY(show.setPointer(200, 200));
        QVERIFY(show.pointerVisible());
        QVERIFY(show.renderFrame(960, 540).pixelColor(200, 200).red() > 180);
        QVERIFY(show.previous());
        QVERIFY(!show.pointerVisible());
        QVERIFY(show.goToSlide(f.next));
        show.clearAnnotations();
        QCOMPARE(show.annotationCount(), 0);
        QCOMPARE(show.renderFrame(960, 540), clean);
        show.setAnnotationTool(QStringLiteral("highlight"));
        QVERIFY(show.addAnnotationStroke({QVariantMap{{"x", 1}, {"y", 1}}}));
        show.stop();
        QVERIFY(show.start(f.next));
        QCOMPARE(show.annotationCount(), 0); // ink ends with the show
    }
};

QTEST_MAIN(SlidePreviewTest)
#include "tst_slide_preview.moc"
