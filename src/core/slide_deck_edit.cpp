// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_deck.h"
#include "rich_text_codec.h"
#include "rich_text_document.h"
#include "slide_timeline.h"

#include <QHash>
#include <QUuid>

// SlideDeck edits: each builds a candidate DeckState and commits it as one
// undo step through commitState (validation first, no partial change).
namespace QindaTK {
using namespace Slides;

namespace {
std::optional<QRectF> rectFrom(const QVariantMap &map)
{
    bool ok[4] = {false, false, false, false};
    const QRectF r(map.value("x").toDouble(&ok[0]), map.value("y").toDouble(&ok[1]),
                   map.value("width").toDouble(&ok[2]), map.value("height").toDouble(&ok[3]));
    if (!(ok[0] && ok[1] && ok[2] && ok[3])) return std::nullopt;
    return r;
}
std::optional<qint64> ticksFrom(const QVariantMap &patch, const QString &secondsKey, const QString &ticksKey,
                                bool *present)
{
    *present = patch.contains(ticksKey) || patch.contains(secondsKey);
    if (patch.contains(ticksKey)) return TimelineTime::parseDecimal(patch.value(ticksKey).toString());
    if (patch.contains(secondsKey)) {
        bool ok = false;
        const double seconds = patch.value(secondsKey).toDouble(&ok);
        return ok ? secondsToTicks(seconds) : std::nullopt;
    }
    return std::nullopt;
}
Master *masterById(DeckState &deck, const QString &id)
{
    for (Master &m : deck.masters) if (m.id == id) return &m;
    return nullptr;
}
} // namespace

// ------------------------------------------------------------------ slides
QString SlideDeck::insertSlide(const QString &afterSlideId, const QString &layoutId)
{
    DeckState next = m_state;
    const int after = afterSlideId.isEmpty() ? next.slides.size() - 1 : slideIndex(next, afterSlideId);
    if (after < 0) return fail(QStringLiteral("Unknown slide")), QString();
    const Slide &reference = next.slides.at(after);
    Slide slide;
    slide.id = newId(QStringLiteral("slide"));
    slide.masterId = reference.masterId;
    slide.section = reference.section;
    const Master *master = findMaster(next, slide.masterId);
    if (!layoutId.isEmpty() && !findLayout(*master, layoutId)) return fail(QStringLiteral("Unknown layout")), QString();
    // The default follows the reference slide's layout, except after a
    // title slide, where the master's second layout (content) is used.
    slide.layoutId = !layoutId.isEmpty() ? layoutId
        : (reference.layoutId == master->layouts.first().id && master->layouts.size() > 1
               ? master->layouts.at(1).id : reference.layoutId);
    ensureLayoutObjects(next, slide);
    next.slides.insert(after + 1, slide);
    return commitState(next, QStringLiteral("Insert slide")) ? slide.id : QString();
}

QString SlideDeck::duplicateSlide(const QString &slideId)
{
    DeckState next = m_state;
    const int i = slideIndex(next, slideId);
    if (i < 0) return fail(QStringLiteral("Unknown slide")), QString();
    Slide copy = next.slides.at(i);
    copy.id = newId(QStringLiteral("slide"));
    QHash<QString, QString> remap;
    for (SlideObject &o : copy.objects) {
        const QString fresh = newId(QStringLiteral("object"));
        remap.insert(o.id, fresh);
        o.id = fresh;
    }
    for (Effect &e : copy.effects) {
        e.id = newId(QStringLiteral("effect"));
        e.targetId = remap.value(e.targetId);
    }
    next.slides.insert(i + 1, copy);
    return commitState(next, QStringLiteral("Duplicate slide")) ? copy.id : QString();
}

bool SlideDeck::moveSlide(const QString &slideId, int targetIndex)
{
    DeckState next = m_state;
    const int i = slideIndex(next, slideId);
    if (i < 0 || targetIndex < 0 || targetIndex >= next.slides.size())
        return fail(QStringLiteral("Unknown slide or target position"));
    next.slides.move(i, targetIndex);
    return commitState(next, QStringLiteral("Move slide"));
}

bool SlideDeck::removeSlide(const QString &slideId)
{
    DeckState next = m_state;
    const int i = slideIndex(next, slideId);
    if (i < 0) return fail(QStringLiteral("Unknown slide"));
    if (next.slides.size() == 1) return fail(QStringLiteral("A deck keeps at least one slide"));
    next.slides.removeAt(i);
    return commitState(next, QStringLiteral("Delete slide"));
}

bool SlideDeck::setSlideHidden(const QString &slideId, bool hidden)
{
    DeckState next = m_state;
    Slide *s = slideById(next, slideId);
    if (!s) return fail(QStringLiteral("Unknown slide"));
    s->hidden = hidden;
    return commitState(next, hidden ? QStringLiteral("Hide slide") : QStringLiteral("Show slide"));
}

bool SlideDeck::setSlideSection(const QString &slideId, const QString &section)
{
    DeckState next = m_state;
    Slide *s = slideById(next, slideId);
    if (!s || section.size() > 256) return fail(QStringLiteral("Unknown slide or section too long"));
    s->section = section;
    return commitState(next, QStringLiteral("Set section"));
}

bool SlideDeck::setSlideTitle(const QString &slideId, const QString &title)
{
    DeckState next = m_state;
    Slide *s = slideById(next, slideId);
    if (!s || title.size() > 1024) return fail(QStringLiteral("Unknown slide or title too long"));
    s->title = title;
    return commitState(next, QStringLiteral("Rename slide"));
}

bool SlideDeck::setNotes(const QString &slideId, const QString &text)
{
    DeckState next = m_state;
    Slide *s = slideById(next, slideId);
    if (!s) return fail(QStringLiteral("Unknown slide"));
    if (s->notes.plain == text) return true;
    s->notes = TextContent::fromPlain(text);
    return commitState(next, QStringLiteral("Edit notes"));
}

bool SlideDeck::setTransition(const QString &slideId, const QString &kind, double seconds)
{
    DeckState next = m_state;
    Slide *s = slideById(next, slideId);
    const auto ticks = secondsToTicks(seconds);
    if (!s || !ticks || !transitionKinds().contains(kind)) return fail(QStringLiteral("Invalid transition"));
    s->transition = {kind, kind == QStringLiteral("none") ? 0 : *ticks};
    return commitState(next, QStringLiteral("Set transition"));
}

// ----------------------------------------------------------------- objects
QString SlideDeck::insertObject(const QString &slideId, const QString &kind, const QVariantMap &bounds,
                                const QString &text)
{
    DeckState next = m_state;
    Slide *s = slideById(next, slideId);
    const auto rect = rectFrom(bounds);
    if (!s || !rect || !objectKinds().contains(kind)) return fail(QStringLiteral("Invalid object insertion")), QString();
    SlideObject o;
    o.id = newId(QStringLiteral("object"));
    o.kind = kind;
    o.bounds = *rect;
    o.fontSize = kind == QStringLiteral("text") ? 24 : 0;
    o.fill = kind == QStringLiteral("shape") ? QStringLiteral("#4f8fba") : QString();
    o.text = TextContent::fromPlain(text);
    s->objects << o;
    return commitState(next, kind == QStringLiteral("text") ? QStringLiteral("Insert text box")
                                                            : QStringLiteral("Insert shape")) ? o.id : QString();
}

bool SlideDeck::removeObject(const QString &objectId)
{
    DeckState next = m_state;
    const auto [s, o] = locateObject(next, objectId);
    if (s < 0) return fail(QStringLiteral("Unknown object"));
    Slide &slide = next.slides[s];
    slide.objects.removeAt(o);
    slide.effects.removeIf([&objectId](const Effect &e) { return e.targetId == objectId; });
    return commitState(next, QStringLiteral("Delete object"));
}

bool SlideDeck::moveObject(const QString &objectId, const QVariantMap &bounds)
{
    DeckState next = m_state;
    const auto [s, o] = locateObject(next, objectId);
    const auto rect = rectFrom(bounds);
    if (s < 0 || !rect) return fail(QStringLiteral("Unknown object or invalid bounds"));
    SlideObject &object = next.slides[s].objects[o];
    if (object.locked) return fail(QStringLiteral("The object is locked"));
    object.bounds = *rect;
    object.ownBounds = true;
    return commitState(next, QStringLiteral("Move object"));
}

bool SlideDeck::setObjectText(const QString &objectId, const QString &text)
{
    DeckState next = m_state;
    const auto [s, o] = locateObject(next, objectId);
    if (s < 0) return fail(QStringLiteral("Unknown object"));
    SlideObject &object = next.slides[s].objects[o];
    if (object.locked) return fail(QStringLiteral("The object is locked"));
    const QVector<ResolvedObject> resolved = resolveObjects(next, next.slides.at(s));
    if (resolved.at(o).text.plain == text && (object.ownText || object.role.isEmpty())) return true;
    object.text = TextContent::fromPlain(text);
    object.ownText = true;
    return commitState(next, QStringLiteral("Edit text"));
}

bool SlideDeck::setObjectFill(const QString &objectId, const QString &fill)
{
    DeckState next = m_state;
    const auto [s, o] = locateObject(next, objectId);
    if (s < 0) return fail(QStringLiteral("Unknown object"));
    SlideObject &object = next.slides[s].objects[o];
    object.fill = fill;
    object.ownFill = true;
    return commitState(next, QStringLiteral("Set fill"));
}

RichTextDocument *SlideDeck::beginTextEdit(const QString &objectId)
{
    if (textEditing()) return fail(QStringLiteral("Another text box is being edited")), nullptr;
    const auto [s, o] = locateObject(m_state, objectId);
    if (s < 0) return fail(QStringLiteral("Unknown object")), nullptr;
    const ResolvedObject object = resolveObjects(m_state, m_state.slides.at(s)).at(o);
    if (object.kind != QStringLiteral("text") || object.locked)
        return fail(QStringLiteral("Only an unlocked text box has rich text")), nullptr;
    const auto fragment = RichTextCodec::fragmentFromJson(object.text.rich);
    if (!fragment) return fail(QStringLiteral("The text box content is invalid")), nullptr;
    auto *document = new RichTextDocument(this);
    if (!document->insertFragment(document->startPosition(), *fragment)) {
        delete document;
        return fail(QStringLiteral("The text box content could not be opened")), nullptr;
    }
    m_textDocument = document;
    m_textObjectId = objectId;
    emit textEditChanged();
    emit historyChanged();
    return document;
}

bool SlideDeck::commitTextEdit()
{
    if (!textEditing() || !m_textDocument) return fail(QStringLiteral("No text box is being edited"));
    RichTextDocument *document = m_textDocument;
    if (document->inTransaction()) return fail(QStringLiteral("Finish the text edit transaction first"));
    const RichText::Fragment fragment = document->extractFragment(document->startPosition(), document->endPosition());
    const QString objectId = m_textObjectId;
    m_textObjectId.clear();
    m_textDocument = nullptr;
    document->deleteLater();
    emit textEditChanged();
    emit historyChanged();
    const auto content = TextContent::fromFragmentJson(RichTextCodec::fragmentToJson(fragment));
    const auto [s, o] = locateObject(m_state, objectId);
    if (!content || s < 0) return fail(QStringLiteral("The edited text box no longer exists"));
    DeckState next = m_state;
    SlideObject &object = next.slides[s].objects[o];
    object.text = *content;
    object.ownText = true;
    return commitState(next, QStringLiteral("Edit text"));
}

void SlideDeck::cancelTextEdit()
{
    if (!textEditing()) return;
    if (m_textDocument) m_textDocument->deleteLater();
    m_textDocument = nullptr;
    m_textObjectId.clear();
    emit textEditChanged();
    emit historyChanged();
}

// ------------------------------------------------------ masters/inheritance
QString SlideDeck::createMaster(const QString &title)
{
    DeckState next = m_state;
    if (title.trimmed().isEmpty() || title.size() > 256) return fail(QStringLiteral("A master needs a title")), QString();
    Master master = defaultDeck().masters.first();
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    master.id = QStringLiteral("master-") + suffix;
    master.title = title;
    for (Layout &l : master.layouts) l.id += u'-' + suffix;
    next.masters << master;
    return commitState(next, QStringLiteral("Create master")) ? master.id : QString();
}

bool SlideDeck::setMasterBackground(const QString &masterId, const QString &background, const QString &foreground)
{
    DeckState next = m_state;
    Master *m = masterById(next, masterId);
    if (!m) return fail(QStringLiteral("Unknown master"));
    m->background = background;
    if (!foreground.isEmpty()) m->foreground = foreground;
    return commitState(next, QStringLiteral("Master background"));
}

bool SlideDeck::setPlaceholderDefault(const QString &masterId, const QString &role, const QString &text)
{
    DeckState next = m_state;
    Master *m = masterById(next, masterId);
    Placeholder *p = nullptr;
    if (m) for (Placeholder &candidate : m->placeholders) if (candidate.role == role) p = &candidate;
    if (!p) return fail(QStringLiteral("Unknown master or placeholder role"));
    if (p->text.plain == text) return true;
    p->text = TextContent::fromPlain(text);
    return commitState(next, QStringLiteral("Edit master placeholder"));
}

bool SlideDeck::setPlaceholderBounds(const QString &masterId, const QString &layoutId, const QString &role,
                                     const QVariantMap &bounds)
{
    DeckState next = m_state;
    Master *m = masterById(next, masterId);
    const auto rect = rectFrom(bounds);
    if (!m || !rect) return fail(QStringLiteral("Unknown master or invalid bounds"));
    QVector<Placeholder> *list = &m->placeholders;
    if (!layoutId.isEmpty()) {
        list = nullptr;
        for (Layout &l : m->layouts) if (l.id == layoutId) list = &l.placeholders;
        if (!list) return fail(QStringLiteral("Unknown layout"));
    }
    for (Placeholder &p : *list)
        if (p.role == role) {
            p.bounds = *rect;
            return commitState(next, QStringLiteral("Move placeholder"));
        }
    return fail(QStringLiteral("Unknown placeholder role"));
}

bool SlideDeck::applyMaster(const QString &masterId, const QStringList &slideIds, const QString &layoutId)
{
    DeckState next = m_state;
    const Master *master = findMaster(next, masterId);
    if (!master || slideIds.isEmpty()) return fail(QStringLiteral("Unknown master or no slides"));
    if (!layoutId.isEmpty() && !findLayout(*master, layoutId)) return fail(QStringLiteral("Unknown layout"));
    for (const QString &id : slideIds) {
        Slide *s = slideById(next, id);
        if (!s) return fail(QStringLiteral("Unknown slide %1").arg(id));
        // Same layout ID, else the layout with the same title, else the first.
        const Master *previous = findMaster(next, s->masterId);
        const Layout *current = previous ? findLayout(*previous, s->layoutId) : nullptr;
        s->masterId = masterId;
        if (!layoutId.isEmpty()) {
            s->layoutId = layoutId;
        } else if (!findLayout(*master, s->layoutId)) {
            s->layoutId = master->layouts.first().id;
            for (const Layout &candidate : master->layouts)
                if (current && candidate.title == current->title) { s->layoutId = candidate.id; break; }
        }
        // Existing role objects keep their own* overrides (D-158).
        ensureLayoutObjects(next, *s);
    }
    return commitState(next, QStringLiteral("Apply master"));
}

bool SlideDeck::resetOverride(const QString &slideId, const QString &role)
{
    DeckState next = m_state;
    Slide *s = slideById(next, slideId);
    if (!s || role.isEmpty()) return fail(QStringLiteral("Unknown slide or role"));
    for (SlideObject &o : s->objects)
        if (o.role == role) {
            o.ownBounds = o.ownText = o.ownFill = false;
            return commitState(next, QStringLiteral("Reset placeholder override"));
        }
    return fail(QStringLiteral("The slide has no %1 placeholder").arg(role));
}

// ----------------------------------------------------------------- effects
QString SlideDeck::addEffect(const QString &targetId, const QString &kind)
{
    DeckState next = m_state;
    const auto [s, o] = locateObject(next, targetId);
    if (s < 0 || !effectKinds().contains(kind)) return fail(QStringLiteral("Unknown target or effect kind")), QString();
    Q_UNUSED(o)
    Effect e;
    e.id = newId(QStringLiteral("effect"));
    e.targetId = targetId;
    e.kind = kind;
    next.slides[s].effects << e;
    return commitState(next, QStringLiteral("Add animation")) ? e.id : QString();
}

bool SlideDeck::editEffect(const QString &effectId, const QVariantMap &patch)
{
    DeckState next = m_state;
    const auto [s, i] = locateEffect(next, effectId);
    if (s < 0) return fail(QStringLiteral("Unknown effect"));
    Effect &e = next.slides[s].effects[i];
    bool present = false;
    const auto delay = ticksFrom(patch, QStringLiteral("delay"), QStringLiteral("delayTicks"), &present);
    if (present) { if (!delay) return fail(QStringLiteral("Invalid delay")); e.delayTicks = *delay; }
    const auto duration = ticksFrom(patch, QStringLiteral("duration"), QStringLiteral("durationTicks"), &present);
    if (present) { if (!duration) return fail(QStringLiteral("Invalid duration")); e.durationTicks = *duration; }
    if (patch.contains("trigger")) e.trigger = patch.value("trigger").toString();
    if (patch.contains("easing")) e.easing = patch.value("easing").toString();
    if (patch.contains("kind")) e.kind = patch.value("kind").toString();
    if (patch.contains("targetId")) e.targetId = patch.value("targetId").toString();
    return commitState(next, QStringLiteral("Edit animation")); // validate() rejects bad values
}

bool SlideDeck::moveEffect(const QString &effectId, int targetIndex)
{
    DeckState next = m_state;
    const auto [s, i] = locateEffect(next, effectId);
    if (s < 0 || targetIndex < 0 || targetIndex >= next.slides.at(s).effects.size())
        return fail(QStringLiteral("Unknown effect or target position"));
    next.slides[s].effects.move(i, targetIndex);
    return commitState(next, QStringLiteral("Reorder animation"));
}

bool SlideDeck::removeEffect(const QString &effectId)
{
    DeckState next = m_state;
    const auto [s, i] = locateEffect(next, effectId);
    if (s < 0) return fail(QStringLiteral("Unknown effect"));
    next.slides[s].effects.removeAt(i);
    return commitState(next, QStringLiteral("Delete animation"));
}

} // namespace QindaTK
