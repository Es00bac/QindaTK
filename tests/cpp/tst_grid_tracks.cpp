// SPDX-License-Identifier: LGPL-3.0-or-later
#include "grid_tracks.h"

#include <QtTest>

using namespace QindaTK;

class TstGridTracks final : public QObject {
    Q_OBJECT
private slots:
    void parsesPlainList()
    {
        const auto tracks = parseGridTracks(QStringLiteral("200 1fr auto 25% 40px"));
        QCOMPARE(tracks.size(), 5);
        QCOMPARE(tracks[0].min.kind, GridLength::Fixed);
        QCOMPARE(tracks[0].min.value, 200.0);
        QCOMPARE(tracks[1].min.kind, GridLength::Fixed);
        QCOMPARE(tracks[1].min.value, 0.0);
        QCOMPARE(tracks[1].max.kind, GridLength::Fr);
        QCOMPARE(tracks[2].max.kind, GridLength::Auto);
        QCOMPARE(tracks[3].max.kind, GridLength::Percent);
        QCOMPARE(tracks[4].max.value, 40.0);
    }
    void parsesRepeatAndMinmax()
    {
        QString error;
        const auto tracks = parseGridTracks(QStringLiteral("repeat(2, minmax(80, 1fr) auto) 12"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(tracks.size(), 5);
        QCOMPARE(tracks[0].min.value, 80.0);
        QCOMPARE(tracks[0].max.kind, GridLength::Fr);
        QCOMPARE(tracks[1].max.kind, GridLength::Auto);
        QCOMPARE(tracks[2].min.value, 80.0);
        QCOMPARE(tracks[4].max.value, 12.0);
    }
    void reportsErrors()
    {
        QString error;
        (void)parseGridTracks(QStringLiteral("1fr bogus"), &error);
        QVERIFY(!error.isEmpty());
        (void)parseGridTracks(QStringLiteral("repeat(2, 1fr"), &error);
        QVERIFY(!error.isEmpty());
    }
    void fixedAndFr()
    {
        const auto tracks = parseGridTracks(QStringLiteral("200 1fr"));
        const auto sizes = sizeGridTracks(tracks, {}, 500, 0);
        QCOMPARE(sizes.sizes, (QVector<qreal>{200, 300}));
    }
    void frShares()
    {
        const auto tracks = parseGridTracks(QStringLiteral("1fr 2fr"));
        const auto sizes = sizeGridTracks(tracks, {}, 330, 30);
        QCOMPARE(sizes.sizes, (QVector<qreal>{100, 200}));
    }
    void autoTakesContent()
    {
        const auto tracks = parseGridTracks(QStringLiteral("auto 1fr"));
        const auto sizes = sizeGridTracks(tracks, {{0, 1, 64}, {1, 1, 500}}, 400, 8);
        QCOMPARE(sizes.sizes, (QVector<qreal>{64, 328}));
        // fr has a 0 minimum: content wider than the share does not grow it.
        QCOMPARE(sizes.natural, (QVector<qreal>{64, 500}));
    }
    void minmaxFloorsFr()
    {
        const auto tracks = parseGridTracks(QStringLiteral("minmax(150, 1fr) 1fr"));
        const auto sizes = sizeGridTracks(tracks, {}, 200, 0);
        QCOMPARE(sizes.sizes, (QVector<qreal>{150, 50}));
    }
    void indefiniteCollapsesFrToContent()
    {
        const auto tracks = parseGridTracks(QStringLiteral("1fr auto"));
        const auto sizes = sizeGridTracks(tracks, {{0, 1, 120}, {1, 1, 30}}, 0, 0);
        QCOMPARE(sizes.sizes, (QVector<qreal>{120, 30}));
    }
    void spanningItemGrowsAutoTracks()
    {
        const auto tracks = parseGridTracks(QStringLiteral("auto auto"));
        const auto sizes = sizeGridTracks(tracks, {{0, 2, 100}}, 0, 0);
        QCOMPARE(sizes.sizes, (QVector<qreal>{50, 50}));
    }
    void stretchesAutoWithoutFr()
    {
        const auto tracks = parseGridTracks(QStringLiteral("auto auto"));
        const auto sizes = sizeGridTracks(tracks, {{0, 1, 20}, {1, 1, 40}}, 100, 0);
        QCOMPARE(sizes.sizes, (QVector<qreal>{40, 60}));
    }
    void percentTracks()
    {
        const auto tracks = parseGridTracks(QStringLiteral("25% 1fr"));
        const auto sizes = sizeGridTracks(tracks, {}, 400, 0);
        QCOMPARE(sizes.sizes, (QVector<qreal>{100, 300}));
    }
};

QTEST_GUILESS_MAIN(TstGridTracks)
#include "tst_grid_tracks.moc"
