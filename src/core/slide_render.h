// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "slide_timeline.h"

#include <QImage>

class QPainter;

namespace QindaTK::Slides {

// AGENT-CONTRACT (docs/slides.md#output, D-160): the ONE slide painter.
// SlideFrameView (audience and presenter previews), renderImage, previews
// and PDF export all call paintFrame, so what the audience sees at a given
// slide/step/tick is what an export or a test sample shows. It paints only
// slide content: speaker notes are drawn solely by paintNotesPage.
// `target` is letterboxed with black to keep the slide aspect ratio.
void paintFrame(QPainter &painter, const DeckState &deck, const SlideFrame &frame, const QRectF &target);
QImage renderImage(const DeckState &deck, const SlideFrame &frame, const QSize &size);
// Slide thumbnail at the top, the slide's notes below (PDF notes pages).
void paintNotesPage(QPainter &painter, const DeckState &deck, const QString &slideId, const QRectF &page);
// One page per shown (not hidden) slide with every build complete; with
// notes, each slide page is followed by its notes page.
bool exportPdf(const DeckState &deck, const QString &path, bool includeNotes, QString *problem);

} // namespace QindaTK::Slides
