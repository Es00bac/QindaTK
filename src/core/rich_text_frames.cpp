// SPDX-License-Identifier: LGPL-3.0-or-later
// Linked text frames of RichTextDocument (WP-051): the frame set step,
// create/resize/thread operations and the TextFrameEditor snapshot.
#include "rich_text_document.h"
#include "rich_text_ops_p.h"

#include <cmath>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

namespace {

constexpr double PointsPerMillimetre = 72.0 / 25.4;

// Reads `key` (millimetres) into `points` when present; false when present but invalid.
bool millimetres(const QVariantMap &map, const char *key, double *points)
{
    const QString name = QLatin1String(key);
    if (!map.contains(name)) return true;
    bool valid = false;
    const double value = map.value(name).toDouble(&valid);
    if (!valid || !std::isfinite(value)) return false;
    *points = value * PointsPerMillimetre;
    return true;
}

double mm(double points) { return points / PointsPerMillimetre; }

} // namespace

OperationError RichTextDocument::applyFrames(const StepFrames &step, bool forward, ChangeSet &change)
{
    const FrameSet &from = forward ? step.before : step.after;
    const FrameSet &to = forward ? step.after : step.before;
    if (!(m_frames == from)) return reject(ErrorCode::Conflict, QStringLiteral("Text frames changed since the edit"));
    if (const QString problem = frameSetProblem(to); !problem.isEmpty()) return reject(ErrorCode::Validation, problem);
    m_frames = to;
    // Frames decide every page's geometry: views lay the story out again.
    change.document = true;
    return {};
}

bool RichTextDocument::setFrameSet(const FrameSet &frames, const QString &label)
{
    if (const QString problem = frameSetProblem(frames); !problem.isEmpty())
        return !reject(ErrorCode::Validation, problem);
    if (frames == m_frames) return true;
    AutoTransaction transaction(this, label);
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepFrames{m_frames, frames}), {});
}

QVariantList RichTextDocument::frameSnapshots() const
{
    const QStringList story = m_frames.storyChain();
    QVariantList result;
    for (int i = 0; i < m_frames.frames.size(); ++i) {
        const TextFrame &f = m_frames.frames.at(i);
        const QString head = m_frames.threadHead(f.id);
        // A frame can continue into a thread head of another thread that is
        // not on an earlier page (docs/publishing.md#text-frames).
        QStringList targets;
        for (const TextFrame &t : m_frames.frames)
            if (t.id != f.id && t.id != head && m_frames.previous(t.id).isEmpty() && t.page >= f.page)
                targets.append(t.id);
        result.append(QVariantMap{
            {QStringLiteral("id"), f.id},
            {QStringLiteral("label"), tr("Frame %1 · page %2").arg(i + 1).arg(f.page + 1)},
            {QStringLiteral("pageId"), QStringLiteral("page-%1").arg(f.page + 1)},
            {QStringLiteral("pageIndex"), f.page},
            {QStringLiteral("xMm"), mm(f.xPt)}, {QStringLiteral("yMm"), mm(f.yPt)},
            {QStringLiteral("widthMm"), mm(f.widthPt)}, {QStringLiteral("heightMm"), mm(f.heightPt)},
            {QStringLiteral("columns"), f.columns}, {QStringLiteral("gutterMm"), mm(f.gutterPt)},
            {QStringLiteral("insetMm"), mm(f.insetPt)},
            {QStringLiteral("nextFrameId"), f.next},
            {QStringLiteral("previousFrameId"), m_frames.previous(f.id)},
            {QStringLiteral("threadId"), head},
            {QStringLiteral("storyId"), story.contains(f.id) ? documentIdString() : QString()},
            {QStringLiteral("storyIndex"), int(story.indexOf(f.id))},
            {QStringLiteral("storyStart"), f.id == m_frames.storyStart},
            {QStringLiteral("connectTargets"), targets}});
    }
    return result;
}

QString RichTextDocument::createTextFrame(int pageIndex, const QVariantMap &geometryMm)
{
    TextFrame frame;
    frame.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    frame.page = pageIndex;
    for (const char *key : {"xMm", "yMm", "widthMm", "heightMm"})
        if (!geometryMm.contains(QLatin1String(key))) {
            reject(ErrorCode::InvalidArgument, QStringLiteral("A text frame needs %1").arg(QLatin1String(key)));
            return {};
        }
    bool columnsValid = true;
    if (geometryMm.contains(QStringLiteral("columns"))) frame.columns = geometryMm.value(QStringLiteral("columns")).toInt(&columnsValid);
    if (!columnsValid || !millimetres(geometryMm, "xMm", &frame.xPt) || !millimetres(geometryMm, "yMm", &frame.yPt)
        || !millimetres(geometryMm, "widthMm", &frame.widthPt) || !millimetres(geometryMm, "heightMm", &frame.heightPt)
        || !millimetres(geometryMm, "gutterMm", &frame.gutterPt) || !millimetres(geometryMm, "insetMm", &frame.insetPt)) {
        reject(ErrorCode::Validation, QStringLiteral("Text frame geometry must be finite numbers"));
        return {};
    }
    FrameSet next = m_frames;
    next.frames.append(frame);
    // The first frame (or the first after the story lost its frames) starts the story.
    if (next.storyStart.isEmpty()) next.storyStart = frame.id;
    return setFrameSet(next, tr("Create text frame")) ? frame.id : QString();
}

bool RichTextDocument::setTextFrameGeometry(const QString &frameId, const QVariantMap &geometryMm)
{
    FrameSet next = m_frames;
    const int index = next.indexOf(frameId);
    if (index < 0) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown text frame"));
    TextFrame &frame = next.frames[index];
    const TextFrame before = frame;
    if (!millimetres(geometryMm, "xMm", &frame.xPt) || !millimetres(geometryMm, "yMm", &frame.yPt)
        || !millimetres(geometryMm, "widthMm", &frame.widthPt) || !millimetres(geometryMm, "heightMm", &frame.heightPt))
        return !reject(ErrorCode::Validation, QStringLiteral("Text frame geometry must be finite numbers"));
    const bool resized = frame.widthPt != before.widthPt || frame.heightPt != before.heightPt;
    return setFrameSet(next, resized ? tr("Resize text frame") : tr("Move text frame"));
}

bool RichTextDocument::setTextFrameLayout(const QString &frameId, const QVariantMap &layout)
{
    FrameSet next = m_frames;
    const int index = next.indexOf(frameId);
    if (index < 0) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown text frame"));
    TextFrame &frame = next.frames[index];
    bool columnsValid = true;
    if (layout.contains(QStringLiteral("columns"))) {
        const double columns = layout.value(QStringLiteral("columns")).toDouble(&columnsValid);
        columnsValid = columnsValid && columns == std::floor(columns) && columns >= 1 && columns <= 24;
        if (columnsValid) frame.columns = int(columns);
    }
    if (!columnsValid || !millimetres(layout, "gutterMm", &frame.gutterPt) || !millimetres(layout, "insetMm", &frame.insetPt))
        return !reject(ErrorCode::Validation, QStringLiteral("Invalid text frame columns, gap or inset"));
    return setFrameSet(next, tr("Text frame columns"));
}

bool RichTextDocument::connectTextFrames(const QString &sourceId, const QString &targetId)
{
    FrameSet next = m_frames;
    const int source = next.indexOf(sourceId);
    const int target = next.indexOf(targetId);
    if (source < 0 || target < 0 || source == target)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown text frame"));
    if (next.frames.at(source).next == targetId) return true;
    if (!next.previous(targetId).isEmpty())
        return !reject(ErrorCode::Validation, QStringLiteral("The next frame already continues another frame"));
    if (next.threadHead(sourceId) == targetId)
        return !reject(ErrorCode::Validation, QStringLiteral("Connecting these frames would form a cycle"));
    // A frame that continued into another is released; its thread keeps the rest.
    next.frames[source].next = targetId;
    // Threading into the story's first frame moves the story start to the new head.
    if (next.storyStart == targetId) next.storyStart = next.threadHead(sourceId);
    return setFrameSet(next, tr("Connect text frames"));
}

bool RichTextDocument::disconnectTextFrame(const QString &frameId)
{
    FrameSet next = m_frames;
    const int index = next.indexOf(frameId);
    if (index < 0) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown text frame"));
    if (next.frames.at(index).next.isEmpty()) return true;
    next.frames[index].next.clear();
    return setFrameSet(next, tr("Disconnect text frame"));
}

bool RichTextDocument::removeTextFrame(const QString &frameId)
{
    FrameSet next = m_frames;
    const int index = next.indexOf(frameId);
    if (index < 0) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown text frame"));
    const TextFrame removed = next.frames.at(index);
    // The thread closes over the gap; a removed story start hands the story on.
    const QString before = next.previous(frameId);
    if (!before.isEmpty()) next.frames[next.indexOf(before)].next = removed.next;
    if (next.storyStart == frameId) next.storyStart = removed.next;
    next.frames.removeAt(index);
    return setFrameSet(next, tr("Delete text frame"));
}

} // namespace QindaTK
