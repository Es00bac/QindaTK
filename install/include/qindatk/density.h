// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace QindaTK {

// AGENT-CONTRACT: the one density authority of a process. Theme's `space`
// and `size` groups multiply their base values by scale(); controls read
// those groups and never Density directly, so a density change re-resolves
// every metric through a single path (Theme::rescale). Compact is the
// measured Sloom Studio scale and the default; Comfortable is 20% roomier;
// Touch is for pen/finger use.
class Density : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString modeName READ modeName WRITE setModeName NOTIFY changed)
    Q_PROPERTY(qreal scale READ scale NOTIFY changed)
    Q_PROPERTY(bool scaleFonts READ scaleFonts WRITE setScaleFonts NOTIFY changed)

public:
    enum Mode { Compact, Comfortable, Touch };
    Q_ENUM(Mode)

    [[nodiscard]] static Density *instance();
    [[nodiscard]] static Density *create(QQmlEngine *engine, QJSEngine *jsEngine);

    [[nodiscard]] Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    [[nodiscard]] QString modeName() const;
    void setModeName(const QString &name);
    [[nodiscard]] qreal scale() const;
    [[nodiscard]] bool scaleFonts() const { return m_scaleFonts; }
    void setScaleFonts(bool scaleFonts);

    // A base metric scaled and rounded to whole pixels; `scaled` keeps the
    // fraction for callers that round themselves.
    Q_INVOKABLE [[nodiscard]] qreal px(qreal value) const;
    Q_INVOKABLE [[nodiscard]] qreal scaled(qreal value) const;

signals:
    void changed();

private:
    explicit Density(QObject *parent = nullptr);

    Mode m_mode = Compact;
    bool m_scaleFonts = false;
};

} // namespace QindaTK
