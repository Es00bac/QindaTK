// SPDX-License-Identifier: LGPL-3.0-or-later
#include "imap_keywords.h"

#include "imap_mutf7.h"

namespace QindaTK::ImapKeywords {
namespace {
constexpr qsizetype MaximumKeyword = 1024;

const QStringList &systemFlags()
{
    static const QStringList flags{QStringLiteral("\\Seen"), QStringLiteral("\\Answered"), QStringLiteral("\\Flagged"),
                                   QStringLiteral("\\Deleted"), QStringLiteral("\\Draft")};
    return flags;
}

// AGENT-NOTE: Thunderbird and SpamAssassin-era servers mark junk state with
// these keywords without "$"; read as categories they would tag most of an
// inbox "NonJunk". A category with one of these names is written with its
// first letter in a run ("&AEo-unk"), so it still reads back exactly.
bool isJunkMarker(const QString &keyword)
{
    for (const char *marker : {"Junk", "NonJunk", "NotJunk"})
        if (keyword.compare(QLatin1String(marker), Qt::CaseInsensitive) == 0) return true;
    return false;
}

// A character a category keyword writes as itself ("_" and "&" mean
// something in the convention, so they are always encoded).
bool plainCategoryChar(QChar c) { return isAtomChar(c) && c != u'_' && c != u'&'; }
} // namespace

bool isAtomChar(QChar c)
{
    const char16_t u = c.unicode();
    if (u <= 0x20 || u >= 0x7f) return false;
    return !QStringView(u"(){%*\"\\]").contains(c);
}

bool validKeyword(const QString &keyword)
{
    if (keyword.isEmpty() || keyword.size() > MaximumKeyword) return false;
    for (const QChar c : keyword)
        if (!isAtomChar(c)) return false;
    return true;
}

bool isSystemFlag(const QString &flag)
{
    for (const QString &system : systemFlags())
        if (flag.compare(system, Qt::CaseInsensitive) == 0) return true;
    return false;
}

QString canonicalFlag(const QString &flag)
{
    for (const QString &system : systemFlags())
        if (flag.compare(system, Qt::CaseInsensitive) == 0) return system;
    return flag;
}

bool validFlag(const QString &flag)
{
    if (!flag.startsWith(u'\\')) return validKeyword(flag);
    const QString atom = flag.mid(1);
    return validKeyword(atom) && atom.compare(QLatin1String("Recent"), Qt::CaseInsensitive) != 0;
}

QStringList keywordsOf(const QStringList &flags)
{
    QStringList keywords;
    for (const QString &flag : flags)
        if (!flag.startsWith(u'\\') && !containsFlag(keywords, flag)) keywords.append(flag);
    return keywords;
}

bool containsFlag(const QStringList &flags, const QString &flag)
{
    for (const QString &candidate : flags)
        if (candidate.compare(flag, Qt::CaseInsensitive) == 0) return true;
    return false;
}

bool isPermanent(const QString &flag, const QStringList &permanentFlags, bool permanentFlagsSent)
{
    if (!permanentFlagsSent) return true;
    if (containsFlag(permanentFlags, canonicalFlag(flag))) return true;
    return !flag.startsWith(u'\\') && permanentFlags.contains(QStringLiteral("\\*"));
}

QString keywordForCategory(const QString &name)
{
    const QString trimmed = name.trimmed();
    QByteArray out;
    QString run;
    const auto flush = [&] {
        out += Imap::encodeRun(run);
        run.clear();
    };
    for (qsizetype i = 0; i < trimmed.size(); ++i) {
        const QChar c = trimmed.at(i);
        const bool leadingSpecial = i == 0 && (c == u'$' || c == u'\\');
        if (c == u' ') {
            flush();
            out += '_';
        } else if (c == u'&' && run.isEmpty()) {
            out += "&-";
        } else if (plainCategoryChar(c) && !leadingSpecial) {
            flush();
            out += static_cast<char>(c.unicode());
        } else {
            run.append(c);
        }
    }
    flush();
    QString keyword = QString::fromLatin1(out);
    if (isJunkMarker(keyword))
        keyword = QString::fromLatin1(Imap::encodeRun(keyword.left(1))) + keyword.mid(1);
    return keyword;
}

bool isCategoryKeyword(const QString &keyword)
{
    if (!validKeyword(keyword) || keyword.startsWith(u'$') || isJunkMarker(keyword)) return false;
    return true;
}

QString categoryForKeyword(const QString &keyword)
{
    if (!isCategoryKeyword(keyword)) return {};
    QString wire = keyword;
    wire.replace(u'_', u' ');      // the modified base64 alphabet has no "_"
    bool ok = false;
    const QString name = Imap::decodeMailboxName(wire.toLatin1(), &ok);
    return (ok ? name : wire).trimmed();
}

QStringList categoriesOf(const QStringList &flags)
{
    QStringList categories;
    for (const QString &flag : flags) {
        const QString name = categoryForKeyword(flag);
        if (!name.isEmpty() && !categories.contains(name, Qt::CaseInsensitive)) categories.append(name);
    }
    return categories;
}

} // namespace QindaTK::ImapKeywords
