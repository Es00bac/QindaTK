// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_deck_types.h"

#include <QHash>

// Resource accounting for SlideDeck (docs/slides.md#limits): the encoded
// size bound validate() enforces at edit time and the heap an undo step
// retains. Both are upper estimates by construction; the tests check them
// against real encodings and text sizes (tst_slide_integrity).
namespace QindaTK::Slides {
namespace {
// Compact JSON writes a double in at most 24 characters ("-1.2345678901234567e+308")
// and a decimal tick string in 22 (quotes included).
constexpr qsizetype NumberBytes = 26;
// Key names, punctuation and booleans of one record, rounded up.
constexpr qsizetype ObjectFixed = 160;
constexpr qsizetype EffectFixed = 120;
constexpr qsizetype SlideFixed = 200;
constexpr qsizetype MasterFixed = 160;
constexpr qsizetype PlaceholderFixed = 120;
constexpr qsizetype LayoutFixed = 64;

// JSON string size: quotes plus escaped UTF-8 (a surrogate half counts 3,
// so a pair's 6 bounds its 4 UTF-8 bytes).
qsizetype stringBytes(const QString &s)
{
    qsizetype n = 2;
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        n += u < 0x20 ? 6 : (u == u'"' || u == u'\\') ? 2 : u < 0x80 ? 1 : u < 0x800 ? 2 : 3;
    }
    return n;
}
qsizetype textBytes(const TextContent &t) { return std::max<qsizetype>(2, t.bytes); }

qsizetype placeholderBytes(const Placeholder &p, bool master)
{
    qsizetype n = PlaceholderFixed + stringBytes(p.role) + 4 * NumberBytes;
    if (master)
        n += stringBytes(p.label) + stringBytes(p.kind) + stringBytes(p.fill) + stringBytes(p.prompt)
            + NumberBytes + textBytes(p.text);
    return n;
}
qsizetype objectBytes(const SlideObject &o)
{
    return ObjectFixed + stringBytes(o.id) + stringBytes(o.kind) + stringBytes(o.role) + stringBytes(o.fill)
        + 6 * NumberBytes + textBytes(o.text);
}
qsizetype effectBytes(const Effect &e)
{
    return EffectFixed + stringBytes(e.id) + stringBytes(e.targetId) + stringBytes(e.kind)
        + stringBytes(e.trigger) + stringBytes(e.easing) + 2 * NumberBytes;
}
qsizetype slideBytes(const Slide &s)
{
    qsizetype n = SlideFixed + stringBytes(s.id) + stringBytes(s.title) + stringBytes(s.section)
        + stringBytes(s.masterId) + stringBytes(s.layoutId) + stringBytes(s.transition.kind) + NumberBytes
        + textBytes(s.notes);
    for (const SlideObject &o : s.objects) n += objectBytes(o);
    for (const Effect &e : s.effects) n += effectBytes(e);
    return n;
}
qsizetype masterBytes(const Master &m)
{
    qsizetype n = MasterFixed + stringBytes(m.id) + stringBytes(m.title) + stringBytes(m.themeId)
        + stringBytes(m.background) + stringBytes(m.foreground);
    for (const Placeholder &p : m.placeholders) n += placeholderBytes(p, true);
    for (const Layout &l : m.layouts) {
        n += LayoutFixed + stringBytes(l.id) + stringBytes(l.title);
        for (const Placeholder &p : l.placeholders) n += placeholderBytes(p, false);
    }
    return n;
}

// In memory a text costs its UTF-16 plain copy plus the fragment's
// QJsonObject (string data at most two bytes per JSON byte, plus element
// records): twice the compact JSON size estimates text-dominated fragments.
quint64 textMemory(const TextContent &t) { return quint64(t.plain.size()) * 2 + quint64(t.bytes) * 2 + 64; }
// Two TextContents share storage when their plain strings do (plain and
// rich are always assigned together).
bool sharedText(const TextContent &a, const TextContent &b) { return a.plain.constData() == b.plain.constData(); }

quint64 slideMemory(const Slide &s)
{
    quint64 n = sizeof(Slide) + textMemory(s.notes) + quint64(s.effects.size()) * sizeof(Effect);
    for (const SlideObject &o : s.objects) n += sizeof(SlideObject) + textMemory(o.text);
    return n;
}
quint64 masterMemory(const Master &m) { return quint64(masterBytes(m)) * 4; }

// What `after` detached from `before` for one slide present in both.
quint64 slideDelta(const Slide &before, const Slide &after)
{
    quint64 n = 0;
    if (!sharedText(before.notes, after.notes)) n += textMemory(before.notes) + textMemory(after.notes);
    if (before.effects.constData() != after.effects.constData())
        n += quint64(before.effects.size() + after.effects.size()) * sizeof(Effect);
    if (before.objects.constData() == after.objects.constData()) return n;
    n += quint64(before.objects.size() + after.objects.size()) * sizeof(SlideObject);
    QHash<QString, const SlideObject *> previous;
    previous.reserve(before.objects.size());
    for (const SlideObject &o : before.objects) previous.insert(o.id, &o);
    for (const SlideObject &o : after.objects) {
        const SlideObject *old = previous.take(o.id);
        if (!old) n += textMemory(o.text);
        else if (!sharedText(old->text, o.text)) n += textMemory(old->text) + textMemory(o.text);
    }
    for (const SlideObject *removed : std::as_const(previous)) n += textMemory(removed->text);
    return n;
}
} // namespace

qsizetype encodedBytesUpperBound(const DeckState &deck)
{
    qsizetype n = 0;
    for (const Master &m : deck.masters) n += masterBytes(m);
    for (const Slide &s : deck.slides) n += slideBytes(s);
    return n;
}

quint64 retainedBytes(const DeckState &before, const DeckState &after)
{
    quint64 n = quint64(before.slides.size() + after.slides.size()) * sizeof(Slide)
        + quint64(before.masters.size() + after.masters.size()) * sizeof(Master);
    if (before.masters.constData() != after.masters.constData()) {
        for (const Master &m : before.masters) n += masterMemory(m);
        for (const Master &m : after.masters) n += masterMemory(m);
    }
    if (before.slides.constData() == after.slides.constData()) return n;
    QHash<QString, const Slide *> previous;
    previous.reserve(before.slides.size());
    for (const Slide &s : before.slides) previous.insert(s.id, &s);
    for (const Slide &s : after.slides) {
        const Slide *old = previous.take(s.id);
        n += old ? slideDelta(*old, s) : slideMemory(s);
    }
    for (const Slide *removed : std::as_const(previous)) n += slideMemory(*removed);
    return n;
}

} // namespace QindaTK::Slides
