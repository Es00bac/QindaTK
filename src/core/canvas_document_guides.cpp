// SPDX-License-Identifier: LGPL-3.0-or-later
// Document guides: validation, the saved extensions record and the QML
// facade whose calls each commit one undoable CanvasEdit.
#include "canvas_document.h"
#include "canvas_document_format.h"

#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
OperationError problem(ErrorCode code, const QString &message, const QString &field)
{
    return {code, message, field, QStringLiteral("canvas.guides")};
}

QString orientationName(Qt::Orientation orientation)
{
    return orientation == Qt::Vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal");
}

std::optional<Qt::Orientation> parseOrientation(const QString &name)
{
    if (name == QLatin1String("vertical")) return Qt::Vertical;
    if (name == QLatin1String("horizontal")) return Qt::Horizontal;
    return std::nullopt;
}
} // namespace

namespace CanvasDocumentFormat {
OperationError validateGuides(const QVector<CanvasGuide> &guides, const CanvasDocumentLimits &limits)
{
    if (guides.size() > limits.maximumGuides)
        return problem(ErrorCode::Unsupported, QStringLiteral("Too many guides"), QStringLiteral("guides"));
    QSet<QString> seen;
    for (const CanvasGuide &guide : guides) {
        if (guide.id.isEmpty() || guide.id.size() > limits.maximumGuideIdLength || seen.contains(guide.id))
            return problem(ErrorCode::Validation, QStringLiteral("Invalid or duplicate guide id"), QStringLiteral("guides.id"));
        if (!std::isfinite(guide.position))
            return problem(ErrorCode::Validation, QStringLiteral("Guide position is not finite"), QStringLiteral("guides.position"));
        seen.insert(guide.id);
    }
    return {};
}

QJsonObject extensionsWithGuides(const CanvasDocumentState &state)
{
    QJsonObject extensions = state.extensions;
    extensions.remove(QLatin1String(GuideExtensionKey));
    if (state.guides.isEmpty() && state.nextGuide <= 1) return extensions;
    QJsonArray items;
    for (const CanvasGuide &guide : state.guides)
        items.append(QJsonObject{{QStringLiteral("id"), guide.id},
                                 {QStringLiteral("orientation"), orientationName(guide.orientation)},
                                 {QStringLiteral("position"), guide.position}});
    // The counter stays exact below 2^53, far beyond maximumGuides per session.
    extensions.insert(QLatin1String(GuideExtensionKey),
                      QJsonObject{{QStringLiteral("next"), double(state.nextGuide)},
                                  {QStringLiteral("items"), items}});
    return extensions;
}

OperationError takeGuides(const QJsonObject &extensions, const CanvasDocumentLimits &limits,
                          CanvasDocumentState *state)
{
    state->extensions = extensions;
    state->guides.clear();
    state->nextGuide = 1;
    const QJsonValue record = state->extensions.take(QLatin1String(GuideExtensionKey));
    if (record.isUndefined()) return {};
    const QJsonObject object = record.toObject();
    const QJsonValue next = object.value(QStringLiteral("next"));
    const QJsonValue items = object.value(QStringLiteral("items"));
    const double counter = next.toDouble(-1);
    if (!record.isObject() || !items.isArray() || !next.isDouble() || counter < 1
        || counter > 9007199254740992.0 || std::floor(counter) != counter)
        return problem(ErrorCode::Validation, QStringLiteral("Invalid guide record"), QStringLiteral("guides"));
    const QJsonArray array = items.toArray();
    if (array.size() > limits.maximumGuides)
        return problem(ErrorCode::Unsupported, QStringLiteral("Too many guides"), QStringLiteral("guides"));
    for (const QJsonValue &entry : array) {
        const QJsonObject item = entry.toObject();
        const auto orientation = parseOrientation(item.value(QStringLiteral("orientation")).toString());
        const QJsonValue position = item.value(QStringLiteral("position"));
        if (!entry.isObject() || !item.value(QStringLiteral("id")).isString() || !orientation
            || !position.isDouble())
            return problem(ErrorCode::Validation, QStringLiteral("Invalid guide"), QStringLiteral("guides"));
        state->guides.append({item.value(QStringLiteral("id")).toString(), *orientation, position.toDouble()});
    }
    state->nextGuide = quint64(counter);
    return validateGuides(state->guides, limits);
}
} // namespace CanvasDocumentFormat

QVariantList CanvasDocument::guideList() const
{
    QVariantList result;
    result.reserve(m_guides.size());
    for (const CanvasGuide &guide : m_guides)
        result.append(QVariantMap{{QStringLiteral("id"), guide.id},
                                  {QStringLiteral("orientation"), orientationName(guide.orientation)},
                                  {QStringLiteral("position"), guide.position}});
    return result;
}

QString CanvasDocument::addGuide(const QString &orientation, qreal position)
{
    const auto axis = parseOrientation(orientation);
    if (!axis || !std::isfinite(position)) return {};
    QSet<QString> used;
    for (const CanvasGuide &guide : std::as_const(m_guides)) used.insert(guide.id);
    // AGENT-NOTE: the counter is saved and never reused, so an id survives
    // save/reopen and an undone add cannot hand its id to a different guide.
    quint64 next = m_nextGuide;
    QString id = QStringLiteral("guide-%1").arg(next);
    while (used.contains(id)) id = QStringLiteral("guide-%1").arg(++next);
    CanvasEdit edit;
    edit.label = tr("Add guide");
    QVector<CanvasGuide> after = m_guides;
    after.append({id, *axis, position});
    edit.guides = CanvasEdit::GuideChange{m_guides, after};
    const quint64 previous = m_nextGuide;
    m_nextGuide = next + 1;
    if (!commit(edit)) {
        m_nextGuide = previous;
        return {};
    }
    return id;
}

bool CanvasDocument::moveGuide(const QString &id, qreal position)
{
    if (!std::isfinite(position)) return false;
    QVector<CanvasGuide> after = m_guides;
    for (CanvasGuide &guide : after) {
        if (guide.id != id) continue;
        if (guide.position == position) return false;
        guide.position = position;
        CanvasEdit edit;
        edit.label = tr("Move guide");
        edit.guides = CanvasEdit::GuideChange{m_guides, after};
        return commit(edit);
    }
    return false;
}

bool CanvasDocument::removeGuide(const QString &id)
{
    QVector<CanvasGuide> after = m_guides;
    if (!after.removeIf([&id](const CanvasGuide &guide) { return guide.id == id; })) return false;
    CanvasEdit edit;
    edit.label = tr("Delete guide");
    edit.guides = CanvasEdit::GuideChange{m_guides, after};
    return commit(edit);
}

} // namespace QindaTK
