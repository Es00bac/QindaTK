// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_deck.h"
#include "rich_text_document.h"
#include "slide_timeline.h"
#include "undo_service.h"

#include <algorithm>

// Construction, QML snapshots and the DocumentModel history half of
// SlideDeck. Edits are in slide_deck_edit.cpp, persistence in
// slide_deck_codec.cpp and rendering/export in slide_render.cpp.
namespace QindaTK {
using namespace Slides;

namespace {
QVariantMap rectMap(const QRectF &r)
{
    return {{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
}
QString effectLabel(const Effect &effect, const QVector<ResolvedObject> &objects)
{
    QString target = effect.targetId;
    for (const ResolvedObject &o : objects)
        if (o.id == effect.targetId) {
            const QString text = o.text.plain.section(u'\n', 0, 0).trimmed();
            target = !text.isEmpty() ? text : !o.role.isEmpty() ? o.role : o.kind;
        }
    return QStringLiteral("%1 · %2").arg(effect.kind, target.left(40));
}
} // namespace

// AGENT-CONTRACT (docs/document-models.md#commands): one whole-deck step.
// DeckState shares slide/object vectors and text implicitly, so before/after
// cost only what an edit detached. Slides::retainedBytes charges exactly
// that, text at its real size, so the UndoService memory limit bounds a
// history of large text edits (docs/slides.md#limits).
struct SlideDeck::DeckCommand final : DocumentCommand {
    QString documentId;
    quint64 resetGeneration = 0;
    DeckState before, after;
    quint64 retained = 0;
    DeckCommand(QString id, quint64 generation, DeckState oldState, DeckState newState)
        : documentId(std::move(id)), resetGeneration(generation),
          before(std::move(oldState)), after(std::move(newState)),
          retained(sizeof(*this) + Slides::retainedBytes(before, after)) {}
    quint64 retainedBytes() const override { return retained; }
    bool isNoOp() const override { return before == after; }
};

SlideDeck::SlideDeck(QObject *parent) : QObject(parent)
{
    m_documentId = DocumentId::create().toString();
    m_state = defaultDeck();
    m_localHistory = new UndoService(this);
    m_localHistory->setMemoryLimit(quint64(256) * 1024 * 1024);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, HistorySteps);
}

SlideDeck::~SlideDeck() = default; // the text-edit document is a QObject child

// ---------------------------------------------------------------- snapshots
QVariantMap SlideDeck::slideSize() const
{
    return {{"width", m_state.size.width()}, {"height", m_state.size.height()}};
}

QStringList SlideDeck::slideIds() const
{
    QStringList ids;
    for (const Slide &s : m_state.slides) ids << s.id;
    return ids;
}

QVariantList SlideDeck::slideSummaries() const
{
    QVariantList out;
    for (int i = 0; i < m_state.slides.size(); ++i) {
        const Slide &s = m_state.slides.at(i);
        const SlideSchedule plan = Slides::schedule(s);
        out << QVariantMap{{"id", s.id}, {"index", i}, {"title", slideTitle(m_state, s)},
                           {"section", s.section}, {"hidden", s.hidden}, {"status", QString()},
                           {"masterId", s.masterId}, {"layoutId", s.layoutId},
                           {"previewUrl", cachedPreview(s)}, {"effectCount", s.effects.size()},
                           {"buildCount", plan.buildCount}, {"transition", s.transition.kind},
                           {"hasNotes", !s.notes.plain.trimmed().isEmpty()}};
    }
    prunePreviews();
    return out;
}

QVariantList SlideDeck::masterSummaries() const
{
    QVariantList out;
    for (const Master &m : m_state.masters) {
        QVariantList layouts, placeholders;
        for (const Layout &l : m.layouts) {
            QVariantList roles;
            for (const Placeholder &p : l.placeholders)
                roles << QVariantMap{{"role", p.role}, {"bounds", rectMap(p.bounds)}};
            layouts << QVariantMap{{"id", l.id}, {"title", l.title}, {"placeholders", roles}};
        }
        for (const Placeholder &p : m.placeholders) {
            int overrides = 0;
            for (const Slide &s : m_state.slides) {
                if (s.masterId != m.id) continue;
                for (const SlideObject &o : s.objects)
                    if (o.role == p.role && (o.ownText || o.ownBounds || o.ownFill)) ++overrides;
            }
            placeholders << QVariantMap{{"role", p.role}, {"label", p.label}, {"kind", p.kind},
                                        {"value", p.text.plain}, {"prompt", p.prompt}, {"fill", p.fill},
                                        {"fontSize", p.fontSize}, {"bounds", rectMap(p.bounds)},
                                        {"overrideCount", overrides}};
        }
        out << QVariantMap{{"id", m.id}, {"title", m.title}, {"themeId", m.themeId},
                           {"background", m.background}, {"foreground", m.foreground},
                           {"layouts", layouts}, {"placeholders", placeholders}};
    }
    return out;
}

QVariantMap SlideDeck::slide(const QString &slideId) const
{
    const int i = slideIndex(m_state, slideId);
    if (i < 0) return {};
    const Slide &s = m_state.slides.at(i);
    return {{"id", s.id}, {"index", i}, {"title", slideTitle(m_state, s)}, {"storedTitle", s.title},
            {"section", s.section}, {"hidden", s.hidden}, {"masterId", s.masterId},
            {"layoutId", s.layoutId}, {"notes", s.notes.plain},
            {"transition", QVariantMap{{"kind", s.transition.kind},
                                       {"duration", ticksToSeconds(s.transition.durationTicks)},
                                       {"durationTicks", QString::number(s.transition.durationTicks)}}}};
}

int SlideDeck::indexOfSlide(const QString &slideId) const { return slideIndex(m_state, slideId); }

QVariantList SlideDeck::objects(const QString &slideId) const
{
    const int i = slideIndex(m_state, slideId);
    if (i < 0) return {};
    QVariantList out;
    for (const ResolvedObject &o : resolveObjects(m_state, m_state.slides.at(i)))
        out << QVariantMap{{"id", o.id}, {"kind", o.kind}, {"role", o.role}, {"text", o.text.plain},
                           {"prompt", o.prompt}, {"rotation", o.rotation},
                           {"bounds", rectMap(o.bounds)}, {"fill", o.fill}, {"fontSize", o.fontSize},
                           {"locked", o.locked}, {"visible", o.visible},
                           {"inheritedBounds", o.inheritedBounds}, {"inheritedText", o.inheritedText},
                           {"inheritedFill", o.inheritedFill}};
    return out;
}

QVariantList SlideDeck::effects(const QString &slideId) const
{
    const int i = slideIndex(m_state, slideId);
    if (i < 0) return {};
    const Slide &s = m_state.slides.at(i);
    const QVector<ResolvedObject> resolved = resolveObjects(m_state, s);
    const SlideSchedule plan = Slides::schedule(s);
    QVariantList out;
    for (int k = 0; k < s.effects.size(); ++k) {
        const Effect &e = s.effects.at(k);
        const EffectTiming &t = plan.effects.at(k);
        out << QVariantMap{{"id", e.id}, {"targetId", e.targetId}, {"label", effectLabel(e, resolved)},
                           {"kind", e.kind}, {"trigger", e.trigger}, {"easing", e.easing},
                           {"delay", ticksToSeconds(e.delayTicks)}, {"duration", ticksToSeconds(e.durationTicks)},
                           {"delayTicks", QString::number(e.delayTicks)},
                           {"durationTicks", QString::number(e.durationTicks)},
                           {"step", t.step}, {"start", ticksToSeconds(t.start)}, {"end", ticksToSeconds(t.end)},
                           {"status", QString()}};
    }
    return out;
}

QVariantList SlideDeck::targetChoices(const QString &slideId) const
{
    const int i = slideIndex(m_state, slideId);
    if (i < 0) return {};
    QVariantList out;
    for (const ResolvedObject &o : resolveObjects(m_state, m_state.slides.at(i))) {
        const QString text = o.text.plain.section(u'\n', 0, 0).trimmed();
        out << QVariantMap{{"id", o.id}, {"label", text.isEmpty() ? (o.role.isEmpty() ? o.kind : o.role) : text.left(40)}};
    }
    return out;
}

QVariantList SlideDeck::masterChoices() const
{
    QVariantList out;
    for (const Master &m : m_state.masters) out << QVariantMap{{"id", m.id}, {"label", m.title}};
    return out;
}

QStringList SlideDeck::documentColors() const
{
    QStringList out{QStringLiteral("#4f8fba")}; // insertObject's shape fill
    auto add = [&out](const QString &color) {
        if (!color.isEmpty() && out.size() < 16 && !out.contains(color, Qt::CaseInsensitive)) out << color;
    };
    for (const Master &m : m_state.masters) {
        add(m.background);
        add(m.foreground);
        for (const Placeholder &p : m.placeholders) add(p.fill);
    }
    for (const Slide &s : m_state.slides)
        for (const SlideObject &o : s.objects) add(o.fill);
    return out;
}

QVariantList SlideDeck::placeholderOverrides(const QString &masterId) const
{
    QVariantList out;
    for (const Slide &s : m_state.slides) {
        if (s.masterId != masterId) continue;
        for (const SlideObject &o : s.objects)
            if (!o.role.isEmpty() && o.ownText)
                out << QVariantMap{{"slideId", s.id}, {"role", o.role}, {"value", o.text.plain}};
    }
    return out;
}

QString SlideDeck::notes(const QString &slideId) const
{
    const int i = slideIndex(m_state, slideId);
    return i < 0 ? QString() : m_state.slides.at(i).notes.plain;
}

QVariantMap SlideDeck::schedule(const QString &slideId) const
{
    const int i = slideIndex(m_state, slideId);
    if (i < 0) return {};
    const SlideSchedule plan = Slides::schedule(m_state.slides.at(i));
    QStringList steps;
    for (qint64 d : plan.stepDurations) steps << QString::number(d);
    QVariantList effects;
    for (const EffectTiming &t : plan.effects)
        effects << QVariantMap{{"effectId", t.effectId}, {"targetId", t.targetId}, {"step", t.step},
                               {"start", QString::number(t.start)}, {"end", QString::number(t.end)}};
    return {{"buildCount", plan.buildCount}, {"transitionTicks", QString::number(plan.transitionTicks)},
            {"stepDurations", steps}, {"effects", effects}};
}

QVariantMap SlideDeck::frame(const QString &slideId, int step, const QString &ticks) const
{
    const auto parsed = TimelineTime::parseDecimal(ticks.isEmpty() ? QStringLiteral("0") : ticks);
    if (!parsed) return {};
    return frameToVariant(evaluate(m_state, slideId, step, *parsed));
}

// ------------------------------------------------------------------ history
UndoService *SlideDeck::history() const
{
    if (auto *service = historyService()) return service;
    auto *self = const_cast<SlideDeck *>(this);
    if (m_localHistory && m_localHistory->addModel(self)) m_localHistory->setStepLimit(self, HistorySteps);
    return historyService();
}
UndoService *SlideDeck::undoService() const { return history(); }
bool SlideDeck::canUndo() const { return !textEditing() && history() && history()->canUndo(this); }
bool SlideDeck::canRedo() const { return !textEditing() && history() && history()->canRedo(this); }
QString SlideDeck::undoText() const { return history() ? history()->undoText(this) : QString(); }

void SlideDeck::historyAttached(UndoService *service)
{
    disconnect(m_historyConnection);
    m_historyConnection = {};
    if (service)
        m_historyConnection = connect(service, &UndoService::historyChanged, this,
            [this](const QString &id) { if (id == m_documentId) emit historyChanged(); });
    emit historyChanged();
}

SnapshotHeader SlideDeck::modelHeader() const
{
    return {schemaId(), DocumentId::parse(m_documentId), {m_revision}, m_resetGeneration};
}

QMetaObject::Connection SlideDeck::connectModelChanged(QObject *receiver, std::function<void()> slot)
{
    return connect(this, &SlideDeck::changed, receiver, std::move(slot));
}

OperationError SlideDeck::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const DeckCommand *>(&command);
    if (!step || step->documentId != m_documentId || step->resetGeneration != m_resetGeneration)
        return {ErrorCode::StaleGeneration, QStringLiteral("Slide history belongs to another document"), {}, {}};
    if (textEditing())
        return {ErrorCode::Unavailable, QStringLiteral("Finish the text box edit first"), {}, {}};
    if (!(m_state == (forward ? step->before : step->after)))
        return {ErrorCode::StaleRevision, QStringLiteral("Deck changed outside the recorded step"), {}, {}};
    return {};
}

void SlideDeck::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto &step = static_cast<const DeckCommand &>(command);
    m_state = forward ? step.after : step.before;
    ++m_revision;
    m_error.clear();
    emit errorChanged();
    emit changed();
}

DocumentCommandPtr SlideDeck::composeCommands(const DocumentCommand &first, const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const DeckCommand *>(&first);
    const auto *b = dynamic_cast<const DeckCommand *>(&second);
    if (!a || !b || a->documentId != b->documentId || a->resetGeneration != b->resetGeneration
        || !(a->after == b->before)) return {};
    return std::make_shared<const DeckCommand>(a->documentId, a->resetGeneration, a->before, b->after);
}

bool SlideDeck::commitState(const DeckState &after, const QString &label)
{
    if (textEditing()) return fail(QStringLiteral("Finish the text box edit first"));
    const QString problem = validate(after, m_encodedLimit);
    if (!problem.isEmpty()) return fail(problem);
    if (after == m_state) {
        m_error.clear();
        emit errorChanged();
        return true;
    }
    const auto step = std::make_shared<const DeckCommand>(m_documentId, m_resetGeneration, m_state, after);
    const OperationError error = history()->commit(this, label, {}, step);
    if (error) return fail(error.message);
    return true;
}

bool SlideDeck::undo()
{
    if (textEditing()) return fail(QStringLiteral("Finish the text box edit first"));
    const OperationError error = history()->undo(this);
    return error ? fail(error.message) : true;
}

bool SlideDeck::redo()
{
    if (textEditing()) return fail(QStringLiteral("Finish the text box edit first"));
    const OperationError error = history()->redo(this);
    return error ? fail(error.message) : true;
}

void SlideDeck::setEncodedLimit(qsizetype bytes)
{
    m_encodedLimit = std::clamp<qsizetype>(bytes, 128 * 1024, MaximumEncodedBytes);
}

bool SlideDeck::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    return false;
}

Slide *SlideDeck::slideById(DeckState &state, const QString &slideId) const
{
    const int i = slideIndex(state, slideId);
    return i < 0 ? nullptr : &state.slides[i];
}

std::pair<int, int> SlideDeck::locateObject(const DeckState &state, const QString &objectId) const
{
    for (int s = 0; s < state.slides.size(); ++s)
        for (int o = 0; o < state.slides.at(s).objects.size(); ++o)
            if (state.slides.at(s).objects.at(o).id == objectId) return {s, o};
    return {-1, -1};
}

std::pair<int, int> SlideDeck::locateEffect(const DeckState &state, const QString &effectId) const
{
    for (int s = 0; s < state.slides.size(); ++s)
        for (int e = 0; e < state.slides.at(s).effects.size(); ++e)
            if (state.slides.at(s).effects.at(e).id == effectId) return {s, e};
    return {-1, -1};
}

void SlideDeck::adoptLoaded(DeckState state, const SnapshotHeader &header)
{
    // Called by the codec on a fresh deck only: history is re-created clean.
    m_localHistory->removeModel(this);
    m_documentId = header.documentId.toString();
    m_revision = std::max<quint64>(1, header.revision.value);
    m_resetGeneration = header.resetGeneration;
    m_state = std::move(state);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, HistorySteps);
    m_localHistory->markModelClean(this);
}

} // namespace QindaTK
