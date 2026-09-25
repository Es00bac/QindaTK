// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_codec.h"
#include "slide_deck.h"
#include "undo_service.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

// qindatk.slides.v1: the SlideDeck JSON format and its DocumentCodec
// (docs/slides.md#persistence). Encoding is canonical (QJsonObject sorts
// keys; arrays keep document order), so save -> reopen -> save is stable.
namespace QindaTK {
using namespace Slides;

namespace {
constexpr qsizetype MaximumBytes = MaximumEncodedBytes;
const char *const Format = "qindatk.slides";

QJsonArray rectJson(const QRectF &r) { return {r.x(), r.y(), r.width(), r.height()}; }
std::optional<QRectF> rectOf(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 4) return std::nullopt;
    for (const QJsonValue &v : a) if (!v.isDouble()) return std::nullopt;
    return QRectF(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(), a[3].toDouble());
}
QJsonObject placeholderJson(const Placeholder &p, bool master)
{
    QJsonObject o{{"role", p.role}, {"bounds", rectJson(p.bounds)}};
    if (master) {
        o["label"] = p.label; o["kind"] = p.kind; o["fill"] = p.fill;
        o["fontSize"] = p.fontSize; o["text"] = p.text.rich;
        if (!p.prompt.isEmpty()) o["prompt"] = p.prompt;
    }
    return o;
}

struct Reader {
    QString problem;
    bool fail(const QString &message) { if (problem.isEmpty()) problem = message; return false; }
    bool text(const QJsonValue &value, TextContent *out)
    {
        if (!value.isObject()) return fail(QStringLiteral("Missing text fragment"));
        // An empty object is empty text (new slides have no notes fragment).
        if (value.toObject().isEmpty()) { *out = TextContent{}; return true; }
        QString why;
        const auto content = TextContent::fromFragmentJson(value.toObject(), &why);
        if (!content) return fail(why);
        *out = *content;
        return true;
    }
    bool ticks(const QJsonValue &value, qint64 *out)
    {
        const auto parsed = TimelineTime::parseDecimal(value.toString());
        if (!parsed) return fail(QStringLiteral("Time values are decimal tick strings"));
        *out = *parsed;
        return true;
    }
    bool placeholder(const QJsonObject &o, bool master, Placeholder *p)
    {
        const auto bounds = rectOf(o.value("bounds"));
        if (!bounds) return fail(QStringLiteral("Placeholder bounds are [x, y, width, height]"));
        p->role = o.value("role").toString();
        p->bounds = *bounds;
        if (!master) return true;
        p->label = o.value("label").toString();
        p->kind = o.value("kind").toString(QStringLiteral("text"));
        p->fill = o.value("fill").toString();
        p->fontSize = o.value("fontSize").toDouble(24);
        p->prompt = o.value("prompt").toString();
        return text(o.value("text"), &p->text);
    }
    bool object(const QJsonObject &o, SlideObject *out)
    {
        const auto bounds = rectOf(o.value("bounds"));
        if (!bounds) return fail(QStringLiteral("Object bounds are [x, y, width, height]"));
        out->id = o.value("id").toString();
        out->kind = o.value("kind").toString();
        out->role = o.value("role").toString();
        out->bounds = *bounds;
        out->fill = o.value("fill").toString();
        out->fontSize = o.value("fontSize").toDouble(0);
        if (o.contains("rotation") && !o.value("rotation").isDouble())
            return fail(QStringLiteral("Object rotation is a number of degrees"));
        out->rotation = o.value("rotation").toDouble(0);
        out->locked = o.value("locked").toBool(false);
        out->visible = o.value("visible").toBool(true);
        const QJsonArray own = o.value("own").toArray();
        for (const QJsonValue &v : own) {
            const QString key = v.toString();
            if (key == QStringLiteral("bounds")) out->ownBounds = true;
            else if (key == QStringLiteral("text")) out->ownText = true;
            else if (key == QStringLiteral("fill")) out->ownFill = true;
            else return fail(QStringLiteral("Unknown override %1").arg(key));
        }
        return text(o.value("text"), &out->text);
    }
    bool effect(const QJsonObject &o, Effect *e)
    {
        e->id = o.value("id").toString();
        e->targetId = o.value("targetId").toString();
        e->kind = o.value("kind").toString();
        e->trigger = o.value("trigger").toString();
        e->easing = o.value("easing").toString();
        return ticks(o.value("delay"), &e->delayTicks) && ticks(o.value("duration"), &e->durationTicks);
    }
    bool slide(const QJsonObject &o, Slide *s)
    {
        s->id = o.value("id").toString();
        s->title = o.value("title").toString();
        s->section = o.value("section").toString();
        s->masterId = o.value("masterId").toString();
        s->layoutId = o.value("layoutId").toString();
        s->hidden = o.value("hidden").toBool(false);
        const QJsonObject transition = o.value("transition").toObject();
        s->transition.kind = transition.value("kind").toString(QStringLiteral("none"));
        if (!ticks(transition.value("duration").toString(QStringLiteral("0")), &s->transition.durationTicks)
            || !text(o.value("notes"), &s->notes)) return false;
        const QJsonArray objects = o.value("objects").toArray();
        const QJsonArray effects = o.value("effects").toArray();
        if (objects.size() > MaximumObjectsPerSlide || effects.size() > MaximumEffectsPerSlide)
            return fail(QStringLiteral("Slide %1 exceeds the object or effect limit").arg(s->id));
        for (const QJsonValue &v : objects) {
            SlideObject object;
            if (!this->object(v.toObject(), &object)) return false;
            s->objects << object;
        }
        for (const QJsonValue &v : effects) {
            Effect e;
            if (!effect(v.toObject(), &e)) return false;
            s->effects << e;
        }
        return true;
    }
};
} // namespace

QByteArray SlideDeck::encodeState(const DeckState &state, const SnapshotHeader &header,
                                  const QJsonObject &extensions)
{
    QJsonArray masters, slides;
    for (const Master &m : state.masters) {
        QJsonArray placeholders, layouts;
        for (const Placeholder &p : m.placeholders) placeholders << placeholderJson(p, true);
        for (const Layout &l : m.layouts) {
            QJsonArray roles;
            for (const Placeholder &p : l.placeholders) roles << placeholderJson(p, false);
            layouts << QJsonObject{{"id", l.id}, {"title", l.title}, {"placeholders", roles}};
        }
        masters << QJsonObject{{"id", m.id}, {"title", m.title}, {"themeId", m.themeId},
                               {"background", m.background}, {"foreground", m.foreground},
                               {"placeholders", placeholders}, {"layouts", layouts}};
    }
    for (const Slide &s : state.slides) {
        QJsonArray objects, effects;
        for (const SlideObject &o : s.objects) {
            QJsonArray own;
            if (o.ownBounds) own << QStringLiteral("bounds");
            if (o.ownText) own << QStringLiteral("text");
            if (o.ownFill) own << QStringLiteral("fill");
            QJsonObject object{{"id", o.id}, {"kind", o.kind}, {"role", o.role},
                               {"bounds", rectJson(o.bounds)}, {"fill", o.fill},
                               {"fontSize", o.fontSize}, {"text", o.text.rich},
                               {"locked", o.locked}, {"visible", o.visible}, {"own", own}};
            if (o.rotation != 0) object["rotation"] = o.rotation;
            objects << object;
        }
        for (const Effect &e : s.effects)
            effects << QJsonObject{{"id", e.id}, {"targetId", e.targetId}, {"kind", e.kind},
                                   {"trigger", e.trigger}, {"easing", e.easing},
                                   {"delay", QString::number(e.delayTicks)},
                                   {"duration", QString::number(e.durationTicks)}};
        slides << QJsonObject{{"id", s.id}, {"title", s.title}, {"section", s.section},
                              {"masterId", s.masterId}, {"layoutId", s.layoutId}, {"hidden", s.hidden},
                              {"notes", s.notes.rich},
                              {"transition", QJsonObject{{"kind", s.transition.kind},
                                                         {"duration", QString::number(s.transition.durationTicks)}}},
                              {"objects", objects}, {"effects", effects}};
    }
    const QJsonObject root{{"format", QString::fromLatin1(Format)}, {"version", 1},
                           {"documentId", header.documentId.toString()},
                           {"revision", header.revision.toDecimal()},
                           {"resetGeneration", QString::number(header.resetGeneration)},
                           {"timebase", QString::number(TicksPerSecond)},
                           {"extensions", extensions},
                           {"size", QJsonObject{{"width", state.size.width()}, {"height", state.size.height()}}},
                           {"masters", masters}, {"slides", slides}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool SlideDeck::decodeState(const QByteArray &bytes, DeckState *state, SnapshotHeader *header,
                            QJsonObject *extensions, QString *problem)
{
    auto failWith = [problem](const QString &message) { if (problem) *problem = message; return false; };
    if (bytes.size() > MaximumBytes) return failWith(QStringLiteral("Slide deck exceeds 64 MiB"));
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    const QJsonObject root = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return failWith(QStringLiteral("Slide deck is not JSON: %1").arg(parse.errorString()));
    if (root.value("format").toString() != QLatin1String(Format) || root.value("version").toInt() != 1)
        return failWith(QStringLiteral("Not a qindatk.slides version 1 document"));
    if (root.value("timebase").toString() != QString::number(TicksPerSecond))
        return failWith(QStringLiteral("Unsupported slide timebase"));
    const DocumentId id = DocumentId::parse(root.value("documentId").toString());
    const auto revision = RevisionId::parseDecimal(root.value("revision").toString());
    const auto generation = TimelineTime::parseDecimal(root.value("resetGeneration").toString());
    if (!id.isValid() || !revision || !generation || *generation < 0)
        return failWith(QStringLiteral("Invalid document identity or revision"));
    const QJsonArray masters = root.value("masters").toArray();
    const QJsonArray slides = root.value("slides").toArray();
    if (masters.size() > MaximumMasters || slides.size() > MaximumSlides)
        return failWith(QStringLiteral("Slide deck exceeds the master or slide limit"));
    DeckState out;
    Reader reader;
    const QJsonObject size = root.value("size").toObject();
    out.size = QSizeF(size.value("width").toDouble(), size.value("height").toDouble());
    for (const QJsonValue &mv : masters) {
        const QJsonObject mo = mv.toObject();
        Master m;
        m.id = mo.value("id").toString();
        m.title = mo.value("title").toString();
        m.themeId = mo.value("themeId").toString();
        m.background = mo.value("background").toString();
        m.foreground = mo.value("foreground").toString();
        const QJsonArray placeholders = mo.value("placeholders").toArray();
        const QJsonArray layouts = mo.value("layouts").toArray();
        if (placeholders.size() > MaximumPlaceholders || layouts.size() > MaximumLayouts)
            return failWith(QStringLiteral("Master %1 exceeds its limits").arg(m.id));
        for (const QJsonValue &pv : placeholders) {
            Placeholder p;
            if (!reader.placeholder(pv.toObject(), true, &p)) return failWith(reader.problem);
            m.placeholders << p;
        }
        for (const QJsonValue &lv : layouts) {
            const QJsonObject lo = lv.toObject();
            Layout l{lo.value("id").toString(), lo.value("title").toString(), {}};
            const QJsonArray roles = lo.value("placeholders").toArray();
            if (roles.size() > MaximumPlaceholders) return failWith(QStringLiteral("Layout %1 exceeds its limits").arg(l.id));
            for (const QJsonValue &pv : roles) {
                Placeholder p;
                if (!reader.placeholder(pv.toObject(), false, &p)) return failWith(reader.problem);
                l.placeholders << p;
            }
            m.layouts << l;
        }
        out.masters << m;
    }
    for (const QJsonValue &sv : slides) {
        Slide s;
        if (!reader.slide(sv.toObject(), &s)) return failWith(reader.problem);
        out.slides << s;
    }
    const QString invalid = validate(out);
    if (!invalid.isEmpty()) return failWith(invalid);
    if (state) *state = std::move(out);
    if (header) *header = {schemaId(), id, *revision, quint64(*generation)};
    if (extensions) *extensions = root.value("extensions").toObject();
    return true;
}

// -------------------------------------------------------- live persistence
QString SlideDeck::toJson() const
{
    return QString::fromUtf8(encodeState(m_state, modelHeader(), {}));
}

bool SlideDeck::fromJson(const QString &json)
{
    DeckState incoming;
    QString problem;
    if (!decodeState(json.toUtf8(), &incoming, nullptr, nullptr, &problem)) return fail(problem);
    // The live deck keeps its identity; loading content is one undo step.
    return commitState(incoming, QStringLiteral("Load slide deck"));
}

bool SlideDeck::saveToFile(const QString &path)
{
    QSaveFile file(path);
    const QByteArray bytes = toJson().toUtf8();
    // Never write what loadFromFile/decodeState would refuse.
    if (bytes.size() > MaximumBytes)
        return fail(QStringLiteral("The slide deck exceeds 64 MiB and cannot be saved"));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("Cannot save slide deck: %1").arg(file.errorString()));
    m_error.clear();
    emit errorChanged();
    return true;
}

bool SlideDeck::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumBytes)
        return fail(QStringLiteral("Cannot read the slide deck or it exceeds 64 MiB"));
    return fromJson(QString::fromUtf8(file.readAll()));
}

// ------------------------------------------------------------ DocumentCodec
namespace {
OperationError codecError(ErrorCode code, const QString &message)
{
    return {code, message, {}, QStringLiteral("slides.codec")};
}
class SlideCapture final : public DocumentCapture {
public:
    SlideCapture(SnapshotHeader header, DeckState state, QJsonObject extensions)
        : m_header(std::move(header)), m_state(std::move(state)), m_extensions(std::move(extensions)) {}
    SnapshotHeader header() const override { return m_header; }
    QByteArray encode(OperationError *error) const override
    {
        if (error) *error = {};
        return SlideDeck::encodeState(m_state, m_header, m_extensions);
    }
private:
    SnapshotHeader m_header;
    DeckState m_state;
    QJsonObject m_extensions;
};
class SlideDecoded final : public DecodedDocument {
public:
    SlideDecoded(SnapshotHeader header, DeckState state, QJsonObject extensions)
        : m_header(std::move(header)), state(std::move(state)), m_extensions(std::move(extensions)) {}
    SnapshotHeader header() const override { return m_header; }
    QJsonObject extensions() const override { return m_extensions; }
    SnapshotHeader m_header;
    DeckState state;
    QJsonObject m_extensions;
};
} // namespace

class SlideDeckDocumentCodec final : public DocumentCodec {
public:
    QString schemaId() const override { return SlideDeck::schemaId(); }
    QString kind() const override { return QStringLiteral("slides"); }
    QString formatId() const override { return QString::fromLatin1(Format); }
    QString fileSuffix() const override { return QStringLiteral("qtkslides"); }
    qsizetype maximumEncodedBytes() const override { return MaximumBytes; }
    bool recognizes(const QByteArray &prefix) const override
    {
        return prefix.trimmed().startsWith('{') && prefix.contains("\"format\":\"qindatk.slides\"");
    }
    std::optional<SnapshotHeader> inspect(const QByteArray &bytes, OperationError *error) const override
    {
        SnapshotHeader header;
        QString problem;
        if (!SlideDeck::decodeState(bytes, nullptr, &header, nullptr, &problem)) {
            if (error) *error = codecError(ErrorCode::Validation, problem);
            return std::nullopt;
        }
        if (error) *error = {};
        return header;
    }
    DecodedDocumentPtr decode(const QByteArray &bytes, OperationError *error) const override
    {
        DeckState state;
        SnapshotHeader header;
        QJsonObject extensions;
        QString problem;
        if (!SlideDeck::decodeState(bytes, &state, &header, &extensions, &problem)) {
            if (error) *error = codecError(ErrorCode::Validation, problem);
            return {};
        }
        if (error) *error = {};
        return std::make_shared<const SlideDecoded>(header, std::move(state), extensions);
    }
    std::unique_ptr<DocumentModel> create() const override { return std::make_unique<SlideDeck>(); }
    std::unique_ptr<DocumentModel> adopt(const DecodedDocument &decoded, OperationError *error) const override
    {
        const auto *source = dynamic_cast<const SlideDecoded *>(&decoded);
        if (!source) {
            if (error) *error = codecError(ErrorCode::InvalidArgument, QStringLiteral("Not a slide deck"));
            return {};
        }
        auto deck = std::make_unique<SlideDeck>();
        deck->adoptLoaded(source->state, source->m_header);
        if (error) *error = {};
        return deck;
    }
    DocumentCapturePtr capture(const DocumentModel &model, const QJsonObject &extensions,
                               OperationError *error) const override
    {
        const auto *deck = dynamic_cast<const SlideDeck *>(&model);
        if (!deck) {
            if (error) *error = codecError(ErrorCode::InvalidArgument, QStringLiteral("Not a slide deck"));
            return {};
        }
        if (QJsonDocument(extensions).toJson(QJsonDocument::Compact).size() > 64 * 1024) {
            if (error) *error = codecError(ErrorCode::Unsupported, QStringLiteral("Deck extensions exceed 64 KiB"));
            return {};
        }
        // A save during a text box edit persists the last committed state.
        if (error) *error = {};
        return std::make_shared<const SlideCapture>(deck->modelHeader(), deck->state(), extensions);
    }
};

DocumentCodecPtr slideDeckDocumentCodec()
{
    static const DocumentCodecPtr codec = std::make_shared<const SlideDeckDocumentCodec>();
    return codec;
}

} // namespace QindaTK
