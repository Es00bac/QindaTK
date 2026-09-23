// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace QindaTK::RichText {

// Extended grapheme cluster boundaries (UAX #29) of one paragraph's text.
// AGENT-NOTE: Qt 6.11.1 QTextBoundaryFinder misses GB12/GB13 when a regional
// indicator follows an Extended_Pictographic character (qunicodetools.cpp
// checks GB11 on the previous class first), so "👧🇺🇸" splits the flag. This
// class applies the pairing rule on top of Qt's result; see
// tst_rich_text_layout::graphemeClustersAndBidiRuns and docs/rich-text.md.
class GraphemeBoundaries {
public:
    explicit GraphemeBoundaries(const QString &text);
    bool isBoundary(int position) const;
    int next(int position) const;      // first boundary after position
    int previous(int position) const;  // last boundary before position
    int length() const { return int(m_boundary.size()) - 1; }
private:
    QVector<bool> m_boundary;          // size text.size() + 1
};

} // namespace QindaTK::RichText
