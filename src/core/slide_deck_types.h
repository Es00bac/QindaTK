// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_time.h"

#include <QJsonObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace QindaTK::Slides {

// AGENT-NOTE: slide time is exact integer ticks in the shared timeline
// timebase (docs/time-map.md, D-159); seconds are presentation values only.
inline constexpr qint64 TicksPerSecond = TimelineTime::kDefaultTicksPerSecond;
inline constexpr qint64 MaximumEffectTicks = TicksPerSecond * 600;  // 10 minutes
inline constexpr int MaximumSlides = 2000;
inline constexpr int MaximumObjectsPerSlide = 500;
inline constexpr int MaximumEffectsPerSlide = 500;
inline constexpr int MaximumMasters = 64;
inline constexpr int MaximumLayouts = 64;
inline constexpr int MaximumPlaceholders = 32;
inline constexpr double MaximumCoordinate = 100000.0;

// Text box content: the canonical `qindatk.richtext.fragment` JSON of the
// rich-text stack (docs/rich-text.md#persistence) plus its derived plain
// text. `rich` is authoritative; `plain` is recomputed whenever it is set.
struct TextContent {
    QJsonObject rich;
    QString plain;
    static TextContent fromPlain(const QString &text);
    // Validates through RichTextCodec::fragmentFromJson; nullopt when invalid.
    static std::optional<TextContent> fromFragmentJson(const QJsonObject &json,
                                                       QString *problem = nullptr);
    friend bool operator==(const TextContent &a, const TextContent &b) { return a.rich == b.rich; }
};

// A master placeholder carries the defaults of one role; a layout
// placeholder only positions a role (its text, fill and size come from the
// master). Colors are document data (#rrggbb / #aarrggbb), not theme roles.
struct Placeholder {
    QString role;
    QString label;
    QString kind = QStringLiteral("text"); // text | shape
    QRectF bounds;
    QString fill;          // empty: transparent
    double fontSize = 24;  // slide pixels
    TextContent text;
    friend bool operator==(const Placeholder &, const Placeholder &) = default;
};

struct Layout {
    QString id;
    QString title;
    QVector<Placeholder> placeholders; // role + bounds
    friend bool operator==(const Layout &, const Layout &) = default;
};

struct Master {
    QString id;
    QString title;
    QString themeId;
    QString background = QStringLiteral("#ffffff");
    QString foreground = QStringLiteral("#1a1a1a");
    QVector<Placeholder> placeholders;
    QVector<Layout> layouts;
    friend bool operator==(const Master &, const Master &) = default;
};

// A slide object. A non-empty `role` binds it to a placeholder: bounds, text
// and fill then inherit (layout -> master) unless the matching own* flag
// records a deliberate per-slide override (D-158).
struct SlideObject {
    QString id;
    QString kind = QStringLiteral("text"); // text | shape
    QString role;
    QRectF bounds;
    QString fill;
    double fontSize = 0; // 0: inherit (placeholder) or 24
    TextContent text;
    bool locked = false;
    bool visible = true;
    bool ownBounds = false;
    bool ownText = false;
    bool ownFill = false;
    friend bool operator==(const SlideObject &, const SlideObject &) = default;
};

struct Effect {
    QString id;
    QString targetId;
    QString kind = QStringLiteral("fade-in");  // appear | fade-in | fly-in | zoom-in | fade-out
    QString trigger = QStringLiteral("click"); // click | with | after
    qint64 delayTicks = 0;
    qint64 durationTicks = TicksPerSecond / 2;
    QString easing = QStringLiteral("out-cubic"); // linear | in-cubic | out-cubic | in-out-cubic
    friend bool operator==(const Effect &, const Effect &) = default;
};

struct Transition {
    QString kind = QStringLiteral("none"); // none | fade | push
    qint64 durationTicks = 0;
    friend bool operator==(const Transition &, const Transition &) = default;
};

struct Slide {
    QString id;
    QString title;   // fallback when the title placeholder is empty
    QString section;
    QString masterId;
    QString layoutId;
    bool hidden = false;
    TextContent notes;
    Transition transition;
    QVector<SlideObject> objects;
    QVector<Effect> effects;
    friend bool operator==(const Slide &, const Slide &) = default;
};

struct DeckState {
    QSizeF size{960, 540};
    QVector<Master> masters;
    QVector<Slide> slides;
    friend bool operator==(const DeckState &, const DeckState &) = default;
};

// Object after inheritance: what the canvas, the evaluator and every
// renderer draw. `inheritedBounds/Text/Fill` say where a value came from.
struct ResolvedObject {
    QString id;
    QString kind;
    QString role;
    QRectF bounds;
    QString fill;
    double fontSize = 24;
    TextContent text;
    bool locked = false;
    bool visible = true;
    bool inheritedBounds = false;
    bool inheritedText = false;
    bool inheritedFill = false;
};

const QStringList &objectKinds();
const QStringList &effectKinds();
const QStringList &effectTriggers();
const QStringList &easingNames();
const QStringList &transitionKinds();
bool isEntryEffect(const QString &kind);

int slideIndex(const DeckState &deck, const QString &slideId);
const Master *findMaster(const DeckState &deck, const QString &masterId);
const Layout *findLayout(const Master &master, const QString &layoutId);
const Placeholder *findPlaceholder(const QVector<Placeholder> &list, const QString &role);
QVector<ResolvedObject> resolveObjects(const DeckState &deck, const Slide &slide);
QString slideTitle(const DeckState &deck, const Slide &slide);

// The state a new deck starts with: one "Default" master with title,
// subtitle and body placeholders, title/content layouts and one title slide.
DeckState defaultDeck();
// A new placeholder-bound object for every role of the slide's layout that
// the slide does not have yet (IDs from QUuid).
void ensureLayoutObjects(const DeckState &deck, Slide &slide);
QString newId(const QString &prefix);

// First problem of a structurally complete deck (unique IDs, references,
// bounds, colors, enums, time ranges); empty when valid.
QString validate(const DeckState &deck);

} // namespace QindaTK::Slides
