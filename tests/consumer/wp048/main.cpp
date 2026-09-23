// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QUrl>

#include <qindatk/timeline_model.h>
#include <qindatk/timeline_types.h>

namespace {
bool check(bool good, const char *what)
{
    if (!good) qCritical("WP-048 installed consumer: %s", what);
    return good;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickView window;
    window.engine()->addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setSource(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (!check(window.status() == QQuickView::Ready && window.rootObject(),
               "installed QML import and component creation")) return 1;

    auto *root = window.rootObject();
    auto *model = root->findChild<QindaTK::TimelineModel *>(
        QStringLiteral("installedTimelineModel"));
    auto *timeline = root->findChild<QQuickItem *>(QStringLiteral("installedEditTimeline"));
    if (!check(model && timeline, "installed TimelineModel and EditTimeline")) return 2;
    if (!check(timeline->property("model").value<QObject *>() == model,
               "EditTimeline binds to the public TimelineModel")) return 3;

    const QUuid trackId(QStringLiteral("{4c478a21-36bd-4a31-98d6-0d8922110048}"));
    const QUuid clipId(QStringLiteral("{4c478a21-36bd-4a31-98d6-0d8922110049}"));
    constexpr qint64 start = 9007199254740993LL; // beyond exact IEEE-754 integer range
    constexpr qint64 duration = 123456789;
    constexpr qint64 delta = 7654321;

    QindaTK::TimelineTrack track;
    track.id = trackId;
    track.name = QStringLiteral("Consumer audio");
    track.kind = QStringLiteral("audio");
    QindaTK::TimelineClip clip;
    clip.id = clipId;
    clip.trackId = trackId;
    clip.start = start;
    clip.duration = duration;
    clip.sourceOffset = 42;
    clip.sourceDuration = duration + 100;
    clip.resourceId = QStringLiteral("consumer:audio-1");
    clip.label = QStringLiteral("Exact tick clip");
    clip.kind = QStringLiteral("audio");
    QindaTK::OperationError error;
    if (!check(model->setContent({705600000, 1}, {track}, {clip}, {}, &error),
               "populate one track and clip through setContent")) return 4;
    if (!check(model->trackCount() == 1 && model->clipCount() == 1,
               "populated model counts")) return 5;

    QindaTK::TimelineEditRequest request;
    request.kind = QindaTK::TimelineEditKind::Move;
    request.clipIds = {clipId};
    request.grabTicks = start;
    QindaTK::TimelineEditInput input;
    input.pointerTicks = start + delta;
    input.snap = false;
    error = model->apply(request, input);
    if (!check(!error, "typed move edit accepted")) return 6;
    const auto moved = model->clipRecord(clipId);
    if (!check(moved && moved->start == start + delta && model->historyCount() == 1,
               "move preserves exact ticks and creates one history step")) return 7;

    if (!check(model->undo(), "undo typed move")) return 8;
    const auto undone = model->clipRecord(clipId);
    if (!check(undone && undone->start == start && model->historyCount() == 1,
               "undo restores exact original tick")) return 9;
    if (!check(model->redo(), "redo typed move")) return 10;
    const auto redone = model->clipRecord(clipId);
    if (!check(redone && redone->start == start + delta,
               "redo restores exact moved tick")) return 11;

    QindaTK::TimelineModel reopened;
    const QVariantMap loaded = reopened.loadJson(model->toJson());
    if (!check(loaded.value(QStringLiteral("ok")).toBool(), "reload serialized timeline")) return 12;
    const auto restored = reopened.clipRecord(clipId);
    if (!check(restored && restored->id == clipId && restored->trackId == trackId
                   && restored->start == start + delta && restored->duration == duration
                   && restored->sourceOffset == 42,
               "JSON roundtrip preserves stable IDs and exact clip ticks")) return 13;
    return 0;
}
