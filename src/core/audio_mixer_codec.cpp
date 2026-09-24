// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_mixer_types.h"

#include <QCoreApplication>
#include <algorithm>

namespace QindaTK::AudioMixer {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AudioMixer", text);
}

bool readList(const QVariantMap &map, const QString &key, QVariantList &out, QString *error)
{
    const QVariant value = map.value(key);
    if (!value.isValid()) return true;
    // JSON gives QVariantList; an in-memory contentMap() round trip gives QStringList.
    if (value.typeId() != QMetaType::QVariantList && value.typeId() != QMetaType::QStringList) {
        if (error) *error = tr("Mixer field “%1” must be a list").arg(key);
        return false;
    }
    out = value.toList();
    return true;
}

bool readString(const QVariantMap &map, const QString &key, QString &out, QString *error)
{
    const QVariant value = map.value(key);
    if (!value.isValid()) return true;
    if (value.typeId() != QMetaType::QString) {
        if (error) *error = tr("Mixer field “%1” must be text").arg(key);
        return false;
    }
    out = value.toString();
    return true;
}

bool readBool(const QVariantMap &map, const QString &key, bool &out, QString *error)
{
    const QVariant value = map.value(key);
    if (!value.isValid()) return true;
    if (value.typeId() != QMetaType::Bool) {
        if (error) *error = tr("Mixer field “%1” must be true or false").arg(key);
        return false;
    }
    out = value.toBool();
    return true;
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool readStrip(const QVariantMap &map, AudioMixerContent &content, QString *error)
{
    QString channel;
    if (!readString(map, QStringLiteral("channelId"), channel, error)) return false;
    if (channel.isEmpty()) return fail(error, tr("A mixer strip needs a channelId"));
    if (content.strips.contains(channel)) return fail(error, tr("Channel %1 has two mixer strips").arg(channel));
    AudioMixerStrip settings = defaultStrip(channel);
    QString layout = layoutName(settings.layout);
    QVariantList sends;
    if (!readString(map, QStringLiteral("output"), settings.output, error)
        || !readString(map, QStringLiteral("layout"), layout, error)
        || !readBool(map, QStringLiteral("soloSafe"), settings.soloSafe, error)
        || !readList(map, QStringLiteral("sends"), sends, error))
        return false;
    const auto parsed = layoutFromName(layout);
    if (!parsed) return fail(error, tr("Unsupported channel layout “%1”").arg(layout));
    settings.layout = *parsed;
    for (const QVariant &value : std::as_const(sends)) {
        const QVariantMap entry = value.toMap();
        AudioMixerSend send;
        if (!readString(entry, QStringLiteral("id"), send.id, error)
            || !readString(entry, QStringLiteral("target"), send.target, error)
            || !readBool(entry, QStringLiteral("preFader"), send.preFader, error))
            return false;
        settings.sends.push_back(send);
    }
    content.strips.insert(channel, settings);
    return true;
}

} // namespace

QVariantMap toMap(const AudioMixerContent &content)
{
    QVariantList buses;
    for (const AudioMixerBus &entry : content.buses)
        buses.push_back(QVariantMap{{QStringLiteral("id"), entry.id},
                                    {QStringLiteral("name"), entry.name},
                                    {QStringLiteral("muted"), entry.muted},
                                    {QStringLiteral("soloed"), entry.soloed}});
    QStringList channels = content.strips.keys();
    std::sort(channels.begin(), channels.end()); // stable files for equal mixes
    QVariantList strips;
    for (const QString &channel : std::as_const(channels)) {
        const AudioMixerStrip &settings = content.strips[channel];
        QVariantList sends;
        for (const AudioMixerSend &send : settings.sends)
            sends.push_back(QVariantMap{{QStringLiteral("id"), send.id},
                                        {QStringLiteral("target"), send.target},
                                        {QStringLiteral("preFader"), send.preFader}});
        strips.push_back(QVariantMap{{QStringLiteral("channelId"), channel},
                                     {QStringLiteral("output"), settings.output},
                                     {QStringLiteral("layout"), layoutName(settings.layout)},
                                     {QStringLiteral("soloSafe"), settings.soloSafe},
                                     {QStringLiteral("sends"), sends}});
    }
    QVariantList groups;
    for (const AudioMixerGroup &entry : content.groups)
        groups.push_back(QVariantMap{{QStringLiteral("id"), entry.id},
                                     {QStringLiteral("name"), entry.name},
                                     {QStringLiteral("members"), entry.members},
                                     {QStringLiteral("muted"), entry.muted},
                                     {QStringLiteral("soloed"), entry.soloed}});
    return {{QStringLiteral("schema"), schemaId()},
            {QStringLiteral("panLaw"), panLawName(content.panLaw)},
            {QStringLiteral("masterMuted"), content.masterMuted},
            {QStringLiteral("buses"), buses},
            {QStringLiteral("strips"), strips},
            {QStringLiteral("groups"), groups}};
}

std::optional<AudioMixerContent> fromMap(const QVariantMap &map, QString *error)
{
    AudioMixerContent content;
    if (map.isEmpty()) return content; // a session saved before WP-062: the default mix
    if (map.value(QStringLiteral("schema")).toString() != schemaId()) {
        fail(error, tr("Unsupported mixer schema"));
        return std::nullopt;
    }
    QString law = panLawName(content.panLaw);
    QVariantList buses, strips, groups;
    if (!readString(map, QStringLiteral("panLaw"), law, error)
        || !readBool(map, QStringLiteral("masterMuted"), content.masterMuted, error)
        || !readList(map, QStringLiteral("buses"), buses, error)
        || !readList(map, QStringLiteral("strips"), strips, error)
        || !readList(map, QStringLiteral("groups"), groups, error))
        return std::nullopt;
    const auto parsedLaw = panLawFromName(law);
    if (!parsedLaw) {
        fail(error, tr("Unsupported pan law “%1”").arg(law));
        return std::nullopt;
    }
    content.panLaw = *parsedLaw;
    for (const QVariant &value : std::as_const(buses)) {
        const QVariantMap entry = value.toMap();
        AudioMixerBus bus;
        if (!readString(entry, QStringLiteral("id"), bus.id, error)
            || !readString(entry, QStringLiteral("name"), bus.name, error)
            || !readBool(entry, QStringLiteral("muted"), bus.muted, error)
            || !readBool(entry, QStringLiteral("soloed"), bus.soloed, error))
            return std::nullopt;
        content.buses.push_back(bus);
    }
    for (const QVariant &value : std::as_const(strips))
        if (!readStrip(value.toMap(), content, error)) return std::nullopt;
    for (const QVariant &value : std::as_const(groups)) {
        const QVariantMap entry = value.toMap();
        AudioMixerGroup group;
        QVariantList members;
        if (!readString(entry, QStringLiteral("id"), group.id, error)
            || !readString(entry, QStringLiteral("name"), group.name, error)
            || !readList(entry, QStringLiteral("members"), members, error)
            || !readBool(entry, QStringLiteral("muted"), group.muted, error)
            || !readBool(entry, QStringLiteral("soloed"), group.soloed, error))
            return std::nullopt;
        for (const QVariant &member : std::as_const(members)) group.members.push_back(member.toString());
        content.groups.push_back(group);
    }
    normalize(content);
    if (const QString problem = validate(content); !problem.isEmpty()) {
        fail(error, problem);
        return std::nullopt;
    }
    return content;
}

} // namespace QindaTK::AudioMixer
