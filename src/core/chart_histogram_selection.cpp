// SPDX-License-Identifier: LGPL-3.0-or-later
#include "chart_histogram_selection.h"

namespace QindaTK {

void ChartHistogramSelection::assign(int first, int last, double lower, double upper,
                                     bool final, const QString &unit,
                                     const QString &channel, int normalization)
{
    if (m_valid && m_first == first && m_last == last && m_lower == lower
        && m_upper == upper && m_final == final && m_unit == unit
        && m_channel == channel && m_normalization == normalization) return;
    m_valid = true; m_first = first; m_last = last;
    m_lower = lower; m_upper = upper; m_final = final;
    m_unit = unit; m_channel = channel; m_normalization = normalization;
    emit changed();
}
void ChartHistogramSelection::clear()
{
    if (!m_valid) return;
    m_valid = false; m_first = m_last = -1; m_lower = m_upper = 0;
    m_final = false; m_unit.clear(); m_channel.clear(); m_normalization = 0;
    emit changed();
}

} // namespace QindaTK
