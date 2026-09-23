// SPDX-License-Identifier: LGPL-3.0-or-later
#include "token_editor_service.h"

#include <QUuid>

namespace QindaTK {
TokenEditorService::TokenEditorService(QObject *parent) : QObject(parent) {}
QVariantMap TokenEditorService::createToken(const QString &label, const QString &value, bool valid) const {
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 512) return {};
    return {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"label", trimmed}, {"value", value.isEmpty() ? trimmed : value},
            {"valid", valid}};
}
QVariantMap TokenEditorService::splitText(const QString &text) const {
    if (text.size() > 8192) return {{"ok", false}, {"error", "Paste exceeds 8192 characters"}};
    QVariantList labels;
    QString current;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == '"') {
            if (quoted && i + 1 < text.size() && text.at(i + 1) == '"') { current += c; ++i; }
            else quoted = !quoted;
        } else if (!quoted && (c == ',' || c == ';' || c == '\n' || c == '\r')) {
            if (!current.trimmed().isEmpty()) labels << current.trimmed();
            current.clear();
            if (labels.size() > 256) return {{"ok", false}, {"error", "Paste exceeds 256 tokens"}};
        } else current += c;
    }
    if (quoted) return {{"ok", false}, {"error", "Unclosed quoted token"}};
    if (!current.trimmed().isEmpty()) labels << current.trimmed();
    if (labels.size() > 256) return {{"ok", false}, {"error", "Paste exceeds 256 tokens"}};
    return {{"ok", true}, {"labels", labels}, {"error", QString()}};
}
bool TokenEditorService::commit(PropertyBinding *binding, const QVariantList &tokens) const {
    if (!binding || !binding->writable() || !binding->begin()) return false;
    if (!binding->update(QVariant::fromValue(tokens))) { binding->cancel(); return false; }
    return binding->commit();
}
} // namespace QindaTK
