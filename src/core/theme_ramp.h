// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace QindaTK {

class ThemeColors;

// AGENT-CONTRACT: one named value->colour scale. Telemetry views (Graph,
// Meter, Sparkline) colour a reading by magnitude rather than by series, the
// way btop does: a CPU core at 95% must read as "hot" without the reader
// consulting a legend. Stops are derived from theme roles in theme_ramp.cpp,
// never authored as literals, so a ramp restyles with its theme.
//
// AGENT-GUARD: positions are kept sorted and clamped to [0, 1] by setStops().
// at() assumes that ordering and interpolates between neighbours; feeding it
// unsorted stops silently returns the wrong colour rather than failing.
class ThemeRamp : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList stops READ stops NOTIFY changed)
    Q_PROPERTY(QList<QColor> colors READ colors NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    struct Stop final {
        qreal position;
        QColor color;
    };

    explicit ThemeRamp(QObject *parent = nullptr);

    // The colour at `t` in [0, 1], linearly interpolated between stops.
    // Out-of-range values clamp to the first/last stop. An empty ramp
    // returns an invalid colour.
    Q_INVOKABLE [[nodiscard]] QColor at(qreal t) const;
    // The colour for `value` rescaled from [from, to] onto [0, 1]. A zero
    // or inverted span returns the ramp's first colour.
    Q_INVOKABLE [[nodiscard]] QColor forValue(qreal value, qreal from, qreal to) const;

    [[nodiscard]] QVariantList stops() const;
    [[nodiscard]] QList<QColor> colors() const;
    [[nodiscard]] int count() const { return static_cast<int>(m_stops.size()); }
    [[nodiscard]] const std::vector<Stop> &stopTable() const { return m_stops; }

    // Replaces every stop. Entries are clamped to [0, 1] and sorted by
    // position; invalid colours are dropped. Emits changed() once.
    void setStops(std::vector<Stop> stops);

signals:
    void changed();

private:
    std::vector<Stop> m_stops;
};

// AGENT-CONTRACT: the ramp set published as `Theme.ramp`. Theme owns it and
// calls rebuild() from finishUpdate(), so a ramp is never stale with respect
// to the colour roles it is derived from. Adding a ramp means: a Q_PROPERTY
// here, a member, a case in rebuild() and in byName(), and a row in the
// docs/theming.md ramp table that tst_theme_ramp checks.
class ThemeRamps : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QindaTK::ThemeRamp *load READ load CONSTANT)
    Q_PROPERTY(QindaTK::ThemeRamp *thermal READ thermal CONSTANT)
    Q_PROPERTY(QindaTK::ThemeRamp *memory READ memory CONSTANT)
    Q_PROPERTY(QindaTK::ThemeRamp *network READ network CONSTANT)
    Q_PROPERTY(QindaTK::ThemeRamp *io READ io CONSTANT)
    Q_PROPERTY(QindaTK::ThemeRamp *neutral READ neutral CONSTANT)

public:
    explicit ThemeRamps(QObject *parent = nullptr);

    [[nodiscard]] ThemeRamp *load() const { return m_load; }
    [[nodiscard]] ThemeRamp *thermal() const { return m_thermal; }
    [[nodiscard]] ThemeRamp *memory() const { return m_memory; }
    [[nodiscard]] ThemeRamp *network() const { return m_network; }
    [[nodiscard]] ThemeRamp *io() const { return m_io; }
    [[nodiscard]] ThemeRamp *neutral() const { return m_neutral; }

    // Null for an unknown name; the names are the property names above.
    Q_INVOKABLE [[nodiscard]] QindaTK::ThemeRamp *byName(const QString &name) const;
    [[nodiscard]] static QStringList names();

    // Re-derives every ramp from the current colour roles.
    void rebuild(const ThemeColors &colors);

private:
    ThemeRamp *m_load = nullptr;
    ThemeRamp *m_thermal = nullptr;
    ThemeRamp *m_memory = nullptr;
    ThemeRamp *m_network = nullptr;
    ThemeRamp *m_io = nullptr;
    ThemeRamp *m_neutral = nullptr;
};

} // namespace QindaTK
