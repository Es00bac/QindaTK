#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Generate src/core/icon_data.cpp from a Lucide icon package.

Usage: gen_icons.py <path-to-lucide-react/dist/esm/icons> [--all]

The package need not come from npm: `curl -O` the tarball named by
https://registry.npmjs.org/lucide-react (dist-tags.latest → dist.tarball),
untar it, and point this script at package/dist/esm/icons. lucide-react
>= 1.x ships `<name>.mjs` modules; older releases shipped `<name>.js`;
both are read.

Every element of an icon (path/circle/rect/line/polyline/polygon/ellipse)
is converted to an SVG path string so the C++ renderer only needs a path
parser. Icons are stroked, 24x24 viewBox, stroke width 2 (Lucide's own
convention); QindaTK's Icon item scales them to the requested size.

AGENT-NOTE: the curated list below is the toolkit's built-in set. Add a
name here and re-run the script; do not hand-edit icon_data.cpp. Lucide is
ISC licensed (see THIRD_PARTY_NOTICES.md).
"""
import re
import sys
from pathlib import Path

CURATED = """
a-large-small activity alert-circle alert-triangle align-center align-justify align-left align-right
anchor aperture archive arrow-down arrow-down-to-line arrow-left arrow-left-right arrow-right arrow-up
arrow-up-down arrow-up-to-line asterisk at-sign audio-lines baseline bell blend bold bookmark box
braces brackets brush bug calendar camera check check-check chevron-down chevron-left chevron-right
chevron-up chevrons-down chevrons-left chevrons-right chevrons-up chevrons-up-down chevrons-down-up
circle circle-check circle-dot circle-help circle-x clipboard clock cloud code columns-2 columns-3 command
component contrast copy corner-down-left corner-down-right cpu crop crosshair database diamond
dot download droplet ellipsis ellipsis-vertical equal eraser expand external-link eye eye-off
file file-image file-plus file-text film filter flag flip-horizontal flip-vertical focus folder
folder-open folder-plus frame fullscreen gamepad-2 gauge git-branch grid-2x2 grid-3x3 grip-horizontal grip-vertical
group hand hash heading heart hexagon highlighter history house image images import indent-increase indent-decrease
info italic keyboard languages layers layers-2 layout-dashboard layout-grid layout-panel-left layout-template
link link-2 list list-ordered list-tree loader loader-circle lock lock-open magnet map-pin maximize maximize-2 menu mic minimize
minimize-2 minus monitor moon mouse-pointer mouse-pointer-2 move move-horizontal move-vertical music navigation
package paint-bucket paintbrush palette panel-bottom panel-bottom-close panel-left panel-left-close panel-right
panel-right-close panel-top panel-top-close panels-top-left pause pen pen-tool pencil percent pilcrow pin pin-off
pipette play plus printer puzzle quote redo redo-2 refresh-cw repeat rotate-ccw rotate-cw route rows-2 rows-3 ruler
save scaling scan scissors search send separator-horizontal separator-vertical settings settings-2 share-2 shrink shuffle
sidebar sigma skip-back skip-forward sliders-horizontal sliders-vertical smartphone sparkles spline square
square-check square-dashed square-pen stamp star step-back step-forward strikethrough subscript sun superscript
table tag target terminal text text-cursor text-cursor-input timer toggle-left toggle-right trash trash-2 triangle
type underline undo undo-2 ungroup unlink unlock upload user users video volume-2 volume-x wand wand-sparkles waypoints
workflow wrap-text wrench x zap zoom-in zoom-out bring-to-front send-to-back square-stack layout-list
between-horizontal-start between-vertical-start align-horizontal-justify-center align-vertical-justify-center
align-start-horizontal align-start-vertical align-end-horizontal align-end-vertical align-center-horizontal align-center-vertical
align-horizontal-space-between align-vertical-space-between shapes lasso lasso-select paint-roller spray-can
library scroll-text wallet
clipboard-paste monitor-play sticky-note book-open sheet file-spreadsheet chart-column chart-bar
funnel square-check-big text-align-start text-align-center text-align-end text-align-justify
list-indent-increase list-indent-decrease text-indent-increase text-indent-decrease
"""

ELEMENT_RE = re.compile(r'\[\s*"(\w+)",\s*\{([^}]*)\}\s*\]', re.S)
REEXPORT_RE = re.compile(r"from\s+'\./([\w-]+)\.m?js'")
ATTR_RE = re.compile(r'(\w+):\s*"([^"]*)"')


def fmt(v):
    v = float(v)
    return ("%g" % v)


def element_to_path(kind, attrs):
    a = {k: v for k, v in ATTR_RE.findall(attrs)}
    if kind == "path":
        return a["d"]
    if kind == "circle":
        cx, cy, r = float(a["cx"]), float(a["cy"]), float(a["r"])
        return "M%s %s a%s %s 0 1 0 %s 0 a%s %s 0 1 0 %s 0" % (
            fmt(cx - r), fmt(cy), fmt(r), fmt(r), fmt(2 * r), fmt(r), fmt(r), fmt(-2 * r))
    if kind == "ellipse":
        cx, cy, rx, ry = (float(a[k]) for k in ("cx", "cy", "rx", "ry"))
        return "M%s %s a%s %s 0 1 0 %s 0 a%s %s 0 1 0 %s 0" % (
            fmt(cx - rx), fmt(cy), fmt(rx), fmt(ry), fmt(2 * rx), fmt(rx), fmt(ry), fmt(-2 * rx))
    if kind == "rect":
        x, y = float(a.get("x", 0)), float(a.get("y", 0))
        w, h = float(a["width"]), float(a["height"])
        rx = float(a.get("rx", a.get("ry", 0)))
        if rx <= 0:
            return "M%s %s h%s v%s h%s z" % (fmt(x), fmt(y), fmt(w), fmt(h), fmt(-w))
        return ("M%s %s h%s a%s %s 0 0 1 %s %s v%s a%s %s 0 0 1 %s %s h%s a%s %s 0 0 1 %s %s v%s a%s %s 0 0 1 %s %s z" % (
            fmt(x + rx), fmt(y), fmt(w - 2 * rx), fmt(rx), fmt(rx), fmt(rx), fmt(rx), fmt(h - 2 * rx),
            fmt(rx), fmt(rx), fmt(-rx), fmt(rx), fmt(-(w - 2 * rx)), fmt(rx), fmt(rx), fmt(-rx), fmt(-rx),
            fmt(-(h - 2 * rx)), fmt(rx), fmt(rx), fmt(rx), fmt(-rx)))
    if kind == "line":
        return "M%s %s L%s %s" % (fmt(a["x1"]), fmt(a["y1"]), fmt(a["x2"]), fmt(a["y2"]))
    if kind in ("polyline", "polygon"):
        pts = [p for p in re.split(r"[\s,]+", a["points"].strip()) if p]
        pairs = list(zip(pts[0::2], pts[1::2]))
        d = "M%s %s" % pairs[0] + "".join(" L%s %s" % p for p in pairs[1:])
        if kind == "polygon":
            d += " z"
        return d
    raise ValueError("unsupported element " + kind)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    src = Path(sys.argv[1])
    names = sorted(set(CURATED.split()))
    if "--all" in sys.argv:
        names = sorted(p.stem for p in list(src.glob("*.mjs")) + list(src.glob("*.js"))
                       if not p.stem.endswith(".map"))
    out = Path(__file__).resolve().parents[2] / "src" / "core" / "icon_data.cpp"
    rows = []
    aliases = []
    missing = []
    for name in names:
        f = src / (name + ".mjs")
        if not f.exists():
            f = src / (name + ".js")
        if not f.exists():
            missing.append(name)
            continue
        text = f.read_text()
        paths = [element_to_path(k, attrs) for k, attrs in ELEMENT_RE.findall(text)]
        if not paths:
            # A deprecated name re-exports the icon under its new name; keep
            # both so callers written against older Lucide releases still
            # resolve.
            target = REEXPORT_RE.search(text)
            if target is None:
                missing.append(name)
                continue
            new_name = target.group(1)
            if new_name not in names:
                tf = src / (new_name + ".mjs")
                if not tf.exists():
                    tf = src / (new_name + ".js")
                tpaths = [element_to_path(k, attrs) for k, attrs in ELEMENT_RE.findall(tf.read_text())]
                rows.append((new_name, tpaths))
                names.append(new_name)
            aliases.append((name, new_name))
            continue
        rows.append((name, paths))
    rows.sort()
    aliases.sort()
    with out.open("w") as fh:
        fh.write("// SPDX-License-Identifier: LGPL-3.0-or-later\n")
        fh.write("// GENERATED FILE - do not edit. Regenerate with tools/scripts/gen_icons.py.\n")
        fh.write("// Icon shapes: Lucide (https://lucide.dev), ISC License,\n")
        fh.write("// Copyright (c) Lucide Icons and Contributors. See THIRD_PARTY_NOTICES.md.\n")
        fh.write("#include \"icon_data.h\"\n\nnamespace QindaTK::IconData {\n\n")
        fh.write("const std::span<const BuiltinIcon> builtinIcons()\n{\n")
        fh.write("    static const BuiltinIcon table[] = {\n")
        for name, paths in rows:
            joined = "\", \"".join(p.replace('"', '\\"') for p in paths)
            fh.write('        {"%s", {"%s"}},\n' % (name, joined))
        fh.write("    };\n    return table;\n}\n\n")
        fh.write("const std::span<const IconAlias> iconAliases()\n{\n")
        fh.write("    static const IconAlias table[] = {\n")
        for old, new in aliases:
            fh.write('        {"%s", "%s"},\n' % (old, new))
        fh.write("    };\n    return table;\n}\n\n} // namespace QindaTK::IconData\n")
    print("wrote %d icons and %d aliases to %s" % (len(rows), len(aliases), out))
    if missing:
        print("missing:", " ".join(missing))


if __name__ == "__main__":
    main()
