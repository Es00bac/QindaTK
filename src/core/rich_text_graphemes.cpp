// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_graphemes.h"

#include <QTextBoundaryFinder>

namespace QindaTK::RichText {

static bool regionalIndicatorAt(const QString &text, int start)
{
    if (start < 0 || start + 1 >= text.size() || !text.at(start).isHighSurrogate()
        || !text.at(start + 1).isLowSurrogate())
        return false;
    const char32_t code = QChar::surrogateToUcs4(text.at(start), text.at(start + 1));
    return code >= 0x1F1E6 && code <= 0x1F1FF;
}

GraphemeBoundaries::GraphemeBoundaries(const QString &text)
    : m_boundary(text.size() + 1, false)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    m_boundary[0] = true;
    m_boundary[text.size()] = true;
    for (int position = finder.toNextBoundary(); position > 0; position = finder.toNextBoundary())
        m_boundary[position] = true;
    // GB12/GB13: between regional indicators, break only after an even run.
    int run = 0;
    for (int i = 0; i + 1 < text.size();) {
        if (!regionalIndicatorAt(text, i)) {
            run = 0;
            ++i;
            continue;
        }
        ++run;
        // Pair RIs strictly from the start of the run; never touch the
        // boundary before a following non-RI (GB9 extenders keep theirs).
        if (regionalIndicatorAt(text, i + 2)) m_boundary[i + 2] = run % 2 == 0;
        i += 2;
    }
}

bool GraphemeBoundaries::isBoundary(int position) const
{
    return position <= 0 || position >= length() || m_boundary.at(position);
}

int GraphemeBoundaries::next(int position) const
{
    for (int i = qMax(0, position + 1); i < m_boundary.size(); ++i)
        if (m_boundary.at(i)) return i;
    return length();
}

int GraphemeBoundaries::previous(int position) const
{
    for (int i = qMin(position - 1, length()); i > 0; --i)
        if (m_boundary.at(i)) return i;
    return 0;
}

} // namespace QindaTK::RichText
