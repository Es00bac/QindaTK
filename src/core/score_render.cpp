// SPDX-License-Identifier: LGPL-3.0-or-later
// Score output (WP-067, D-179, docs/notation.md#output): ScoreView's page
// snapshot, and PDF/image pages painted from that same snapshot by the one
// score painter (score_paint.cpp).
#include "score_layout.h"
#include "score_paint.h"

#include <QFont>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QVariantList>
#include <algorithm>
#include <cmath>

namespace QindaTK::Score {

namespace {

double stemXOf(const ScoreEventLayout &event, double sp)
{
    return event.x + (event.stem > 0 ? 0.55 : -0.55) * sp;
}

// Entry positions: every column plus every beat, at exact tick strings.
QVariantList entryPositions(const ScoreMeasureLayout &measure, double pageWidth)
{
    QVector<std::pair<qint64, double>> points = measure.columns;
    const qint64 beat = kWholeTicks / std::max(1, measure.denominator);
    for (qint64 tick = measure.start + (beat - measure.phase % beat) % beat; tick < measure.end; tick += beat) {
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

} // namespace

QVariantMap pageSnapshot(const ScoreLayout &layout, int page, const QString &scoreId, quint64 revision)
{
    const double pw = std::max(1.0, layout.pageSize.width()), ph = std::max(1.0, layout.pageSize.height());
    if (page < 0 || page >= layout.pages.size())
        return {{QStringLiteral("available"), false}, {QStringLiteral("error"), QObject::tr("There is no page %1").arg(page + 1)}};
    const double sp = layout.staffSpace;
    const ScorePageLayout &lastPage = layout.pages.back();
    const int lastMeasure = lastPage.systems.isEmpty() ? -1 : lastPage.systems.back().lastMeasure;
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
                    {QStringLiteral("number"), measure.index == system.firstMeasure && measure.index > 0 ? QString::number(measure.index + 1) : QString()},
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
                if (measure.index == lastMeasure) entry.insert(QStringLiteral("final"), true);
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
    ScorePaintStyle style; // black ink on white paper
    paintSnapshot(painter, pageSnapshot(layout, page, {}, 0), layout.pageSize, style);
}

} // namespace QindaTK::Score
