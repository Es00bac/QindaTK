// SPDX-License-Identifier: LGPL-3.0-or-later
// Form controls of a local data design (D-311, docs/forms-reports.md#form-controls):
// the positioned controls FormDesigner lays out — labels, bound text, combo,
// list and check boxes, subforms, command buttons, tab controls, images,
// lines and rectangles — as `form.controls`, their structural checks and the
// LocalDataDesignDocument commands that edit them (one undo step each).
#include "local_data_design_document.h"
#include "local_data_design_store.h"

#include <QMetaType>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace QindaTK {
namespace {
constexpr int MaximumControls = 500;
constexpr double MaximumCoordinate = 20000;
constexpr int MaximumText = 1000;
constexpr int MaximumExpression = 4000;
constexpr int MaximumItems = 1000;
constexpr int MaximumPages = 32;

QVariantMap mapAt(const QVariantMap &map, const QString &key) { return map.value(key).toMap(); }

bool isString(const QVariant &value) { return value.metaType().id() == QMetaType::QString; }

bool isNumber(const QVariant &value, double *out)
{
    const int type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::LongLong && type != QMetaType::Double
        && type != QMetaType::UInt && type != QMetaType::ULongLong && type != QMetaType::Float)
        return false;
    *out = value.toDouble();
    return std::isfinite(*out);
}

// Bound types read and write one field of the record.
bool boundType(const QString &type)
{
    return type == QLatin1String("textBox") || type == QLatin1String("comboBox")
        || type == QLatin1String("listBox") || type == QLatin1String("checkBox");
}

QString typeLabel(const QString &type)
{
    static const QHash<QString, const char *> labels{
        {QStringLiteral("label"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "label")},
        {QStringLiteral("textBox"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "text box")},
        {QStringLiteral("comboBox"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "combo box")},
        {QStringLiteral("listBox"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "list box")},
        {QStringLiteral("checkBox"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "check box")},
        {QStringLiteral("subform"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "subform")},
        {QStringLiteral("commandButton"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "command button")},
        {QStringLiteral("tabControl"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "tab control")},
        {QStringLiteral("image"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "image")},
        {QStringLiteral("line"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "line")},
        {QStringLiteral("rectangle"), QT_TRANSLATE_NOOP("LocalDataDesignDocument", "rectangle")}};
    return LocalDataDesignDocument::tr(labels.value(type, "control"));
}

int indexOfControl(const QVariantList &controls, const QString &id)
{
    for (int i = 0; i < controls.size(); ++i)
        if (controls.at(i).toMap().value(QStringLiteral("id")).toString() == id) return i;
    return -1;
}

// "Text3": the type's name and the first number no control of that name uses.
QString uniqueName(const QVariantList &controls, const QString &type)
{
    QString stem = type;
    stem[0] = stem.at(0).toUpper();
    if (stem.endsWith(QLatin1String("Box")) && type != QLatin1String("checkBox")) stem.chop(3);
    QSet<QString> taken;
    for (const QVariant &entry : controls) taken.insert(entry.toMap().value(QStringLiteral("name")).toString());
    for (int n = 1;; ++n)
        if (!taken.contains(stem + QString::number(n))) return stem + QString::number(n);
}
} // namespace

QStringList LocalDataDesignStore::formControlTypes()
{
    return {QStringLiteral("label"), QStringLiteral("textBox"), QStringLiteral("comboBox"),
            QStringLiteral("listBox"), QStringLiteral("checkBox"), QStringLiteral("subform"),
            QStringLiteral("commandButton"), QStringLiteral("tabControl"), QStringLiteral("image"),
            QStringLiteral("line"), QStringLiteral("rectangle")};
}

// AGENT-NOTE: sizes are design units (logical pixels of the form at 100 %),
// document data like a report's page size, not theme chrome.
QVariantMap LocalDataDesignStore::formControlDefaults(const QString &type)
{
    struct Default { const char *type; int width; int height; };
    static const Default sizes[] = {{"label", 96, 24}, {"textBox", 160, 24}, {"comboBox", 160, 24},
                                    {"listBox", 160, 96}, {"checkBox", 120, 24}, {"subform", 320, 160},
                                    {"commandButton", 96, 24}, {"tabControl", 320, 200}, {"image", 96, 96},
                                    {"line", 160, 0}, {"rectangle", 160, 96}};
    QVariantMap control{{QStringLiteral("type"), type}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}};
    for (const Default &entry : sizes) {
        if (type != QLatin1String(entry.type)) continue;
        control.insert(QStringLiteral("width"), entry.width);
        control.insert(QStringLiteral("height"), entry.height);
    }
    if (type == QLatin1String("label")) control.insert(QStringLiteral("caption"), LocalDataDesignDocument::tr("Label"));
    if (type == QLatin1String("checkBox")) control.insert(QStringLiteral("caption"), LocalDataDesignDocument::tr("Check box"));
    if (type == QLatin1String("commandButton")) {
        control.insert(QStringLiteral("caption"), LocalDataDesignDocument::tr("Button"));
        control.insert(QStringLiteral("actionId"), QString());
    }
    if (type == QLatin1String("comboBox") || type == QLatin1String("listBox"))
        control.insert(QStringLiteral("items"), QVariantList{});
    if (type == QLatin1String("subform")) control.insert(QStringLiteral("formName"), QString());
    if (type == QLatin1String("tabControl"))
        control.insert(QStringLiteral("pages"), QVariantList{LocalDataDesignDocument::tr("Page 1"),
                                                             LocalDataDesignDocument::tr("Page 2")});
    if (type == QLatin1String("image")) control.insert(QStringLiteral("source"), QString());
    if (type == QLatin1String("line")) control.insert(QStringLiteral("slant"), QStringLiteral("down"));
    return control;
}

QString LocalDataDesignStore::checkFormControls(const QVariantList &controls)
{
    if (controls.size() > MaximumControls) return tr("A form has at most %1 controls").arg(MaximumControls);
    const QStringList types = formControlTypes();
    QSet<QString> ids;
    QHash<QString, int> tabPages; // tab control id -> page count
    for (const QVariant &entry : controls) {
        if (entry.metaType().id() != QMetaType::QVariantMap) return tr("A form control must be a map");
        const QVariantMap control = entry.toMap();
        const QString id = control.value(QStringLiteral("id")).toString();
        if (!isString(control.value(QStringLiteral("id"))) || id.trimmed().isEmpty() || id.size() > 64 || ids.contains(id))
            return tr("Form controls need unique, nonempty IDs");
        ids.insert(id);
        const QString type = control.value(QStringLiteral("type")).toString();
        if (!types.contains(type)) return tr("Unknown form control type %1").arg(type);
        double geometry[4] = {0, 0, 0, 0};
        const char *keys[] = {"x", "y", "width", "height"};
        for (int k = 0; k < 4; ++k)
            if (!isNumber(control.value(QString::fromLatin1(keys[k])), &geometry[k]) || geometry[k] < 0
                || geometry[k] > MaximumCoordinate)
                return tr("Form control %1 needs x, y, width and height from 0 to %2").arg(id).arg(MaximumCoordinate);
        const bool line = type == QLatin1String("line");
        if ((!line && (geometry[2] < 1 || geometry[3] < 1)) || (line && geometry[2] + geometry[3] < 1))
            return tr("Form control %1 is empty").arg(id);
        for (const char *key : {"name", "caption", "fieldName", "formName", "linkMasterFields", "linkChildFields",
                                "actionId", "rowSource", "source", "parentId", "slant"}) {
            const QVariant value = control.value(QString::fromLatin1(key));
            if (value.isValid() && (!isString(value) || value.toString().size() > MaximumText))
                return tr("Form control %1: %2 must be text of at most %3 characters").arg(id, QLatin1String(key)).arg(MaximumText);
        }
        const QVariant expression = control.value(QStringLiteral("expression"));
        if (expression.isValid() && (!isString(expression) || expression.toString().size() > MaximumExpression))
            return tr("Form control %1: an expression is text of at most %2 characters").arg(id).arg(MaximumExpression);
        if (!control.value(QStringLiteral("fieldName")).toString().isEmpty() && !boundType(type))
            return tr("Form control %1 (%2) cannot bind a field").arg(id, type);
        const QVariant items = control.value(QStringLiteral("items"));
        if (items.isValid()) {
            const QVariantList list = items.toList();
            if (items.metaType().id() != QMetaType::QVariantList || list.size() > MaximumItems)
                return tr("Form control %1: items must be a list of at most %2 values").arg(id).arg(MaximumItems);
            for (const QVariant &item : list)
                if (!isString(item) || item.toString().size() > MaximumText)
                    return tr("Form control %1: items must be text").arg(id);
        }
        const QVariant pages = control.value(QStringLiteral("pages"));
        if (type == QLatin1String("tabControl")) {
            const QVariantList list = pages.toList();
            if (pages.metaType().id() != QMetaType::QVariantList || list.isEmpty() || list.size() > MaximumPages)
                return tr("Tab control %1 needs 1 to %2 pages").arg(id).arg(MaximumPages);
            for (const QVariant &page : list)
                if (!isString(page) || page.toString().size() > MaximumText) return tr("Tab control %1: page names are text").arg(id);
            tabPages.insert(id, int(list.size()));
        } else if (pages.isValid()) {
            return tr("Only a tab control has pages");
        }
        const QString slant = control.value(QStringLiteral("slant")).toString();
        if (!slant.isEmpty() && slant != QLatin1String("down") && slant != QLatin1String("up"))
            return tr("Line %1: slant is down or up").arg(id);
    }
    // A control on a tab page names an existing tab control and one of its pages.
    for (const QVariant &entry : controls) {
        const QVariantMap control = entry.toMap();
        const QString parent = control.value(QStringLiteral("parentId")).toString();
        if (parent.isEmpty()) {
            if (control.contains(QStringLiteral("page"))) return tr("Only a control on a tab page has a page");
            continue;
        }
        double page = -1;
        const QString id = control.value(QStringLiteral("id")).toString();
        if (!tabPages.contains(parent) || parent == id || control.value(QStringLiteral("type")).toString() == QLatin1String("tabControl"))
            return tr("Form control %1 is on an unknown tab control").arg(id);
        if (!isNumber(control.value(QStringLiteral("page")), &page) || page != std::floor(page) || page < 0
            || page >= tabPages.value(parent))
            return tr("Form control %1 is on an unknown tab page").arg(id);
    }
    return {};
}

QString LocalDataDesignDocument::addFormControl(const QVariantMap &control)
{
    const QString type = control.value(QStringLiteral("type")).toString();
    if (!LocalDataDesignStore::formControlTypes().contains(type)) {
        fail(tr("Unknown form control type %1").arg(type));
        return {};
    }
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    QVariantList controls = form.value(QStringLiteral("controls")).toList();
    QVariantMap created = LocalDataDesignStore::formControlDefaults(type);
    for (auto it = control.cbegin(); it != control.cend(); ++it) created.insert(it.key(), it.value());
    QString id = created.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) id = QStringLiteral("ctl-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
    created.insert(QStringLiteral("id"), id);
    if (created.value(QStringLiteral("name")).toString().isEmpty())
        created.insert(QStringLiteral("name"), uniqueName(controls, type));
    controls.append(created);
    form.insert(QStringLiteral("controls"), controls);
    next.insert(QStringLiteral("form"), form);
    return commit(next, tr("Add %1").arg(typeLabel(type))) ? id : QString();
}

bool LocalDataDesignDocument::setFormControl(const QString &controlId, const QVariantMap &changes)
{
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    QVariantList controls = form.value(QStringLiteral("controls")).toList();
    const int i = indexOfControl(controls, controlId);
    if (i < 0) return fail(tr("No form control %1").arg(controlId));
    QVariantMap control = controls.at(i).toMap();
    if (changes.contains(QStringLiteral("id")) && changes.value(QStringLiteral("id")).toString() != controlId)
        return fail(tr("A form control keeps its ID"));
    if (changes.contains(QStringLiteral("type")) && changes.value(QStringLiteral("type")) != control.value(QStringLiteral("type")))
        return fail(tr("A form control keeps its type"));
    bool geometryOnly = true;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        static const QStringList geometry{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                                          QStringLiteral("height")};
        geometryOnly = geometryOnly && geometry.contains(it.key());
        // An invalid (undefined) value removes an optional property.
        if (it.value().isValid() && !it.value().isNull()) control.insert(it.key(), it.value());
        else control.remove(it.key());
    }
    controls[i] = control;
    form.insert(QStringLiteral("controls"), controls);
    next.insert(QStringLiteral("form"), form);
    const QString name = control.value(QStringLiteral("name"), controlId).toString();
    return commit(next, geometryOnly ? tr("Move or size %1").arg(name) : tr("Change %1").arg(name));
}

bool LocalDataDesignDocument::removeFormControl(const QString &controlId)
{
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    const QVariantList controls = form.value(QStringLiteral("controls")).toList();
    const int i = indexOfControl(controls, controlId);
    if (i < 0) return fail(tr("No form control %1").arg(controlId));
    // The controls on a tab control's pages go with it.
    QVariantList kept;
    for (const QVariant &entry : controls) {
        const QVariantMap control = entry.toMap();
        if (control.value(QStringLiteral("id")).toString() != controlId
            && control.value(QStringLiteral("parentId")).toString() != controlId)
            kept.append(entry);
    }
    form.insert(QStringLiteral("controls"), kept);
    next.insert(QStringLiteral("form"), form);
    return commit(next, tr("Delete %1").arg(controls.at(i).toMap().value(QStringLiteral("name"), controlId).toString()));
}

bool LocalDataDesignDocument::setFormControls(const QVariantList &controls)
{
    QVariantMap next = design();
    QVariantMap form = mapAt(next, QStringLiteral("form"));
    form.insert(QStringLiteral("controls"), controls);
    next.insert(QStringLiteral("form"), form);
    return commit(next, tr("Set form controls"));
}

} // namespace QindaTK
