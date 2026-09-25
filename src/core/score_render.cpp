// SPDX-License-Identifier: LGPL-3.0-or-later
// Score output (WP-067, D-179, docs/notation.md#output): ScoreView's page
// snapshot and the QPainter page used for PDF files and images. Both read
// the one ScoreLayout; neither decides a position of its own.
#include "score_layout.h"

#include <QFont>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QVariantList>
#include <algorithm>
#include <cmath>

namespace QindaTK::Score {

namespace {

double stepY(double top, double sp, double step)
{
    return top + (8.0 - step) * sp / 2.0;
}

double stemXOf(const ScoreEventLayout &event, double sp)
{
    return event.x + (event.stem > 0 ? 0.55 : -0.55) * sp;
}

// Entry positions: every column plus every beat, at exact tick strings.
QVariantList entryPositions(const ScoreMeasureLayout &measure, double pageWidth)
{
    QVector<std::pair<qint64, double>> points = measure.columns;
    const qint64 beat = kWholeTicks / std::max(1, measure.denominator);
    for (qint64 tick = measure.start; tick < measure.end; tick += beat) {
        bool known = false;
        for (const auto &column : measure.columns) known = known || column.first == tick;
        if (known) continue;
        // Interpolate between the surrounding columns (or the bar line).
        double x0 = measure.contentX, x1 = measure.x1;
        qint64 t0 = measure.start, t1 = measure.end;
        for (const auto &[at, x] : measure.columns) {
            if (at <= tick) { t0 = at; x0 = x; }
            if (at > tick) { t1 = at; x1 = x; break; }
        }
        points.push_back({tick, x0 + (x1 - x0) * double(tick - t0) / double(std::max<qint64>(1, t1 - t0))});
    }
    std::sort(points.begin(), points.end());
    QVariantList list;
    for (const auto &[tick, x] : points)
        list.push_back(QVariantMap{{QStringLiteral("x"), x / pageWidth}, {QStringLiteral("ticks"), QString::number(tick)}});
    return list;
}

QString glyph(const QString &name)
{
    if (name == QLatin1String("sharp")) return QStringLiteral("♯");
    if (name == QLatin1String("flat")) return QStringLiteral("♭");
    if (name == QLatin1String("natural")) return QStringLiteral("♮");
    if (name == QLatin1String("double-sharp")) return QStringLiteral("x");
    if (name == QLatin1String("double-flat")) return QStringLiteral("♭♭");
    return name;
}

QString articulationGlyph(const QString &name)
{
    if (name == QLatin1String("staccato")) return QStringLiteral(".");
    if (name == QLatin1String("staccatissimo")) return QStringLiteral("'");
    if (name == QLatin1String("tenuto")) return QStringLiteral("–");
    if (name == QLatin1String("accent")) return QStringLiteral(">");
    if (name == QLatin1String("marcato")) return QStringLiteral("^");
    return QStringLiteral("⌢"); // fermata arc
}

void drawText(QPainter *painter, const QPointF &centre, const QString &text, double size, bool bold = false,
              bool italic = false)
{
    QFont font = painter->font();
    font.setPointSizeF(std::max(1.0, size));
    font.setBold(bold);
    font.setItalic(italic);
    painter->setFont(font);
    const QRectF box(centre.x() - size * 4, centre.y() - size, size * 8, size * 2);
    painter->drawText(box, Qt::AlignCenter, text);
}

void paintEvent(QPainter *painter, const ScoreEventLayout &event, double top, double sp)
{
    const QColor ink(Qt::black);
    painter->setPen(QPen(ink, 0.12 * sp));
    if (event.rest) {
        const double y = stepY(top, sp, event.step);
        painter->setBrush(ink);
        if (event.base <= 1) { // whole hangs below a line, half sits on it
            const double lineY = event.base == 0 ? y - sp / 2 : y;
            painter->drawRect(QRectF(event.x - 0.6 * sp, event.base == 0 ? lineY : lineY - 0.5 * sp, 1.2 * sp, 0.5 * sp));
        } else if (event.base == 2) {
            QPainterPath zigzag;
            zigzag.moveTo(event.x - 0.3 * sp, y - 1.4 * sp);
            zigzag.lineTo(event.x + 0.3 * sp, y - 0.6 * sp);
            zigzag.lineTo(event.x - 0.3 * sp, y + 0.1 * sp);
            zigzag.lineTo(event.x + 0.3 * sp, y + 0.9 * sp);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(ink, 0.25 * sp));
            painter->drawPath(zigzag);
        } else {
            painter->drawLine(QPointF(event.x + 0.4 * sp, y - 1.0 * sp), QPointF(event.x - 0.2 * sp, y + 1.0 * sp));
            for (int f = 0; f < event.base - 2; ++f)
                painter->drawEllipse(QPointF(event.x - 0.1 * sp + f * 0.15 * sp, y - 0.8 * sp + f * 0.6 * sp),
                                     0.25 * sp, 0.25 * sp);
        }
        for (int d = 0; d < event.dots; ++d)
            painter->drawEllipse(QPointF(event.x + (1.0 + 0.5 * d) * sp, y - 0.25 * sp), 0.15 * sp, 0.15 * sp);
        return;
    }
    for (const ScoreHeadLayout &head : event.heads) {
        const double x = event.x + head.dx;
        const double y = stepY(top, sp, head.step);
        painter->setPen(QPen(ink, 0.1 * sp));
        for (double ledger = -2; ledger >= head.step - 0.01; ledger -= 2)
            painter->drawLine(QPointF(x - 0.9 * sp, stepY(top, sp, ledger)), QPointF(x + 0.9 * sp, stepY(top, sp, ledger)));
        for (double ledger = 10; ledger <= head.step + 0.01; ledger += 2)
            painter->drawLine(QPointF(x - 0.9 * sp, stepY(top, sp, ledger)), QPointF(x + 0.9 * sp, stepY(top, sp, ledger)));
        painter->save();
        painter->translate(x, y);
        painter->rotate(-20);
        painter->setBrush(event.filled ? QBrush(ink) : QBrush(Qt::NoBrush));
        painter->setPen(QPen(ink, 0.15 * sp));
        painter->drawEllipse(QPointF(0, 0), 0.62 * sp, 0.42 * sp);
        painter->restore();
        if (!head.accidental.isEmpty())
            drawText(painter, QPointF(x - 1.3 * sp, y), glyph(head.accidental), 1.8 * sp);
        painter->setBrush(ink);
        const double dotY = std::fmod(head.step, 2.0) == 0.0 ? y - sp / 2 : y;
        for (int d = 0; d < event.dots; ++d)
            painter->drawEllipse(QPointF(event.x + (1.0 + 0.5 * d) * sp, dotY), 0.15 * sp, 0.15 * sp);
    }
    if (event.stem != 0) {
        double from = event.step;
        for (const ScoreHeadLayout &head : event.heads)
            from = event.stem > 0 ? std::min(from, head.step) : std::max(from, head.step);
        const double x = stemXOf(event, sp);
        painter->setPen(QPen(ink, 0.12 * sp));
        painter->drawLine(QPointF(x, stepY(top, sp, from)), QPointF(x, stepY(top, sp, event.stemEnd)));
        for (int f = 0; f < event.flags; ++f) {
            const double y = stepY(top, sp, event.stemEnd) + (event.stem > 0 ? 1 : -1) * f * 0.75 * sp;
            painter->drawLine(QPointF(x, y), QPointF(x + 0.8 * sp, y + (event.stem > 0 ? 1.2 : -1.2) * sp));
        }
    }
    for (int a = 0; a < event.articulations.size(); ++a) {
        const double step = event.articulationStep + (event.stem < 0 ? 1.5 : -1.5) * a;
        drawText(painter, QPointF(event.x, stepY(top, sp, step)), articulationGlyph(event.articulations[a]), 1.6 * sp, true);
    }
    if (!event.dynamic.isEmpty())
        drawText(painter, QPointF(event.x, stepY(top, sp, -6)), event.dynamic, 1.6 * sp, true, true);
}

} // namespace

QVariantMap pageSnapshot(const ScoreLayout &layout, int page, const QString &scoreId, quint64 revision)
{
    const double pw = std::max(1.0, layout.pageSize.width()), ph = std::max(1.0, layout.pageSize.height());
    if (page < 0 || page >= layout.pages.size())
        return {{QStringLiteral("available"), false}, {QStringLiteral("error"), QObject::tr("There is no page %1").arg(page + 1)}};
    const double sp = layout.staffSpace;
    QVariantList systems;
    for (const ScoreSystemLayout &system : layout.pages[page].systems) {
        QVariantList staves;
        for (const ScoreStaffLayout &staff : system.staves) {
            QVariantList measures;
            for (const ScoreMeasureLayout &measure : staff.measures) {
                QVariantList voices;
                for (const ScoreVoiceLayout &voice : measure.voices) {
                    QVariantList events;
                    for (const ScoreEventLayout &event : voice.events) {
                        QVariantList heads;
                        for (const ScoreHeadLayout &head : event.heads)
                            heads.push_back(QVariantMap{{QStringLiteral("noteId"), idText(head.noteId)},
                                                        {QStringLiteral("staffStep"), head.step},
                                                        {QStringLiteral("dx"), head.dx / pw},
                                                        {QStringLiteral("accidental"), head.accidental},
                                                        {QStringLiteral("tie"), head.tieStart}});
                        events.push_back(QVariantMap{
                            {QStringLiteral("id"), idText(event.id)},
                            {QStringLiteral("kind"), event.rest ? QStringLiteral("rest") : QStringLiteral("note")},
                            {QStringLiteral("x"), event.x / pw},
                            {QStringLiteral("staffStep"), event.step},
                            {QStringLiteral("ticks"), QString::number(event.tick)},
                            {QStringLiteral("durationTicks"), QString::number(event.ticks)},
                            {QStringLiteral("durationId"), event.durationId},
                            {QStringLiteral("base"), event.base},
                            {QStringLiteral("dotCount"), event.dots},
                            {QStringLiteral("filledHead"), event.filled},
                            {QStringLiteral("stemDirection"), event.stem > 0 ? QStringLiteral("up")
                                                              : event.stem < 0 ? QStringLiteral("down") : QString()},
                            {QStringLiteral("stemX"), stemXOf(event, sp) / pw},
                            {QStringLiteral("stemEndStep"), event.stemEnd},
                            {QStringLiteral("flags"), event.flags},
                            {QStringLiteral("beamed"), event.beamed},
                            {QStringLiteral("heads"), heads},
                            {QStringLiteral("accidental"), event.heads.isEmpty() ? QString() : event.heads.front().accidental},
                            {QStringLiteral("articulations"), event.articulations},
                            {QStringLiteral("articulationStep"), event.articulationStep},
                            {QStringLiteral("dynamic"), event.dynamic},
                            {QStringLiteral("label"), event.label}});
                    }
                    voices.push_back(QVariantMap{{QStringLiteral("id"), idText(voice.id)},
                                                 {QStringLiteral("number"), voice.number},
                                                 {QStringLiteral("events"), events}});
                }
                QVariantList key;
                for (const ScoreGlyphLayout &glyphLayout : measure.key)
                    key.push_back(QVariantMap{{QStringLiteral("x"), glyphLayout.x / pw},
                                              {QStringLiteral("staffStep"), glyphLayout.step},
                                              {QStringLiteral("name"), glyphLayout.name}});
                QVariantMap entry{
                    {QStringLiteral("id"), idText(measure.id)},
                    {QStringLiteral("number"), measure.index == system.firstMeasure ? QString::number(measure.index + 1) : QString()},
                    {QStringLiteral("index"), measure.index},
                    {QStringLiteral("x0"), measure.x0 / pw},
                    {QStringLiteral("x1"), measure.x1 / pw},
                    {QStringLiteral("contentX"), measure.contentX / pw},
                    {QStringLiteral("startTicks"), QString::number(measure.start)},
                    {QStringLiteral("endTicks"), QString::number(measure.end)},
                    {QStringLiteral("keyGlyphs"), key},
                    {QStringLiteral("entryPositions"), entryPositions(measure, pw)},
                    {QStringLiteral("voices"), voices}};
                if (measure.showClef) entry.insert(QStringLiteral("clefX"), measure.clefX / pw);
                if (measure.showTime)
                    entry.insert(QStringLiteral("time"), QVariantMap{{QStringLiteral("x"), measure.timeX / pw},
                                                                     {QStringLiteral("numerator"), measure.numerator},
                                                                     {QStringLiteral("denominator"), measure.denominator}});
                measures.push_back(entry);
            }
            QVariantList ties, beams, tuplets;
            for (const ScoreTieLayout &tie : staff.ties)
                ties.push_back(QVariantMap{{QStringLiteral("x0"), tie.x0 / pw}, {QStringLiteral("x1"), tie.x1 / pw},
                                           {QStringLiteral("staffStep"), tie.step}, {QStringLiteral("above"), tie.above}});
            for (const ScoreBeamLayout &beam : staff.beams)
                beams.push_back(QVariantMap{{QStringLiteral("x0"), beam.x0 / pw}, {QStringLiteral("step0"), beam.s0},
                                            {QStringLiteral("x1"), beam.x1 / pw}, {QStringLiteral("step1"), beam.s1},
                                            {QStringLiteral("up"), beam.up}});
            for (const ScoreTupletLayout &tuplet : staff.tuplets)
                tuplets.push_back(QVariantMap{{QStringLiteral("x0"), tuplet.x0 / pw}, {QStringLiteral("x1"), tuplet.x1 / pw},
                                              {QStringLiteral("staffStep"), tuplet.step}, {QStringLiteral("label"), tuplet.label}});
            staves.push_back(QVariantMap{{QStringLiteral("id"), idText(staff.id)},
                                         {QStringLiteral("partId"), idText(staff.partId)},
                                         {QStringLiteral("label"), staff.label},
                                         {QStringLiteral("clef"), clefName(staff.clef)},
                                         {QStringLiteral("top"), staff.top / ph},
                                         {QStringLiteral("lineGap"), sp / ph},
                                         {QStringLiteral("x0"), staff.x0 / pw},
                                         {QStringLiteral("x1"), staff.x1 / pw},
                                         {QStringLiteral("measures"), measures},
                                         {QStringLiteral("ties"), ties},
                                         {QStringLiteral("beams"), beams},
                                         {QStringLiteral("tuplets"), tuplets}});
        }
        systems.push_back(QVariantMap{{QStringLiteral("id"), QStringLiteral("system-%1").arg(system.index)},
                                      {QStringLiteral("top"), system.top / ph},
                                      {QStringLiteral("bottom"), system.bottom / ph},
                                      {QStringLiteral("staves"), staves}});
    }
    QVariantMap snapshot{{QStringLiteral("available"), true},
                         {QStringLiteral("scoreId"), scoreId},
                         {QStringLiteral("revision"), QString::number(revision)},
                         {QStringLiteral("page"), page},
                         {QStringLiteral("pageCount"), int(layout.pages.size())},
                         {QStringLiteral("pageWidth"), pw},
                         {QStringLiteral("pageHeight"), ph},
                         {QStringLiteral("systems"), systems}};
    if (page == 0 && !layout.title.isEmpty()) {
        snapshot.insert(QStringLiteral("title"), layout.title);
        snapshot.insert(QStringLiteral("composer"), layout.composer);
        snapshot.insert(QStringLiteral("titleY"), layout.titleY / ph);
    }
    return snapshot;
}

void paintPage(QPainter *painter, const ScoreLayout &layout, int page)
{
    if (!painter || page < 0 || page >= layout.pages.size()) return;
    const double sp = layout.staffSpace;
    const QColor ink(Qt::black);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(ink);
    if (page == 0 && !layout.title.isEmpty()) {
        drawText(painter, QPointF(layout.pageSize.width() / 2, layout.titleY), layout.title, 2.4 * sp, true);
        if (!layout.composer.isEmpty())
            drawText(painter, QPointF(layout.pageSize.width() * 0.8, layout.titleY + 2.5 * sp), layout.composer, 1.4 * sp);
    }
    for (const ScoreSystemLayout &system : layout.pages[page].systems)
        for (const ScoreStaffLayout &staff : system.staves) {
            const double top = staff.top;
            painter->setPen(QPen(ink, 0.1 * sp));
            for (int line = 0; line < 5; ++line)
                painter->drawLine(QPointF(staff.x0, top + line * sp), QPointF(staff.x1, top + line * sp));
            if (!staff.label.isEmpty())
                drawText(painter, QPointF(staff.x0 - 2.2 * sp, top + 2 * sp), staff.label, 1.3 * sp);
            painter->drawLine(QPointF(staff.x0, top), QPointF(staff.x0, top + 4 * sp));
            for (const ScoreMeasureLayout &measure : staff.measures) {
                painter->setPen(QPen(ink, 0.15 * sp));
                painter->drawLine(QPointF(measure.x1, top), QPointF(measure.x1, top + 4 * sp));
                if (measure.index == system.firstMeasure)
                    drawText(painter, QPointF(measure.x0 + sp, top - 1.5 * sp), QString::number(measure.index + 1), 1.0 * sp);
                if (measure.showClef) {
                    const QString letter = staff.clef == ScoreClef::Bass ? QStringLiteral("F")
                                           : staff.clef == ScoreClef::Alto ? QStringLiteral("C") : QStringLiteral("G");
                    const double step = staff.clef == ScoreClef::Bass ? 6 : staff.clef == ScoreClef::Alto ? 4 : 2;
                    drawText(painter, QPointF(measure.clefX + sp, stepY(top, sp, step)), letter, 3.0 * sp, true);
                }
                for (const ScoreGlyphLayout &key : measure.key)
                    drawText(painter, QPointF(key.x, stepY(top, sp, key.step)), glyph(key.name), 1.8 * sp);
                if (measure.showTime) {
                    drawText(painter, QPointF(measure.timeX, stepY(top, sp, 6)), QString::number(measure.numerator), 2.0 * sp, true);
                    drawText(painter, QPointF(measure.timeX, stepY(top, sp, 2)), QString::number(measure.denominator), 2.0 * sp, true);
                }
                for (const ScoreVoiceLayout &voice : measure.voices)
                    for (const ScoreEventLayout &event : voice.events) paintEvent(painter, event, top, sp);
            }
            painter->setBrush(ink);
            painter->setPen(Qt::NoPen);
            for (const ScoreBeamLayout &beam : staff.beams) {
                const double thickness = (beam.up ? 0.5 : -0.5) * sp;
                const QPointF a(beam.x0, stepY(top, sp, beam.s0)), b(beam.x1, stepY(top, sp, beam.s1));
                painter->drawPolygon(QPolygonF{a, b, b + QPointF(0, thickness), a + QPointF(0, thickness)});
            }
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(ink, 0.12 * sp));
            for (const ScoreTieLayout &tie : staff.ties) {
                const double y = stepY(top, sp, tie.step) + (tie.above ? -0.6 : 0.6) * sp;
                QPainterPath arc;
                arc.moveTo(tie.x0, y);
                arc.quadTo((tie.x0 + tie.x1) / 2, y + (tie.above ? -1.0 : 1.0) * sp, tie.x1, y);
                painter->drawPath(arc);
            }
            for (const ScoreTupletLayout &tuplet : staff.tuplets) {
                const double y = stepY(top, sp, tuplet.step);
                const double mid = (tuplet.x0 + tuplet.x1) / 2;
                painter->drawLine(QPointF(tuplet.x0, y), QPointF(mid - sp, y));
                painter->drawLine(QPointF(mid + sp, y), QPointF(tuplet.x1, y));
                drawText(painter, QPointF(mid, y), tuplet.label, 1.3 * sp, false, true);
            }
        }
    painter->restore();
}

} // namespace QindaTK::Score
