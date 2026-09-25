// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
// MusicXML import internals shared by score_musicxml_read.cpp and
// score_musicxml_read_notes.cpp (WP-067, D-181). Not installed.
#include "score_types.h"

#include <QHash>
#include <QSet>
#include <QXmlStreamReader>

namespace QindaTK::ScoreXml {

inline QUuid fromXmlId(const QString &text)
{
    const qsizetype dash = text.indexOf(QLatin1Char('-'));
    const QUuid id = dash > 0 ? QUuid::fromString(text.mid(dash + 1)) : QUuid();
    return id.isNull() ? QUuid::createUuid() : id;
}

// Visits the children of the current element: elements through `element`
// (which must consume them) and processing instructions through `pi`.
template<typename Element, typename Instruction>
void xmlChildren(QXmlStreamReader &xml, Element element, Instruction pi)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) element(xml.name().toString());
        else if (xml.isEndElement()) return;
        else if (xml.isProcessingInstruction()) pi(xml.processingInstructionTarget().toString(),
                                                   xml.processingInstructionData().toString().trimmed());
    }
}

template<typename Element>
void xmlChildren(QXmlStreamReader &xml, Element element)
{
    xmlChildren(xml, element, [](const QString &, const QString &) {});
}

struct XmlNote {
    bool chord = false, rest = false, grace = false, cue = false, hasType = false;
    ScorePitch pitch;
    ScoreDuration duration;
    qint64 divisions = 0;
    bool tieStart = false;
    int staff = 1;
    QString voice = QStringLiteral("1");
    QString id;
    QStringList articulations;
};

struct PartState {
    int part = -1;
    qint64 divisions = 1;
    int staves = 1;
    QVector<ScoreClef> clefs{ScoreClef::Treble};
    QHash<int, QUuid> staffIds;                  // 0-based staff
    QHash<QPair<int, int>, QUuid> voiceIds;      // (staff, number)
    QHash<QPair<int, QString>, int> voiceNumbers; // (staff, MusicXML voice) -> 1..4
    QHash<QPair<int, int>, QVector<ScoreEvent>> events;
    QHash<QPair<int, QString>, QString> dynamics; // pending, (staff, voice or "")
    QUuid pendingEvent;
};

struct Import {
    ScoreContent content;
    QHash<QString, int> partIndex; // MusicXML part id -> index
    QSet<QString> unsupported; // content the model cannot keep
    QSet<QString> recomputed;  // engraving and file metadata the layout and export regenerate
    int pickups = 0;
    int mismatches = 0;
    int rounded = 0;
    QString error;
};

inline QStringList sortedList(const QSet<QString> &set)
{
    QStringList list(set.begin(), set.end());
    list.sort();
    return list;
}

// AGENT-GUARD (D-181): every element the reader skips lands in one of the
// two reports; a bare skipCurrentElement() would lose content silently.
inline void unsupported(Import &import, QXmlStreamReader &xml, const QString &name)
{
    import.unsupported.insert(name);
    xml.skipCurrentElement();
}

inline void recomputed(Import &import, QXmlStreamReader &xml, const QString &name)
{
    import.recomputed.insert(name);
    xml.skipCurrentElement();
}

XmlNote readNote(QXmlStreamReader &xml, Import &import);
void readDirection(QXmlStreamReader &xml, Import &import, PartState &state);
void addNote(Import &import, PartState &state, const XmlNote &note, qint64 &cursor);

} // namespace QindaTK::ScoreXml
