// SPDX-License-Identifier: LGPL-3.0-or-later
#include "flex.h"

#include <QtQml/qqml.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace QindaTK {

namespace {

struct FlexEntry final {
    QQuickItem *item = nullptr;
    FlexAttached *attached = nullptr;
    qreal grow = 0;
    qreal shrink = 1;
    qreal hypo = 0;       // hypothetical main size (clamped basis)
    qreal minMain = 0;
    qreal maxMain = LayoutAttachedBase::unbounded;
    qreal target = 0;     // resolved main size
    qreal crossHypo = 0;
    qreal minCross = 0;
    qreal maxCross = LayoutAttachedBase::unbounded;
    qreal crossSize = 0;
    int alignSelf = Flex::Auto;
    bool frozen = false;
    qreal violation = 0;
};

qreal clampTo(qreal value, qreal lo, qreal hi)
{
    return std::max(lo, std::min(value, hi));
}

bool takesSlot(const QQuickItem *child, const FlexAttached *attached)
{
    if (!child->isVisible()) {
        return false;
    }
    if (child->inherits("QQuickRepeater")) {
        return false;
    }
    return attached == nullptr || !attached->ignore();
}

// CSS 9.7 "Resolving Flexible Lengths": grow or shrink the line's items
// into the free space, freezing items that hit a min or max until none
// violates its bounds.
void resolveLine(std::vector<FlexEntry *> &line, qreal mainAvail, qreal gap)
{
    const qreal gaps = gap * static_cast<qreal>(std::max<size_t>(line.size(), 1) - 1);
    if (mainAvail <= 0) {
        for (FlexEntry *e : line) {
            e->target = e->hypo;
        }
        return;
    }
    qreal sumHypo = 0;
    for (FlexEntry *e : line) {
        sumHypo += e->hypo;
    }
    const bool growing = mainAvail - gaps - sumHypo > 0;
    for (FlexEntry *e : line) {
        e->frozen = false;
        e->target = e->hypo;
        const bool inflexible = growing ? e->grow <= 0 : e->shrink <= 0;
        const bool atBound = growing ? e->hypo >= e->maxMain : e->hypo <= e->minMain;
        if (inflexible || atBound) {
            e->frozen = true;
            e->target = clampTo(e->hypo, e->minMain, e->maxMain);
        }
    }
    for (int iteration = 0; iteration < 64; ++iteration) {
        qreal frozenTotal = 0;
        qreal unfrozenHypo = 0;
        qreal totalFactor = 0;
        int unfrozen = 0;
        for (FlexEntry *e : line) {
            if (e->frozen) {
                frozenTotal += e->target;
            } else {
                ++unfrozen;
                unfrozenHypo += e->hypo;
                totalFactor += growing ? e->grow : e->shrink * e->hypo;
            }
        }
        if (unfrozen == 0) {
            break;
        }
        const qreal free = mainAvail - gaps - frozenTotal - unfrozenHypo;
        qreal totalViolation = 0;
        for (FlexEntry *e : line) {
            if (e->frozen) {
                continue;
            }
            qreal target = e->hypo;
            if (totalFactor > 0) {
                const qreal factor = growing ? e->grow : e->shrink * e->hypo;
                target = e->hypo + free * factor / totalFactor;
            }
            const qreal clamped = clampTo(target, e->minMain, e->maxMain);
            e->violation = clamped - target;
            e->target = clamped;
            totalViolation += e->violation;
        }
        if (std::abs(totalViolation) < 0.01) {
            for (FlexEntry *e : line) {
                e->frozen = true;
            }
            break;
        }
        for (FlexEntry *e : line) {
            if (e->frozen) {
                continue;
            }
            if ((totalViolation > 0 && e->violation > 0) || (totalViolation < 0 && e->violation < 0)) {
                e->frozen = true;
            }
        }
    }
}

// Start offset and per-gap extra for a justify/align-content distribution.
void distribute(Flex::Alignment mode, qreal free, int count, qreal &offset, qreal &between)
{
    offset = 0;
    between = 0;
    const qreal safeFree = std::max<qreal>(free, 0);
    switch (mode) {
    case Flex::End:
        offset = safeFree;
        break;
    case Flex::Center:
        offset = safeFree / 2;
        break;
    case Flex::SpaceBetween:
        between = count > 1 ? safeFree / (count - 1) : 0;
        break;
    case Flex::SpaceAround:
        between = count > 0 ? safeFree / count : 0;
        offset = between / 2;
        break;
    case Flex::SpaceEvenly:
        between = safeFree / (count + 1);
        offset = between;
        break;
    default:
        break;
    }
}

} // namespace

// ---- FlexAttached --------------------------------------------------------

FlexAttached::FlexAttached(QObject *parent)
    : LayoutAttachedBase(parent)
{
}

void FlexAttached::setFlex(qreal value)
{
    if (m_grow == value && m_shrink == 1 && m_basis == 0) {
        return;
    }
    m_grow = value;
    m_shrink = 1;
    m_basis = 0;
    emit changed();
    relayoutParent();
}

// ---- Flex ----------------------------------------------------------------

Flex::Flex(QQuickItem *parent)
    : QQuickItem(parent)
{
}

Flex::~Flex() = default;

FlexAttached *Flex::qmlAttachedProperties(QObject *object)
{
    return new FlexAttached(object);
}

void Flex::invalidateLayout()
{
    if (m_inLayout) {
        m_dirtyDuringLayout = true;
        return;
    }
    polish();
}

void Flex::relayout()
{
    doLayout();
}

void Flex::componentComplete()
{
    QQuickItem::componentComplete();
    for (QQuickItem *child : childItems()) {
        watch(child);
    }
    polish();
}

void Flex::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemChildAddedChange) {
        watch(value.item);
        invalidateLayout();
    } else if (change == ItemChildRemovedChange) {
        unwatch(value.item);
        invalidateLayout();
    } else if (change == ItemVisibleHasChanged && value.boolValue) {
        invalidateLayout();
    }
}

void Flex::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateLayout();
    }
}

void Flex::updatePolish()
{
    doLayout();
}

void Flex::watch(QQuickItem *child)
{
    if (child == nullptr) {
        return;
    }
    connect(child, &QQuickItem::implicitWidthChanged, this, &Flex::invalidateLayout,
            Qt::UniqueConnection);
    connect(child, &QQuickItem::implicitHeightChanged, this, &Flex::invalidateLayout,
            Qt::UniqueConnection);
    connect(child, &QQuickItem::visibleChanged, this, &Flex::invalidateLayout,
            Qt::UniqueConnection);
}

void Flex::unwatch(QQuickItem *child)
{
    if (child != nullptr) {
        disconnect(child, nullptr, this, nullptr);
    }
}

void Flex::doLayout()
{
    if (!isComponentComplete() || m_inLayout) {
        return;
    }
    m_inLayout = true;
    const bool horizontal = m_direction == Row || m_direction == RowReverse;
    const bool reversed = m_direction == RowReverse || m_direction == ColumnReverse;
    const qreal padL = resolvedPadding(m_paddingLeft);
    const qreal padT = resolvedPadding(m_paddingTop);
    const qreal padR = resolvedPadding(m_paddingRight);
    const qreal padB = resolvedPadding(m_paddingBottom);
    const qreal colGap = m_columnGap >= 0 ? m_columnGap : m_gap;
    const qreal rowGap = m_rowGap >= 0 ? m_rowGap : m_gap;
    const qreal mainGap = horizontal ? colGap : rowGap;
    const qreal crossGap = horizontal ? rowGap : colGap;
    const qreal innerW = width() - padL - padR;
    const qreal innerH = height() - padT - padB;
    const qreal mainAvail = horizontal ? innerW : innerH;
    const qreal crossAvail = horizontal ? innerH : innerW;
    const bool definiteMain = (horizontal ? width() : height()) > 0 && mainAvail > 0;
    const bool definiteCross = (horizontal ? height() : width()) > 0 && crossAvail > 0;

    std::vector<FlexEntry> entries;
    for (QQuickItem *child : childItems()) {
        auto *attached = qobject_cast<FlexAttached *>(qmlAttachedPropertiesObject<Flex>(child, false));
        if (!takesSlot(child, attached)) {
            continue;
        }
        FlexEntry e;
        e.item = child;
        e.attached = attached;
        const qreal minW = attached ? attached->minWidth() : 0;
        const qreal maxW = attached ? attached->maxWidth() : LayoutAttachedBase::unbounded;
        const qreal minH = attached ? attached->minHeight() : 0;
        const qreal maxH = attached ? attached->maxHeight() : LayoutAttachedBase::unbounded;
        e.minMain = horizontal ? minW : minH;
        e.maxMain = horizontal ? maxW : maxH;
        e.minCross = horizontal ? minH : minW;
        e.maxCross = horizontal ? maxH : maxW;
        e.grow = attached ? attached->grow() : 0;
        e.shrink = attached ? attached->shrink() : 1;
        e.alignSelf = attached ? attached->alignSelf() : Auto;
        const qreal basis = attached ? attached->basis() : -1;
        const qreal implicitMain = horizontal ? child->implicitWidth() : child->implicitHeight();
        e.hypo = clampTo(basis >= 0 ? basis : implicitMain, e.minMain, e.maxMain);
        e.crossHypo = clampTo(horizontal ? child->implicitHeight() : child->implicitWidth(),
                              e.minCross, e.maxCross);
        entries.push_back(e);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const FlexEntry &a, const FlexEntry &b) {
        const int oa = a.attached ? a.attached->order() : 0;
        const int ob = b.attached ? b.attached->order() : 0;
        return oa < ob;
    });

    // Line breaking.
    std::vector<std::vector<FlexEntry *>> lines;
    if (m_wrap == NoWrap || !definiteMain) {
        lines.emplace_back();
        for (FlexEntry &e : entries) {
            lines.back().push_back(&e);
        }
    } else {
        qreal used = 0;
        for (FlexEntry &e : entries) {
            const qreal needed = (lines.empty() || lines.back().empty()) ? e.hypo : used + mainGap + e.hypo;
            if (lines.empty() || (needed > mainAvail + 0.01 && !lines.back().empty())) {
                lines.emplace_back();
                used = e.hypo;
            } else {
                used = needed;
            }
            lines.back().push_back(&e);
        }
    }
    if (lines.empty()) {
        lines.emplace_back();
    }

    // Main sizes per line, natural cross size per line.
    std::vector<qreal> lineCross(lines.size(), 0);
    qreal maxContentMain = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        resolveLine(lines[i], definiteMain ? mainAvail : 0, mainGap);
        qreal lineMain = 0;
        for (FlexEntry *e : lines[i]) {
            lineMain += e->hypo;
            lineCross[i] = std::max(lineCross[i], e->crossHypo);
        }
        lineMain += mainGap * static_cast<qreal>(std::max<size_t>(lines[i].size(), 1) - 1);
        maxContentMain = std::max(maxContentMain, lineMain);
    }
    qreal naturalCross = 0;
    for (qreal c : lineCross) {
        naturalCross += c;
    }
    naturalCross += crossGap * static_cast<qreal>(lines.size() - 1);

    // Cross-axis: a single line fills the definite cross size; multiple
    // lines share leftover cross space per alignContent.
    if (lines.size() == 1 && definiteCross) {
        lineCross[0] = crossAvail;
    }
    qreal crossOffset = 0;
    qreal crossBetween = 0;
    if (lines.size() > 1 && definiteCross) {
        const qreal free = crossAvail - naturalCross;
        if (m_alignContent == Stretch && free > 0) {
            for (qreal &c : lineCross) {
                c += free / static_cast<qreal>(lines.size());
            }
        } else {
            distribute(m_alignContent, free, static_cast<int>(lines.size()), crossOffset, crossBetween);
        }
    }

    // Place.
    qreal crossCursor = crossOffset;
    qreal extentMain = 0;
    qreal extentCross = 0;
    const size_t lineCount = lines.size();
    for (size_t li = 0; li < lineCount; ++li) {
        const size_t index = m_wrap == WrapReverse ? lineCount - 1 - li : li;
        std::vector<FlexEntry *> &line = lines[index];
        const qreal thisCross = lineCross[index];
        qreal used = 0;
        for (FlexEntry *e : line) {
            used += e->target;
        }
        used += mainGap * static_cast<qreal>(std::max<size_t>(line.size(), 1) - 1);
        qreal offset = 0;
        qreal between = 0;
        if (definiteMain) {
            distribute(m_justify, mainAvail - used, static_cast<int>(line.size()), offset, between);
        }
        qreal cursor = offset;
        for (FlexEntry *e : line) {
            const Alignment self = e->alignSelf == Auto ? m_align : static_cast<Alignment>(e->alignSelf);
            qreal crossSize = e->crossHypo;
            qreal crossPos = 0;
            switch (self) {
            case Stretch:
                crossSize = clampTo(thisCross, e->minCross, e->maxCross);
                break;
            case Center:
                crossPos = (thisCross - crossSize) / 2;
                break;
            case End:
                crossPos = thisCross - crossSize;
                break;
            default:
                break;
            }
            qreal mainPos = cursor;
            if (reversed) {
                const qreal span = definiteMain ? mainAvail : used;
                mainPos = span - cursor - e->target;
            }
            const qreal x = padL + (horizontal ? mainPos : crossCursor + crossPos);
            const qreal y = padT + (horizontal ? crossCursor + crossPos : mainPos);
            const qreal w = horizontal ? e->target : crossSize;
            const qreal h = horizontal ? crossSize : e->target;
            e->item->setPosition(QPointF(x, y));
            e->item->setSize(QSizeF(w, h));
            extentMain = std::max(extentMain, mainPos + e->target);
            extentCross = std::max(extentCross, crossCursor + crossPos + crossSize);
            cursor += e->target + mainGap + between;
        }
        crossCursor += thisCross + crossGap + crossBetween;
    }

    const qreal implicitMain = maxContentMain + (horizontal ? padL + padR : padT + padB);
    const qreal implicitCross = naturalCross + (horizontal ? padT + padB : padL + padR);
    setImplicitSize(horizontal ? implicitMain : implicitCross, horizontal ? implicitCross : implicitMain);
    const qreal contentW = (horizontal ? std::max(extentMain, maxContentMain) : std::max(extentCross, naturalCross)) + padL + padR;
    const qreal contentH = (horizontal ? std::max(extentCross, naturalCross) : std::max(extentMain, maxContentMain)) + padT + padB;
    const int newLineCount = static_cast<int>(lines.size());
    if (contentW != m_contentWidth || contentH != m_contentHeight || newLineCount != m_lineCount) {
        m_contentWidth = contentW;
        m_contentHeight = contentH;
        m_lineCount = newLineCount;
        emit contentSizeChanged();
    }
    m_inLayout = false;
    if (m_dirtyDuringLayout) {
        m_dirtyDuringLayout = false;
        polish();
    }
}

} // namespace QindaTK
