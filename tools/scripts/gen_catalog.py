#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Generate docs/catalog.json, the machine-readable type catalog of QindaTK.

Usage (from the repository root):

    python3 tools/scripts/gen_catalog.py            # writes docs/catalog.json
    python3 tools/scripts/gen_catalog.py --print    # prints instead of writing

Sources:
  * src/qml/*.qml   — every file is one QML type named after the file. The
                      root type, the leading comment block, `property`
                      declarations (with type and default expression),
                      `signal` declarations and `function` names are read.
  * src/core/*.h    — every class carrying QML_ELEMENT / QML_NAMED_ELEMENT /
                      QML_ANONYMOUS / QML_SINGLETON. Q_PROPERTY lines,
                      Q_INVOKABLE methods, enums and signals are read.

Output shape:
  {"generated": "<iso date>", "module": "QindaTK", "version": "1.0",
   "types": [{"name", "kind": "qml"|"cpp", "base", "description",
              "singleton", "attached", "file",
              "properties": [{"name", "type", "default", "readonly"}],
              "signals": [{"name", "parameters"}],
              "methods": [...], "enums": {...}}]}

The parser is regular-expression based on purpose: it must stay correct for
the declaration style used in this repository (one declaration per line),
not for arbitrary QML/C++. Run it after adding or changing a type and
commit the result; docs/controls.md stays the human contract.
"""
import datetime
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QML_DIR = ROOT / "src" / "qml"
CORE_DIR = ROOT / "src" / "core"
OUT = ROOT / "docs" / "catalog.json"

QML_PROPERTY = re.compile(
    r"^\s*(?P<mods>(?:readonly\s+|default\s+|required\s+)*)property\s+"
    r"(?P<type>[\w.<>]+)\s+(?P<name>\w+)\s*(?::\s*(?P<default>.*?))?\s*$")
QML_SIGNAL = re.compile(r"^\s*signal\s+(?P<name>\w+)\s*(?:\((?P<params>[^)]*)\))?\s*$")
QML_FUNCTION = re.compile(r"^\s*function\s+(?P<name>\w+)\s*\((?P<params>[^)]*)\)")
QML_ENUM = re.compile(r"^\s*enum\s+(?P<name>\w+)\s*\{(?P<body>[^}]*)\}")
QML_ROOT = re.compile(r"^(?P<type>[A-Z][\w.]*)\s*\{\s*$")
QML_OBJECTNAME = re.compile(r'objectName:\s*"(?P<name>[^"]+)"')

CPP_CLASS = re.compile(r"^class\s+(?P<name>\w+)\s*(?:final\s*)?:\s*public\s+(?P<base>[\w:]+)")
CPP_PROPERTY = re.compile(
    r"Q_PROPERTY\((?P<type>[\w:<>*\s]+?)\s+(?P<name>\w+)\s+READ\s+\w+"
    r"(?P<write>\s+WRITE\s+\w+)?(?:\s+NOTIFY\s+\w+)?(?P<constant>\s+CONSTANT)?\)")
CPP_INVOKABLE = re.compile(r"Q_INVOKABLE\s+(?:\[\[nodiscard\]\]\s+)?(?:static\s+)?(?P<ret>[\w:<>*&\s]+?)\s+(?P<name>\w+)\s*\((?P<params>[^)]*)\)")
CPP_ENUM = re.compile(r"^\s*enum\s+(?:class\s+)?(?P<name>\w+)\s*\{(?P<body>[^}]*)\}")
CPP_SIGNAL = re.compile(r"^\s*void\s+(?P<name>\w+)\s*\((?P<params>[^)]*)\)\s*;")
CPP_QML_MACRO = re.compile(r"^\s*(QML_ELEMENT|QML_NAMED_ELEMENT\((\w+)\)|QML_ANONYMOUS|QML_SINGLETON|QML_ATTACHED\((\w+)\))")


def leading_comment(lines):
    """The comment block that precedes the root object, minus the SPDX line."""
    out = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("//"):
            text = stripped[2:].strip()
            if text.startswith("SPDX-License-Identifier"):
                continue
            out.append(text)
        elif stripped.startswith("import") or stripped.startswith("pragma") or not stripped:
            continue
        else:
            break
    return " ".join(out).strip()


def parse_qml(path):
    lines = path.read_text().splitlines()
    entry = {
        "name": path.stem,
        "kind": "qml",
        "base": "",
        "description": leading_comment(lines),
        "singleton": any(l.strip() == "pragma Singleton" for l in lines),
        "attached": None,
        "file": str(path.relative_to(ROOT)),
        "properties": [],
        "signals": [],
        "methods": [],
        "enums": {},
        "objectNames": [],
    }
    depth = 0
    root_seen = False
    for line in lines:
        if not root_seen:
            match = QML_ROOT.match(line)
            if match:
                entry["base"] = match.group("type")
                root_seen = True
                depth = 1
                continue
            continue
        # Only declarations at depth 1 (the root object) are the public API.
        if depth == 1:
            match = QML_PROPERTY.match(line)
            if match:
                mods = match.group("mods") or ""
                entry["properties"].append({
                    "name": match.group("name"),
                    "type": match.group("type"),
                    "default": (match.group("default") or "").strip(),
                    "readonly": "readonly" in mods,
                    "defaultProperty": "default" in mods,
                    "required": "required" in mods,
                })
            match = QML_SIGNAL.match(line)
            if match:
                entry["signals"].append({"name": match.group("name"),
                                         "parameters": (match.group("params") or "").strip()})
            match = QML_FUNCTION.match(line)
            if match and not match.group("name").startswith("_"):
                entry["methods"].append({"name": match.group("name"),
                                         "parameters": match.group("params").strip()})
            match = QML_ENUM.match(line)
            if match:
                entry["enums"][match.group("name")] = [v.strip() for v in match.group("body").split(",") if v.strip()]
        for match in QML_OBJECTNAME.finditer(line):
            entry["objectNames"].append(match.group("name"))
        depth += line.count("{") - line.count("}")
    entry["objectNames"] = sorted(set(entry["objectNames"]))
    # Private helpers (leading underscore) are not API.
    entry["properties"] = [p for p in entry["properties"] if not p["name"].startswith("_")]
    return entry


def parse_header(path):
    lines = path.read_text().splitlines()
    types = []
    current = None
    in_signals = False
    for line in lines:
        cls = CPP_CLASS.match(line)
        if cls:
            current = {
                "name": cls.group("name"),
                "qmlName": cls.group("name"),
                "kind": "cpp",
                "base": cls.group("base"),
                "description": "",
                "singleton": False,
                "attached": None,
                "anonymous": False,
                "registered": False,
                "file": str(path.relative_to(ROOT)),
                "properties": [],
                "signals": [],
                "methods": [],
                "enums": {},
            }
            types.append(current)
            in_signals = False
            continue
        if current is None:
            continue
        macro = CPP_QML_MACRO.match(line)
        if macro:
            text = macro.group(1)
            if text == "QML_ELEMENT":
                current["registered"] = True
            elif text.startswith("QML_NAMED_ELEMENT"):
                current["registered"] = True
                current["qmlName"] = macro.group(2)
            elif text == "QML_ANONYMOUS":
                current["anonymous"] = True
                current["registered"] = True
            elif text == "QML_SINGLETON":
                current["singleton"] = True
            elif text.startswith("QML_ATTACHED"):
                current["attached"] = macro.group(3)
            continue
        if re.match(r"^\s*signals:", line):
            in_signals = True
            continue
        if re.match(r"^\s*(public|private|protected)( slots)?:", line):
            in_signals = False
        prop = CPP_PROPERTY.search(line)
        if prop:
            current["properties"].append({
                "name": prop.group("name"),
                "type": prop.group("type").strip(),
                "default": "",
                "readonly": prop.group("write") is None,
                "constant": prop.group("constant") is not None,
            })
            continue
        inv = CPP_INVOKABLE.search(line)
        if inv:
            current["methods"].append({"name": inv.group("name"),
                                       "returns": inv.group("ret").strip(),
                                       "parameters": " ".join(inv.group("params").split())})
            continue
        enum = CPP_ENUM.match(line)
        if enum:
            current["enums"][enum.group("name")] = [v.strip() for v in enum.group("body").split(",") if v.strip()]
            continue
        if in_signals:
            sig = CPP_SIGNAL.match(line)
            if sig:
                current["signals"].append({"name": sig.group("name"),
                                           "parameters": " ".join(sig.group("params").split())})
    # The class comment: the block immediately above `class`.
    text = path.read_text()
    for entry in types:
        match = re.search(r"((?:^//.*\n)+)^class\s+" + entry["name"] + r"\b", text, re.M)
        if match:
            comment = " ".join(l[2:].strip() for l in match.group(1).splitlines() if l.startswith("//"))
            entry["description"] = comment.strip()
    return [t for t in types if t["registered"]]


def build():
    types = [parse_qml(p) for p in sorted(QML_DIR.glob("*.qml"))]
    for header in sorted(CORE_DIR.glob("*.h")):
        if header.name.endswith("_p.h"):
            continue
        types.extend(parse_header(header))
    for entry in types:
        if entry["kind"] == "cpp":
            entry["name"] = entry.pop("qmlName")
    types.sort(key=lambda t: (t["kind"] != "qml", t["name"]))
    return {
        "generated": datetime.date.today().isoformat(),
        "module": "QindaTK",
        "version": "1.0",
        "types": types,
    }


def main():
    catalog = build()
    text = json.dumps(catalog, indent=2, ensure_ascii=False) + "\n"
    if "--print" in sys.argv:
        sys.stdout.write(text)
        return
    OUT.write_text(text)
    print("wrote %s (%d types)" % (OUT.relative_to(ROOT), len(catalog["types"])))


if __name__ == "__main__":
    main()
