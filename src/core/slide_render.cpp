// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_render.h"
#include "rich_text_codec.h"
#include "rich_text_html.h"
#include "slide_deck.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QDir>
#include <QPageSize>
#include <QPainter>
#include <QSet>
#include <QPdfWriter>
#include <QtMath>
#include <QTextDocument>
#include <algorithm>

namespace QindaTK {
namespace Slides {
namespace {
QColor colorOr(const QString &name, const QColor &fallback)
{
    const QColor c(name);
    return name.isEmpty() || !c.isValid() ? fallback : c;
}

void paintText(QPainter &painter, const TextContent &text, const QRectF &box, double fontSize,
               const QColor &color)
{
    const auto fragment = RichTextCodec::fragmentFromJson(text.rich);
    QTextDocument document;
    QFont font = document.defaultFont();
    font.setPixelSize(std::max(1, qRound(fontSize)));
    document.setDefaultFont(font);
    document.setDocumentMargin(4);
    if (fragment) document.setHtml(RichTextHtml::fromFragment(*fragment));
    else document.setPlainText(text.plain);
    document.setTextWidth(box.width());
    painter.save();
    painter.translate(box.topLeft());
    const QRectF clip(0, 0, box.width(), box.height());
    painter.setClipRect(clip, Qt::IntersectClip);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, color);
    context.clip = clip;
    document.documentLayout()->draw(&painter, context);
    painter.restore();
}

// Fill, then text, in object-local coordinates (0, 0, width, height). The
// prompt is painted only for the editing canvas (showPrompt) and only while
// the resolved text is empty.
void paintContent(QPainter &painter, const ResolvedObject &object, const QColor &foreground, bool showPrompt)
{
    const QRectF local(QPointF(0, 0), object.bounds.size());
    if (!object.fill.isEmpty()) painter.fillRect(local, colorOr(object.fill, Qt::transparent));
    if (showPrompt && object.text.plain.isEmpty() && !object.prompt.isEmpty()) {
        QColor muted = foreground;
        muted.setAlphaF(0.45);
        paintText(painter, TextContent::fromPlain(object.prompt), local, object.fontSize, muted);
    } else if (object.kind == QStringLiteral("text") || !object.text.plain.isEmpty()) {
        paintText(painter, object.text, local, object.fontSize, foreground);
    }
}

// Paints one slide into `rect` (slide aspect) with the given object states.
void paintSlide(QPainter &painter, const DeckState &deck, const QString &slideId,
                const QVector<ObjectFrame> &states, const QRectF &rect, qreal opacity)
{
    const int index = slideIndex(deck, slideId);
    if (index < 0 || rect.isEmpty()) return;
    const Slide &slide = deck.slides.at(index);
    const Master *master = findMaster(deck, slide.masterId);
    const QColor background = colorOr(master ? master->background : QString(), Qt::white);
    const QColor foreground = colorOr(master ? master->foreground : QString(), Qt::black);
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / deck.size.width(), rect.height() / deck.size.height());
    painter.setOpacity(opacity);
    painter.fillRect(QRectF(QPointF(0, 0), deck.size), background);
    const QVector<ResolvedObject> objects = resolveObjects(deck, slide);
    for (const ResolvedObject &object : objects) {
        if (!object.visible) continue;
        ObjectFrame state{object.id};
        for (const ObjectFrame &candidate : states) if (candidate.id == object.id) state = candidate;
        if (state.opacity <= 0 || state.scale <= 0) continue;
        painter.save();
        painter.setOpacity(opacity * state.opacity);
        painter.translate(state.dx, state.dy);
        painter.translate(object.bounds.center());
        painter.scale(state.scale, state.scale);
        painter.rotate(object.rotation);
        painter.translate(-object.bounds.width() / 2, -object.bounds.height() / 2);
        paintContent(painter, object, foreground, false);
        painter.restore();
    }
    painter.restore();
}

// Presenter ink and pointer over the current slide (slide pixels).
// Colours are output content like a master's, not chrome theme roles.
void paintAnnotations(QPainter &painter, const DeckState &deck, const SlideFrame &frame, const QRectF &rect)
{
    if (frame.annotations.isEmpty() && !frame.pointerVisible) return;
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / deck.size.width(), rect.height() / deck.size.height());
    for (const AnnotationStroke &stroke : frame.annotations) {
        const bool highlight = stroke.tool == QStringLiteral("highlight");
        QPen pen(highlight ? QColor(255, 212, 0, 110) : QColor(224, 64, 58), highlight ? 18 : 4,
                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        if (stroke.points.size() == 1) painter.drawPoint(stroke.points.first());
        else painter.drawPolyline(stroke.points.constData(), int(stroke.points.size()));
    }
    if (frame.pointerVisible) {
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(224, 32, 32));
        painter.drawEllipse(frame.pointer, 8, 8);
    }
    painter.restore();
}

QRectF fitted(const QSizeF &slide, const QRectF &target)
{
    const qreal scale = std::min(target.width() / slide.width(), target.height() / slide.height());
    const QSizeF size = slide * scale;
    return {target.x() + (target.width() - size.width()) / 2, target.y() + (target.height() - size.height()) / 2,
            size.width(), size.height()};
}
} // namespace

void paintFrame(QPainter &painter, const DeckState &deck, const SlideFrame &frame, const QRectF &target)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.fillRect(target, Qt::black);
    const QRectF rect = fitted(deck.size, target);
    if (!frame.blackedOut && !rect.isEmpty()) {
        const bool transition = !frame.fromSlideId.isEmpty() && frame.transitionProgress < 1;
        const qreal p = std::clamp(frame.transitionProgress, 0.0, 1.0);
        if (transition && frame.transitionKind == QStringLiteral("push")) {
            painter.setClipRect(rect, Qt::IntersectClip);
            const SlideFrame from = finalFrame(deck, frame.fromSlideId);
            paintSlide(painter, deck, frame.fromSlideId, from.objects, rect.translated(-p * rect.width(), 0), 1);
            paintSlide(painter, deck, frame.slideId, frame.objects, rect.translated((1 - p) * rect.width(), 0), 1);
        } else if (transition) { // fade
            const SlideFrame from = finalFrame(deck, frame.fromSlideId);
            paintSlide(painter, deck, frame.fromSlideId, from.objects, rect, 1);
            paintSlide(painter, deck, frame.slideId, frame.objects, rect, p);
        } else {
            paintSlide(painter, deck, frame.slideId, frame.objects, rect, 1);
        }
        paintAnnotations(painter, deck, frame, rect);
    }
    painter.restore();
}

QImage renderObjectImage(const DeckState &deck, const QString &slideId, const ResolvedObject &object,
                         qreal scale, bool showPrompt)
{
    const int index = slideIndex(deck, slideId);
    const QSize size(std::max(1, qCeil(object.bounds.width() * scale)),
                     std::max(1, qCeil(object.bounds.height() * scale)));
    if (index < 0 || !(scale > 0) || size.width() > 8192 || size.height() > 8192) return {};
    const Master *master = findMaster(deck, deck.slides.at(index).masterId);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(qreal(size.width()) / object.bounds.width(), qreal(size.height()) / object.bounds.height());
    paintContent(painter, object, colorOr(master ? master->foreground : QString(), Qt::black), showPrompt);
    painter.end();
    return image;
}

QImage renderImage(const DeckState &deck, const SlideFrame &frame, const QSize &size)
{
    if (size.isEmpty() || size.width() > 8192 || size.height() > 8192) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    paintFrame(painter, deck, frame, QRectF(QPointF(0, 0), QSizeF(size)));
    painter.end();
    return image;
}

void paintNotesPage(QPainter &painter, const DeckState &deck, const QString &slideId, const QRectF &page)
{
    const int index = slideIndex(deck, slideId);
    if (index < 0) return;
    painter.save();
    painter.fillRect(page, Qt::white);
    const QRectF thumbnail(page.x() + page.width() * 0.2, page.y() + page.height() * 0.04,
                           page.width() * 0.6, page.height() * 0.44);
    paintFrame(painter, deck, finalFrame(deck, slideId), thumbnail);
    const QRectF notes(page.x() + page.width() * 0.08, page.y() + page.height() * 0.52,
                       page.width() * 0.84, page.height() * 0.44);
    paintText(painter, deck.slides.at(index).notes, notes, page.height() / 30.0, Qt::black);
    painter.restore();
}

bool exportPdf(const DeckState &deck, const QString &path, bool includeNotes, QString *problem)
{
    QStringList shown;
    for (const Slide &s : deck.slides) if (!s.hidden) shown << s.id;
    if (shown.isEmpty()) {
        if (problem) *problem = QStringLiteral("Every slide is hidden; nothing to export");
        return false;
    }
    QPdfWriter writer(path);
    writer.setResolution(96);
    // 96 px per inch, 72 points per inch: one slide pixel is 0.75 pt.
    writer.setPageSize(QPageSize(deck.size * 0.75, QPageSize::Point, QStringLiteral("Slide"),
                                  QPageSize::ExactMatch));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(QStringLiteral("Slides"));
    QPainter painter;
    if (!painter.begin(&writer)) {
        if (problem) *problem = QStringLiteral("Cannot write %1").arg(path);
        return false;
    }
    const QRectF page(0, 0, writer.width(), writer.height());
    bool first = true;
    for (const QString &id : shown) {
        if (!first) writer.newPage();
        first = false;
        paintFrame(painter, deck, finalFrame(deck, id), page);
        if (includeNotes) {
            writer.newPage();
            paintNotesPage(painter, deck, id, page);
        }
    }
    painter.end();
    return true;
}

} // namespace Slides

// ------------------------------------------------------ SlideDeck outputs
bool SlideDeck::exportPdf(const QString &path, bool includeNotes)
{
    if (textEditing()) return fail(QStringLiteral("Finish the text box edit first"));
    QString problem;
    if (!Slides::exportPdf(m_state, path, includeNotes, &problem)) return fail(problem);
    m_error.clear();
    emit errorChanged();
    return true;
}

QImage SlideDeck::renderFrame(const QString &slideId, int step, const QString &ticks, int width, int height) const
{
    const auto parsed = TimelineTime::parseDecimal(ticks.isEmpty() ? QStringLiteral("0") : ticks);
    if (!parsed || Slides::slideIndex(m_state, slideId) < 0) return {};
    return Slides::renderImage(m_state, Slides::evaluate(m_state, slideId, step, *parsed), QSize(width, height));
}

int SlideDeck::exportPlayback(const QString &slideId, const QString &directory, int framesPerSecond, int width)
{
    if (textEditing()) return fail(QStringLiteral("Finish the text box edit first")), -1;
    const QVector<Slides::PlaybackPosition> positions = Slides::playbackPositions(m_state, slideId, framesPerSecond);
    if (positions.isEmpty()) return fail(QStringLiteral("Unknown slide, frame rate outside 1-240 or too many frames")), -1;
    const int w = width > 0 ? std::min(width, 4096) : qRound(m_state.size.width());
    const QSize size(w, std::max(1, qRound(w * m_state.size.height() / m_state.size.width())));
    const QDir dir(directory);
    if (!dir.exists() && !QDir().mkpath(directory)) return fail(QStringLiteral("Cannot create %1").arg(directory)), -1;
    for (int i = 0; i < positions.size(); ++i) {
        const QImage image = Slides::renderImage(m_state, Slides::evaluate(m_state, slideId, positions.at(i).step,
                                                                           positions.at(i).ticks), size);
        const QString path = dir.filePath(QStringLiteral("frame-%1.png").arg(i, 5, 10, QLatin1Char('0')));
        if (image.isNull() || !image.save(path, "PNG")) return fail(QStringLiteral("Cannot write %1").arg(path)), -1;
    }
    m_error.clear();
    emit errorChanged();
    return int(positions.size());
}

QString SlideDeck::cachedPreview(const Slides::Slide &slide) const
{
    const Slides::Master *master = Slides::findMaster(m_state, slide.masterId);
    if (!master) return {};
    auto it = m_previews.constFind(slide.id);
    if (it != m_previews.constEnd() && it->size == m_state.size && it->master == *master && it->slide == slide)
        return it->url;
    const QString url = previewDataUrl(slide.id, PreviewWidth);
    m_previews.insert(slide.id, {slide, *master, m_state.size, url});
    return url;
}

void SlideDeck::prunePreviews() const
{
    if (m_previews.size() <= m_state.slides.size()) return;
    QSet<QString> live;
    for (const Slides::Slide &s : m_state.slides) live.insert(s.id);
    for (auto it = m_previews.begin(); it != m_previews.end();)
        it = live.contains(it.key()) ? std::next(it) : m_previews.erase(it);
}

QString SlideDeck::previewDataUrl(const QString &slideId, int width) const
{
    if (width < 16 || width > 2048 || Slides::slideIndex(m_state, slideId) < 0) return {};
    const int height = std::max(1, qRound(width * m_state.size.height() / m_state.size.width()));
    const QImage image = Slides::renderImage(m_state, Slides::finalFrame(m_state, slideId), QSize(width, height));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

} // namespace QindaTK
