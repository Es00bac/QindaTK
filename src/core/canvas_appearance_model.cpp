// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_appearance_model.h"
#include "canvas_appearance.h"
#include "canvas_document_format.h"
#include "canvas_selection_model.h"

#include <QSet>

namespace QindaTK {
namespace {
QVariantMap record(const CanvasEffect &effect)
{
    const bool supported = CanvasAppearanceRules::supportedEffect(effect.kind);
    QVariantMap map{{QStringLiteral("id"), effect.id},
                    {QStringLiteral("name"), CanvasAppearanceRules::effectName(effect.kind)},
                    {QStringLiteral("kind"), effect.kind},
                    {QStringLiteral("enabled"), effect.enabled},
                    {QStringLiteral("supported"), supported},
                    {QStringLiteral("parameters"), CanvasAppearanceRules::parameterMap(effect)},
                    {QStringLiteral("masked"), false}};
    if (!supported)
        map.insert(QStringLiteral("unsupportedReason"),
                   CanvasAppearanceModel::tr("Not drawn or exported by the reference canvas; kept in the document"));
    return map;
}
QString colorText(const QColor &color)
{
    return color.alpha() < 255 ? color.name(QColor::HexArgb) : color.name(QColor::HexRgb);
}

// The fixed paint entries in the order the renderer and SVG export paint
// them, before the effects (canvas_appearance_paint.cpp).
QVariantList paintRecords(const CanvasObject &o)
{
    const CanvasAppearance &a = *o.appearance;
    const auto entry = [](const char *id, const QString &name, bool present, const QString &detail) {
        return QVariantMap{{QStringLiteral("id"), QLatin1String(id)}, {QStringLiteral("kind"), QLatin1String(id)},
                           {QStringLiteral("name"), name}, {QStringLiteral("present"), present},
                           {QStringLiteral("detail"), detail}};
    };
    const bool managed = !o.managedFill.isEmpty();
    QVariantList list;
    list.append(entry("fill", CanvasAppearanceModel::tr("Fill"), o.hasFill(),
                      !o.hasFill() ? CanvasAppearanceModel::tr("None")
                      : managed ? CanvasAppearanceModel::tr("Typed colour or gradient")
                                : colorText(o.fill)));
    list.append(entry("pattern", CanvasAppearanceModel::tr("Pattern"), o.hasPattern(),
                      o.hasPattern() ? CanvasAppearanceModel::tr("%1 in %2")
                                           .arg(CanvasAppearanceRules::patternName(a.pattern.kind),
                                                colorText(a.pattern.ink))
                                     : CanvasAppearanceModel::tr("None")));
    list.append(entry("stroke", CanvasAppearanceModel::tr("Stroke"), o.hasStroke(),
                      o.hasStroke() ? CanvasAppearanceModel::tr("%1 %2, %3")
                                          .arg(QString::number(o.strokeWidth),
                                               CanvasAppearanceRules::alignmentName(a.strokeAlignment),
                                               colorText(o.stroke))
                                    : CanvasAppearanceModel::tr("None")));
    return list;
}
} // namespace

void CanvasAppearanceModel::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            refresh();
            emit documentChanged();
        });
        connect(document, &CanvasDocumentAdapter::edited, this, [this] { refresh(); });
        connect(document, &CanvasDocumentAdapter::reset, this, [this] { refresh(); });
    }
    refresh();
    emit documentChanged();
}

void CanvasAppearanceModel::setSelectionModel(CanvasSelectionModel *model)
{
    if (m_selectionModel == model) return;
    if (m_selectionModel) disconnect(m_selectionModel, nullptr, this, nullptr);
    m_selectionModel = model;
    if (model) {
        // The key object is the first selected one.
        const auto follow = [this] {
            const auto &ids = m_selectionModel ? m_selectionModel->selection() : QVector<CanvasObjectId>();
            setObjectId(ids.isEmpty() ? QString() : QString::number(ids.constFirst()));
        };
        connect(model, &CanvasSelectionModel::selectionChanged, this, follow);
        connect(model, &QObject::destroyed, this, [this] {
            m_selectionModel = nullptr;
            emit selectionModelChanged();
        });
        follow();
    }
    emit selectionModelChanged();
}

void CanvasAppearanceModel::setObjectId(const QString &id)
{
    if (m_objectId == id) return;
    m_objectId = id;
    emit objectIdChanged();
    refresh();
}

QStringList CanvasAppearanceModel::supportedKinds() const
{
    return CanvasAppearanceRules::supportedEffects();
}

const CanvasObject *CanvasAppearanceModel::target() const
{
    bool ok = false;
    const CanvasObjectId id = m_objectId.toULongLong(&ok);
    return ok && id != 0 && m_document ? m_document->object(id) : nullptr;
}

qsizetype CanvasAppearanceModel::indexOf(const QString &effectId) const
{
    const CanvasObject *o = target();
    if (!o) return -1;
    for (qsizetype i = 0; i < o->appearance->effects.size(); ++i)
        if (o->appearance->effects.at(i).id == effectId) return i;
    return -1;
}

void CanvasAppearanceModel::refresh()
{
    QVariantList effects;
    const CanvasObject *o = target();
    if (o)
        for (const CanvasEffect &effect : o->appearance->effects) effects.append(record(effect));
    const bool editable = o && m_document->effectiveVisible(*o) && !m_document->effectiveLocked(*o);
    const QVariantList paints = o ? paintRecords(*o) : QVariantList();
    if (effects == m_effects && editable == m_editable && paints == m_paints) return;
    m_effects = effects;
    m_editable = editable;
    m_paints = paints;
    emit effectsChanged();
}

bool CanvasAppearanceModel::fail(const QString &message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit errorChanged();
    }
    return false;
}

bool CanvasAppearanceModel::commit(const QVector<CanvasEffect> &effects, const QString &label)
{
    const CanvasObject *before = target();
    if (!before) return fail(tr("Select an object first"));
    if (!m_document->effectiveVisible(*before) || m_document->effectiveLocked(*before))
        return fail(tr("The object is hidden or locked"));
    CanvasObject after = *before;
    after.appearance.update([&effects](CanvasAppearance &a) { a.effects = effects; });
    if (const OperationError error = CanvasDocumentFormat::validateObject(after, after.id, CanvasDocumentLimits{}))
        return fail(error.message);
    if (after != *before) {
        CanvasEdit edit;
        edit.label = label;
        edit.changes.append({after.id, *before, after});
        if (!m_document->commit(edit)) return fail(tr("The document changed before the effect could commit"));
    }
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit errorChanged();
    }
    emit applied();
    return true;
}

QString CanvasAppearanceModel::addEffect(const QString &kind, const QVariantMap &parameters)
{
    const CanvasObject *o = target();
    if (!o) {
        fail(tr("Select an object first"));
        return {};
    }
    CanvasEffect effect;
    effect.kind = kind;
    if (const OperationError error = CanvasAppearanceRules::canonicalParameters(kind, parameters, &effect.parameters)) {
        fail(error.message);
        return {};
    }
    QSet<QString> used;
    for (const CanvasEffect &existing : o->appearance->effects) used.insert(existing.id);
    const QString stem = kind.size() <= 48 ? kind : QStringLiteral("effect");
    int n = 1;
    while (used.contains(QStringLiteral("%1-%2").arg(stem).arg(n))) ++n;
    effect.id = QStringLiteral("%1-%2").arg(stem).arg(n);
    QVector<CanvasEffect> effects = o->appearance->effects;
    effects.append(effect);
    return commit(effects, tr("Add appearance effect")) ? effect.id : QString();
}

bool CanvasAppearanceModel::removeEffect(const QString &effectId)
{
    const qsizetype index = indexOf(effectId);
    if (index < 0) return fail(tr("No such appearance effect: %1").arg(effectId));
    QVector<CanvasEffect> effects = target()->appearance->effects;
    effects.removeAt(index);
    return commit(effects, tr("Remove appearance effect"));
}

bool CanvasAppearanceModel::moveEffect(const QString &effectId, int index)
{
    const qsizetype from = indexOf(effectId);
    if (from < 0) return fail(tr("No such appearance effect: %1").arg(effectId));
    QVector<CanvasEffect> effects = target()->appearance->effects;
    if (index < 0 || index >= effects.size()) return fail(tr("Invalid effect position"));
    effects.move(from, index);
    return commit(effects, tr("Reorder appearance effects"));
}

bool CanvasAppearanceModel::setEffectEnabled(const QString &effectId, bool enabled)
{
    const qsizetype index = indexOf(effectId);
    if (index < 0) return fail(tr("No such appearance effect: %1").arg(effectId));
    QVector<CanvasEffect> effects = target()->appearance->effects;
    effects[index].enabled = enabled;
    return commit(effects, enabled ? tr("Enable appearance effect") : tr("Bypass appearance effect"));
}

bool CanvasAppearanceModel::setEffectParameters(const QString &effectId, const QVariantMap &parameters)
{
    const qsizetype index = indexOf(effectId);
    if (index < 0) return fail(tr("No such appearance effect: %1").arg(effectId));
    QVector<CanvasEffect> effects = target()->appearance->effects;
    const QString kind = effects.at(index).kind;
    // A supported kind keeps the values a partial map leaves out.
    QVariantMap merged = parameters;
    if (CanvasAppearanceRules::supportedEffect(kind)) {
        merged = CanvasAppearanceRules::parameterMap(effects.at(index));
        for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) merged.insert(it.key(), it.value());
    }
    QByteArray canonical;
    if (const OperationError error = CanvasAppearanceRules::canonicalParameters(kind, merged, &canonical))
        return fail(error.message);
    effects[index].parameters = canonical;
    return commit(effects, tr("Edit appearance effect"));
}

QVariantMap CanvasAppearanceModel::effect(const QString &effectId) const
{
    const qsizetype index = indexOf(effectId);
    return index < 0 ? QVariantMap() : record(target()->appearance->effects.at(index));
}

} // namespace QindaTK
