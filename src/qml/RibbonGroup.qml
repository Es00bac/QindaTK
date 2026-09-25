// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// One labelled group of a Tk.Ribbon, drawn as a soft card (D-297). Two
// arrangements, both declared as data by the host:
//   `items` — columns: a "large" item is a column of its own; consecutive
//             "small"/"icon"/component items stack `stackRows` high;
//   `rows`  — explicit rows ([[…], […]]), for groups that read left to right
//             (a font group: family and size on top, B I U below).
// `level` is the ribbon's squeeze level: 0 as declared, 1 turns large items
// small, 2 turns every item with an icon into an icon-only button. In
// `compact` mode every group is one row and the caption is hidden.
//
// AGENT-CONTRACT (Ribbon.qml): `widthAt(level)` is this group's width at a
// level, computed from the items' weight-independent metrics
// (RibbonButton.largeWidth/smallWidth/iconWidth, a component's implicit
// width). The ribbon sums it to pick its level, so it must never read the
// current placement — that is what keeps the squeeze free of binding loops.
// AGENT-NOTE: items are placed by computed x/y rather than nested Flex
// columns: one arrange() both measures and places, so the planned width and
// the drawn width cannot drift apart, and a level change moves items
// without re-creating them (a combo keeps its popup and focus).
Item {
    id: group

    required property var groupData
    required property Tk.Ribbon ribbon
    property int level: 0
    property bool compact: false
    readonly property string groupId: String(groupData.id || "")
    readonly property var entries: flatten(groupData)
    readonly property var arrangement: arrange(level)
    readonly property real captionHeight: compact ? 0 : caption.implicitHeight + Tk.Theme.space.xs
    readonly property real padding: Tk.Theme.space.sm

    objectName: "ribbonGroup_" + groupId
    implicitWidth: arrangement.width
    implicitHeight: arrangement.height + captionHeight + padding * 2
    Tk.Flex.shrink: 0
    Accessible.role: Accessible.Grouping
    Accessible.name: String(groupData.label || groupId)

    // Items in reading order; each carries its row index for `rows` groups.
    function flatten(data) {
        const out = []
        if (data && data.rows && data.rows.length) {
            for (let r = 0; r < data.rows.length; ++r)
                for (let i = 0; i < data.rows[r].length; ++i)
                    if (data.rows[r][i] && data.rows[r][i].visible !== false) out.push({ item: data.rows[r][i], row: r })
        } else if (data && data.items) {
            for (let i = 0; i < data.items.length; ++i)
                if (data.items[i] && data.items[i].visible !== false) out.push({ item: data.items[i], row: -1 })
        }
        return out
    }
    function hasIcon(index) {
        const slot = slots.itemAt(index) as Tk.RibbonSlot
        return slot !== null && slot.iconName.length > 0
    }
    // The weight an entry is drawn at, for a squeeze level.
    function weightAt(index, lvl) {
        const entry = entries[index]
        if (entry.item.component) return "component"
        const declared = String(entry.item.size || "small")
        if (compact) return hasIcon(index) ? (declared === "large" && lvl === 0 ? "small" : "icon") : "small"
        if (lvl >= 2) return hasIcon(index) ? "icon" : "small"
        if (lvl >= 1 && declared === "large") return "small"
        return declared
    }
    function widthOf(index, weight) {
        const slot = slots.itemAt(index) as Tk.RibbonSlot
        return slot === null ? 0 : slot.widthFor(weight)
    }
    function widthAt(lvl) { return arrange(lvl).width }
    function arrange(lvl) {
        // Read so the arrangement re-evaluates once the slots exist.
        const created = slots.count
        const gap = Tk.Theme.space.xs
        const rowHeight = Tk.Theme.size.control
        const stack = compact ? 1 : Math.max(1, ribbon.stackRows)
        const band = stack * rowHeight + (stack - 1) * gap
        const placements = []
        let width = 0
        if (groupData.rows && groupData.rows.length && !compact) {
            const rowX = []
            for (let i = 0; i < entries.length; ++i) {
                const r = entries[i].row
                const weight = weightAt(i, lvl)
                const w = widthOf(i, weight)
                const x = rowX[r] === undefined ? 0 : rowX[r] + gap
                placements.push({ x: x, y: r * (rowHeight + gap), weight: weight, width: w, height: rowHeight })
                rowX[r] = x + w
            }
            for (let r = 0; r < rowX.length; ++r) width = Math.max(width, rowX[r] || 0)
        } else {
            let x = 0, columnWidth = 0, filled = 0, started = false
            for (let i = 0; i < entries.length; ++i) {
                const weight = weightAt(i, lvl)
                const w = widthOf(i, weight)
                if (weight === "large") {
                    if (filled > 0) { x += columnWidth + gap; filled = 0; columnWidth = 0 }
                    else if (started) x += gap
                    placements.push({ x: x, y: 0, weight: weight, width: w, height: band })
                    x += w
                    started = true
                    continue
                }
                if (filled === 0 && started) x += gap
                if (filled === stack) { x += columnWidth + gap; filled = 0; columnWidth = 0 }
                placements.push({ x: x, y: filled * (rowHeight + gap), weight: weight, width: w, height: rowHeight })
                columnWidth = Math.max(columnWidth, w)
                filled += 1
                started = true
            }
            width = x + (filled > 0 ? columnWidth : 0)
        }
        const captionWidth = compact ? 0 : caption.implicitWidth
        return { width: Math.ceil(Math.max(width, captionWidth) + group.padding * 2),
                 height: entries.length ? band : 0, placements: placements }
    }

    Rectangle {
        objectName: "ribbonGroupCard"
        anchors.fill: parent
        radius: Tk.Theme.radius.md
        color: Tk.Theme.color.panelAlt
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.alpha(Tk.Theme.color.divider, Tk.Theme.opacity.muted)
    }

    Item {
        id: body
        x: group.padding
        y: group.padding
        width: group.width - group.padding * 2
        height: group.arrangement.height
        Repeater {
            id: slots
            model: group.entries.length
            delegate: Tk.RibbonSlot {
                required property int index
                readonly property var place: group.arrangement.placements[index] || ({ x: 0, y: 0, weight: "small", width: 0, height: 0 })
                ribbon: group.ribbon
                groupId: group.groupId
                itemData: group.entries[index].item
                weight: place.weight
                x: place.x
                y: place.y + (place.height - height) / 2
                width: place.width
            }
        }
    }

    Tk.Overline {
        id: caption
        objectName: "ribbonGroupCaption"
        visible: !group.compact
        title: String(group.groupData.label || "")
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: group.padding - Tk.Theme.space.xs
        font.pixelSize: Tk.Theme.font.micro
    }
}
