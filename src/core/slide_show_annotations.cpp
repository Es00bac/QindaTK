// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_deck.h"
#include "slide_show.h"

#include <cmath>

// SlideShow annotation commands (OFF-23, docs/slides.md#annotations): the
// presenter's pointer and ink over the live show. Ink is show state, kept
// per slide until the show stops, and reaches the audience through the
// frame (paintFrame draws it); it is never part of the deck.
namespace QindaTK {
using namespace Slides;

namespace {
bool slidePoint(const QVariant &value, QPointF *out)
{
    QPointF p;
    if (value.canConvert<QPointF>() && value.metaType().id() == QMetaType::QPointF) {
        p = value.toPointF();
    } else {
        const QVariantMap map = value.toMap();
        bool okX = false, okY = false;
        p = QPointF(map.value("x").toDouble(&okX), map.value("y").toDouble(&okY));
        if (!okX || !okY) return false;
    }
    if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || std::abs(p.x()) > MaximumCoordinate
        || std::abs(p.y()) > MaximumCoordinate) return false;
    *out = p;
    return true;
}
} // namespace

void SlideShow::setAnnotationTool(const QString &tool)
{
    if (tool == m_annotationTool) return;
    if (tool != QStringLiteral("pointer") && tool != QStringLiteral("pen") && tool != QStringLiteral("highlight")) {
        fail(QStringLiteral("Unknown annotation tool %1").arg(tool));
        return;
    }
    m_annotationTool = tool;
    if (tool != QStringLiteral("pointer")) m_pointerVisible = false;
    emit annotationsChanged();
    emit frameChanged();
}

bool SlideShow::addAnnotationStroke(const QVariantList &points)
{
    if (!m_running || m_slideId.isEmpty()) return fail(QStringLiteral("The show is not running"));
    if (m_annotationTool == QStringLiteral("pointer")) return fail(QStringLiteral("Choose the pen or highlight tool to draw"));
    if (points.isEmpty() || points.size() > MaximumStrokePoints)
        return fail(QStringLiteral("A stroke has 1-%1 points").arg(MaximumStrokePoints));
    QVector<AnnotationStroke> &strokes = m_annotations[m_slideId];
    if (strokes.size() >= MaximumStrokes) return fail(QStringLiteral("This slide holds the most strokes; clear it first"));
    AnnotationStroke stroke{m_annotationTool, {}};
    stroke.points.reserve(points.size());
    for (const QVariant &value : points) {
        QPointF p;
        if (!slidePoint(value, &p)) return fail(QStringLiteral("Stroke points are finite slide coordinates"));
        stroke.points << p;
    }
    strokes << stroke;
    emit annotationsChanged();
    emit frameChanged();
    return true;
}

void SlideShow::clearAnnotations()
{
    if (m_annotations.remove(m_slideId) == 0) return;
    emit annotationsChanged();
    emit frameChanged();
}

bool SlideShow::setPointer(qreal x, qreal y)
{
    QPointF p;
    if (!m_running || !slidePoint(QVariantMap{{"x", x}, {"y", y}}, &p)) return fail(QStringLiteral("Invalid pointer position"));
    if (m_pointerVisible && p == m_pointer) return true;
    m_pointer = p;
    m_pointerVisible = true;
    emit annotationsChanged();
    emit frameChanged();
    return true;
}

void SlideShow::hidePointer()
{
    if (!m_pointerVisible) return;
    m_pointerVisible = false;
    emit annotationsChanged();
    emit frameChanged();
}

} // namespace QindaTK
