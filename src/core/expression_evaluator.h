// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "locale_service.h"
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Restricted numeric grammar and canonical unit conversion for ExpressionField.
// Grammar: sum := product ((+|-) product)*; product := unary ((*|/) unary)*;
// unary := (+|-) unary | number [unit] | identifier | (sum) |
//          abs(sum) | min(sum;sum) | max(sum;sum) | clamp(sum;sum;sum).
// Comma separates arguments only in dot-decimal locales; semicolon always does.
// Symbols are scoped values: number, {value,unit}, or expression string.
// A dotted identifier is one exact symbol key, never object traversal.
// Limits: 512 UTF-16 code units, 128 tokens, 16 nested calls/parentheses,
// 256 operations. No assignment, property access, calls outside this allowlist,
// arbitrary JavaScript, or implicit file/network access.
class ExpressionEvaluator : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::LocaleService *localeService READ localeService WRITE setLocaleService NOTIFY changed)
    Q_PROPERTY(QVariantMap symbols READ symbols WRITE setSymbols NOTIFY changed)
    Q_PROPERTY(QVariantMap units READ units WRITE setUnits NOTIFY changed)
public:
    explicit ExpressionEvaluator(QObject *parent = nullptr);
    LocaleService *localeService() const { return m_localeService; }
    void setLocaleService(LocaleService *locale);
    QVariantMap symbols() const { return m_symbols; }
    void setSymbols(const QVariantMap &symbols);
    QVariantMap units() const { return m_units; }
    void setUnits(const QVariantMap &units);
    Q_INVOKABLE QVariantMap evaluate(const QString &expression) const;
signals:
    void changed();
private:
    QPointer<LocaleService> m_localeService;
    QVariantMap m_symbols;
    QVariantMap m_units;
};
} // namespace QindaTK
