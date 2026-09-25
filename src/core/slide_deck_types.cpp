// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_deck_types.h"
#include "rich_text_codec.h"
#include "rich_text_html.h"

#include <QColor>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace QindaTK::Slides {
namespace {
QString plainOf(const RichText::Fragment &fragment)
{
    QStringList lines;
    for (const RichText::Block &block : fragment.blocks) {
        if (block.paragraph) {
            QString text = block.paragraph->text;
            text.remove(QChar(RichText::ObjectChar));
            lines << text;
        } else if (block.table) {
            for (const RichText::TableRow &row : block.table->rows)
                for (const RichText::TableCell &cell : row.cells)
                    for (const RichText::ParagraphPtr &p : cell.paragraphs)
                        if (p) lines << p->text;
        }
    }
    return lines.join(u'\n');
}
bool validRect(const QRectF &r)
{
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width())
        && std::isfinite(r.height()) && r.width() > 0 && r.height() > 0
        && std::abs(r.x()) <= MaximumCoordinate && std::abs(r.y()) <= MaximumCoordinate
        && r.width() <= MaximumCoordinate && r.height() <= MaximumCoordinate;
}
bool validColor(const QString &color) { return color.isEmpty() || QColor::isValidColorName(color); }
Placeholder placeholder(const QString &role, const QString &label, const QRectF &bounds,
                        double fontSize, const QString &text)
{
    Placeholder p;
    p.role = role;
    p.label = label;
    p.bounds = bounds;
    p.fontSize = fontSize;
    p.text = TextContent::fromPlain(text);
    return p;
}
} // namespace

TextContent TextContent::fromPlain(const QString &text)
{
    const RichText::Fragment fragment = RichTextHtml::fromPlainText(text);
    return {RichTextCodec::fragmentToJson(fragment), plainOf(fragment)};
}

std::optional<TextContent> TextContent::fromFragmentJson(const QJsonObject &json, QString *problem)
{
    OperationError error;
    const auto fragment = RichTextCodec::fragmentFromJson(json, &error);
    if (!fragment) {
        if (problem) *problem = error.message.isEmpty() ? QStringLiteral("Invalid text fragment") : error.message;
        return std::nullopt;
    }
    return TextContent{json, plainOf(*fragment)};
}

const QStringList &objectKinds() { static const QStringList v{"text", "shape"}; return v; }
const QStringList &effectKinds()
{
    static const QStringList v{"appear", "fade-in", "fly-in", "zoom-in", "fade-out"};
    return v;
}
const QStringList &effectTriggers() { static const QStringList v{"click", "with", "after"}; return v; }
const QStringList &easingNames()
{
    static const QStringList v{"linear", "in-cubic", "out-cubic", "in-out-cubic"};
    return v;
}
const QStringList &transitionKinds() { static const QStringList v{"none", "fade", "push"}; return v; }
bool isEntryEffect(const QString &kind) { return kind != QStringLiteral("fade-out"); }

int slideIndex(const DeckState &deck, const QString &slideId)
{
    for (int i = 0; i < deck.slides.size(); ++i) if (deck.slides.at(i).id == slideId) return i;
    return -1;
}
const Master *findMaster(const DeckState &deck, const QString &masterId)
{
    for (const Master &m : deck.masters) if (m.id == masterId) return &m;
    return nullptr;
}
const Layout *findLayout(const Master &master, const QString &layoutId)
{
    for (const Layout &l : master.layouts) if (l.id == layoutId) return &l;
    return nullptr;
}
const Placeholder *findPlaceholder(const QVector<Placeholder> &list, const QString &role)
{
    for (const Placeholder &p : list) if (p.role == role) return &p;
    return nullptr;
}

QVector<ResolvedObject> resolveObjects(const DeckState &deck, const Slide &slide)
{
    const Master *master = findMaster(deck, slide.masterId);
    const Layout *layout = master ? findLayout(*master, slide.layoutId) : nullptr;
    QVector<ResolvedObject> out;
    out.reserve(slide.objects.size());
    for (const SlideObject &o : slide.objects) {
        ResolvedObject r{o.id, o.kind, o.role, o.bounds, o.fill, o.fontSize > 0 ? o.fontSize : 24,
                         o.text, o.locked, o.visible};
        const Placeholder *base = master && !o.role.isEmpty() ? findPlaceholder(master->placeholders, o.role) : nullptr;
        if (base) {
            // AGENT-GUARD: inheritance is layout -> master for bounds and
            // master for text/fill/kind; an own* flag always wins (D-158).
            const Placeholder *positioned = layout ? findPlaceholder(layout->placeholders, o.role) : nullptr;
            r.kind = base->kind;
            if (o.fontSize <= 0) r.fontSize = base->fontSize;
            if (!o.ownBounds) { r.bounds = positioned ? positioned->bounds : base->bounds; r.inheritedBounds = true; }
            if (!o.ownText) { r.text = base->text; r.inheritedText = true; }
            if (!o.ownFill) { r.fill = base->fill; r.inheritedFill = true; }
        }
        out << r;
    }
    return out;
}

QString slideTitle(const DeckState &deck, const Slide &slide)
{
    for (const ResolvedObject &o : resolveObjects(deck, slide))
        if (o.role == QStringLiteral("title") && !o.text.plain.trimmed().isEmpty())
            return o.text.plain.section(u'\n', 0, 0).trimmed();
    return slide.title;
}

QString newId(const QString &prefix)
{
    return prefix + u'-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void ensureLayoutObjects(const DeckState &deck, Slide &slide)
{
    const Master *master = findMaster(deck, slide.masterId);
    const Layout *layout = master ? findLayout(*master, slide.layoutId) : nullptr;
    if (!layout) return;
    for (const Placeholder &p : layout->placeholders) {
        bool present = false;
        for (const SlideObject &o : slide.objects) present = present || o.role == p.role;
        if (present) continue;
        const Placeholder *base = findPlaceholder(master->placeholders, p.role);
        SlideObject o;
        o.id = newId(QStringLiteral("object"));
        o.role = p.role;
        o.kind = base ? base->kind : QStringLiteral("text");
        o.bounds = p.bounds;  // last-known geometry; inherited while !ownBounds
        o.text = base ? base->text : TextContent::fromPlain({});
        slide.objects << o;
    }
}

DeckState defaultDeck()
{
    DeckState deck;
    Master master;
    master.id = QStringLiteral("master-default");
    master.title = QStringLiteral("Default");
    master.themeId = QStringLiteral("default");
    master.placeholders = {
        placeholder("title", "Title", QRectF(80, 60, 800, 90), 40, "Click to add title"),
        placeholder("subtitle", "Subtitle", QRectF(80, 300, 800, 60), 24, "Click to add subtitle"),
        placeholder("body", "Body", QRectF(80, 170, 800, 320), 22, "Click to add text")};
    Layout title{QStringLiteral("layout-title"), QStringLiteral("Title slide"),
                 {placeholder("title", "Title", QRectF(80, 180, 800, 110), 40, {}),
                  placeholder("subtitle", "Subtitle", QRectF(80, 310, 800, 60), 24, {})}};
    Layout content{QStringLiteral("layout-content"), QStringLiteral("Title and content"),
                   {placeholder("title", "Title", QRectF(80, 40, 800, 90), 40, {}),
                    placeholder("body", "Body", QRectF(80, 150, 800, 340), 22, {})}};
    master.layouts = {title, content};
    deck.masters << master;
    Slide slide;
    slide.id = newId(QStringLiteral("slide"));
    slide.masterId = master.id;
    slide.layoutId = title.id;
    ensureLayoutObjects(deck, slide);
    deck.slides << slide;
    return deck;
}

QString validate(const DeckState &deck)
{
    if (!(deck.size.width() >= 16 && deck.size.height() >= 16 && deck.size.width() <= 16384
          && deck.size.height() <= 16384))
        return QStringLiteral("Slide size must be 16-16384 pixels");
    if (deck.masters.isEmpty() || deck.masters.size() > MaximumMasters)
        return QStringLiteral("A deck needs 1-%1 masters").arg(MaximumMasters);
    if (deck.slides.isEmpty() || deck.slides.size() > MaximumSlides)
        return QStringLiteral("A deck needs 1-%1 slides").arg(MaximumSlides);
    QSet<QString> ids;
    auto unique = [&ids](const QString &id) {
        if (id.trimmed().isEmpty() || id.size() > 128 || ids.contains(id)) return false;
        ids.insert(id);
        return true;
    };
    auto placeholdersValid = [&](const QVector<Placeholder> &list, bool master) {
        if (list.size() > MaximumPlaceholders) return false;
        QSet<QString> roles;
        for (const Placeholder &p : list) {
            if (p.role.isEmpty() || roles.contains(p.role) || !validRect(p.bounds)) return false;
            roles.insert(p.role);
            if (master && (!objectKinds().contains(p.kind) || !validColor(p.fill)
                           || !(p.fontSize >= 4 && p.fontSize <= 400))) return false;
        }
        return true;
    };
    for (const Master &m : deck.masters) {
        if (!unique(m.id) || !validColor(m.background) || !validColor(m.foreground)
            || m.layouts.isEmpty() || m.layouts.size() > MaximumLayouts
            || !placeholdersValid(m.placeholders, true))
            return QStringLiteral("Invalid master %1").arg(m.id);
        for (const Layout &l : m.layouts) {
            if (!unique(l.id) || !placeholdersValid(l.placeholders, false))
                return QStringLiteral("Invalid layout %1").arg(l.id);
            for (const Placeholder &p : l.placeholders)
                if (!findPlaceholder(m.placeholders, p.role))
                    return QStringLiteral("Layout %1 uses unknown role %2").arg(l.id, p.role);
        }
    }
    for (const Slide &s : deck.slides) {
        if (!unique(s.id)) return QStringLiteral("Duplicate or empty slide ID %1").arg(s.id);
        const Master *m = findMaster(deck, s.masterId);
        if (!m || !findLayout(*m, s.layoutId)) return QStringLiteral("Slide %1 names an unknown master or layout").arg(s.id);
        if (!transitionKinds().contains(s.transition.kind) || s.transition.durationTicks < 0
            || s.transition.durationTicks > MaximumEffectTicks)
            return QStringLiteral("Slide %1 has an invalid transition").arg(s.id);
        if (s.objects.size() > MaximumObjectsPerSlide || s.effects.size() > MaximumEffectsPerSlide)
            return QStringLiteral("Slide %1 exceeds the object or effect limit").arg(s.id);
        QSet<QString> objects, roles;
        for (const SlideObject &o : s.objects) {
            if (!unique(o.id) || !objectKinds().contains(o.kind) || !validRect(o.bounds) || !validColor(o.fill)
                || !(o.fontSize == 0 || (o.fontSize >= 4 && o.fontSize <= 400)))
                return QStringLiteral("Invalid object %1").arg(o.id);
            if (!o.role.isEmpty()) {
                if (roles.contains(o.role)) return QStringLiteral("Slide %1 repeats role %2").arg(s.id, o.role);
                roles.insert(o.role);
            }
            objects.insert(o.id);
        }
        for (const Effect &e : s.effects) {
            if (!unique(e.id) || !objects.contains(e.targetId) || !effectKinds().contains(e.kind)
                || !effectTriggers().contains(e.trigger) || !easingNames().contains(e.easing)
                || e.delayTicks < 0 || e.delayTicks > MaximumEffectTicks
                || e.durationTicks < 0 || e.durationTicks > MaximumEffectTicks)
                return QStringLiteral("Invalid effect %1").arg(e.id);
        }
    }
    return {};
}

} // namespace QindaTK::Slides
