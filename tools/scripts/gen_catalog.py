#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Generate a source declaration inventory for QindaTK's QML modules.

This is source parsing, not compiled Qt meta-object/runtime reflection. The
registration fields describe macros or module source declarations observed in
the repository; they do not prove that a built plugin exports the same API.
"""
import argparse
import datetime
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QML_DIR = ROOT / "src" / "qml"
CORE_DIR = ROOT / "src" / "core"
BRIDGE_DIR = ROOT / "src" / "bridge"
OUT = ROOT / "docs" / "catalog.json"

QML_PROPERTY = re.compile(
    r"^\s*(?P<mods>(?:readonly\s+|default\s+|required\s+)*)property\s+"
    r"(?P<type>[\w.<>]+)\s+(?P<name>\w+)\s*(?::\s*(?P<default>.*?))?\s*$")
QML_SIGNAL = re.compile(r"^\s*signal\s+(?P<name>\w+)\s*(?:\((?P<params>[^)]*)\))?\s*$")
QML_FUNCTION = re.compile(r"^\s*function\s+(?P<name>\w+)\s*\((?P<params>[^)]*)\)")
QML_ENUM = re.compile(r"^\s*enum\s+(?P<name>\w+)\s*\{(?P<body>[^}]*)\}")
QML_ROOT = re.compile(r"^(?P<type>[A-Z][\w.]*)\s*\{\s*$")
QML_OBJECTNAME = re.compile(r'objectName:\s*"(?P<name>[^"]+)"')
CPP_CLASS = re.compile(r"^(?:class|struct)\s+(?P<name>\w+)(?:\s+final)?(?:\s*:\s*public\s+(?P<base>[\w:]+))?")
CPP_PROPERTY = re.compile(
    r"Q_PROPERTY\(\s*(?P<type>[\w:<>]+\s*\*?)\s*(?P<name>\w+)\s+"
    r"(?P<accessor>READ\s+\w+|MEMBER\s+\w+)"
    r"(?P<tail>[^)]*)\)")
CPP_INVOKABLE = re.compile(
    r"Q_INVOKABLE\s+(?:(?:\[\[nodiscard\]\]|static)\s+)*"
    r"(?P<ret>[\w:<>]+(?:\s*[*&])?)\s*(?P<name>\w+)\s*\((?P<params>[^)]*)\)")
CPP_ENUM = re.compile(r"^\s*enum\s+(?:class\s+)?(?P<name>\w+)\s*\{(?P<body>[^}]*)\}")
CPP_SIGNAL = re.compile(r"^\s*void\s+(?P<name>\w+)\s*\((?P<params>[^)]*)\)\s*;")
CPP_QML_MACRO = re.compile(
    r"^\s*(?P<macro>QML_ELEMENT|QML_NAMED_ELEMENT\((?P<named>\w+)\)|"
    r"QML_ANONYMOUS|QML_SINGLETON|QML_VALUE_TYPE\((?P<value>\w+)\)|"
    r"QML_ATTACHED\((?P<attached>\w+)\))")


def _entry(name, kind, path):
    return {"name": name, "kind": kind, "base": "", "description": "",
            "singleton": False, "attached": None, "file": str(path.relative_to(ROOT)),
            "properties": [], "signals": [], "methods": [], "enums": {}}


def leading_comment(lines):
    out = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("//"):
            text = stripped[2:].strip()
            if not text.startswith("SPDX-License-Identifier"):
                out.append(text)
        elif stripped.startswith(("import", "pragma")) or not stripped:
            continue
        else:
            break
    return " ".join(out).strip()


def parse_qml(path, module="QindaTK", registration="qml_module"):
    lines = path.read_text().splitlines()
    entry = _entry(path.stem, "qml", path)
    entry.update({"module": module, "registrationKind": registration,
                  "exported": True, "anonymous": False, "internal": False,
                  "valueType": False, "bridge": registration == "bridge_qml",
                  "description": leading_comment(lines), "objectNames": []})
    entry["singleton"] = any(line.strip() == "pragma Singleton" for line in lines)
    depth, root_seen = 0, False
    for line in lines:
        if not root_seen:
            root = QML_ROOT.match(line)
            if root:
                entry["base"] = root.group("type")
                root_seen, depth = True, 1
            continue
        if depth == 1:
            prop = QML_PROPERTY.match(line)
            if prop:
                mods = prop.group("mods") or ""
                entry["properties"].append({"name": prop.group("name"),
                    "type": prop.group("type"), "default": (prop.group("default") or "").strip(),
                    "readonly": "readonly" in mods, "defaultProperty": "default" in mods,
                    "required": "required" in mods})
            signal = QML_SIGNAL.match(line)
            if signal:
                entry["signals"].append({"name": signal.group("name"),
                    "parameters": (signal.group("params") or "").strip()})
            method = QML_FUNCTION.match(line)
            if method and not method.group("name").startswith("_"):
                entry["methods"].append({"name": method.group("name"),
                    "parameters": method.group("params").strip()})
            enum = QML_ENUM.match(line)
            if enum:
                entry["enums"][enum.group("name")] = [v.strip() for v in enum.group("body").split(",") if v.strip()]
        entry["objectNames"].extend(m.group("name") for m in QML_OBJECTNAME.finditer(line))
        depth += line.count("{") - line.count("}")
    entry["objectNames"] = sorted(set(entry["objectNames"]))
    entry["properties"] = [p for p in entry["properties"] if not p["name"].startswith("_")]
    return entry


def parse_header(path):
    text = path.read_text()
    types, current, in_signals = [], None, False
    source_lines = text.splitlines()
    index = 0
    while index < len(source_lines):
        line = source_lines[index]
        cls = CPP_CLASS.match(line)
        if cls:
            current = _entry(cls.group("name"), "cpp", path)
            current.update({"qmlName": cls.group("name"), "base": cls.group("base") or "",
                            "module": "QindaTK",
                            "anonymous": False, "registered": False, "registrationKind": "internal",
                            "exported": False, "internal": True, "valueType": False,
                            "bridge": False, "singleton": False, "attached": None})
            types.append(current)
            in_signals = False
            index += 1
            continue
        if current is None:
            index += 1
            continue
        macro = CPP_QML_MACRO.match(line)
        if macro:
            name = macro.group("macro")
            if name == "QML_ELEMENT":
                current.update(registered=True, registrationKind="qml_element", exported=True, internal=False)
            elif name.startswith("QML_NAMED_ELEMENT"):
                current.update(registered=True, qmlName=macro.group("named"),
                               registrationKind="qml_element", exported=True, internal=False)
            elif name == "QML_ANONYMOUS":
                current.update(registered=True, anonymous=True, registrationKind="qml_anonymous")
            elif name.startswith("QML_VALUE_TYPE"):
                current.update(registered=True, qmlName=macro.group("value"), valueType=True,
                               registrationKind="qml_value_type", exported=True, internal=False)
            elif name == "QML_SINGLETON":
                current["singleton"] = True
            elif name.startswith("QML_ATTACHED"):
                current["attached"] = macro.group("attached")
            index += 1
            continue
        if re.match(r"^\s*signals:", line):
            in_signals = True
        elif re.match(r"^\s*(public|private|protected)( slots)?:", line):
            in_signals = False
        statement = line.strip()
        is_property = "Q_PROPERTY(" in statement
        if (is_property or "Q_INVOKABLE" in statement) and ")" not in statement:
            while index + 1 < len(source_lines) and ")" not in statement:
                index += 1
                statement += " " + source_lines[index].strip()
        statements = [statement] if ("Q_PROPERTY(" in statement and ")" in statement) or (
            "Q_INVOKABLE" in statement and ";" in statement) else []
        for statement in statements:
            prop = CPP_PROPERTY.search(statement)
            if prop:
                tail = prop.group("tail")
                member = prop.group("accessor").startswith("MEMBER")
                constant = re.search(r"\bCONSTANT\b", tail) is not None
                current["properties"].append({"name": prop.group("name"),
                    "type": " ".join(prop.group("type").split()), "default": "",
                    "readonly": constant if member else re.search(r"\bWRITE\b", tail) is None,
                    "constant": constant})
                continue
            inv = CPP_INVOKABLE.search(statement)
            if inv:
                current["methods"].append({"name": inv.group("name"),
                    "returns": " ".join(inv.group("ret").split()),
                    "parameters": " ".join(inv.group("params").split())})
                continue
            enum = CPP_ENUM.match(statement)
            if enum:
                current["enums"][enum.group("name")] = [v.strip() for v in enum.group("body").split(",") if v.strip()]
            if in_signals:
                sig = CPP_SIGNAL.search(statement)
                if sig:
                    current["signals"].append({"name": sig.group("name"),
                        "parameters": " ".join(sig.group("params").split())})
        index += 1
    for entry in types:
        match = re.search(r"((?:^//.*\n)+)^class\s+" + re.escape(entry["name"]) + r"\b", text, re.M)
        if match:
            entry["description"] = " ".join(l[2:].strip() for l in match.group(1).splitlines()
                                             if l.startswith("//") and "SPDX-License-Identifier" not in l)
    return [entry for entry in types if entry["registered"]]


def build():
    types = [parse_qml(path) for path in sorted(QML_DIR.glob("*.qml"))]
    types.extend(parse_qml(path, "QindaTK.QindaQt", "bridge_qml")
                 for path in sorted(BRIDGE_DIR.glob("*.qml")))
    for header in sorted(CORE_DIR.glob("*.h")):
        if not header.name.endswith("_p.h"):
            types.extend(parse_header(header))
    for entry in types:
        if entry["kind"] == "cpp":
            entry["name"] = entry.pop("qmlName")
    types.sort(key=lambda entry: (entry["module"] != "QindaTK", entry["kind"] != "qml", entry["name"]))
    return {"_license": "SPDX-License-Identifier: LGPL-3.0-or-later",
            "generated": datetime.date.today().isoformat(), "module": "QindaTK",
            "version": "1.0", "types": types}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print", action="store_true", help="print JSON instead of writing")
    parser.add_argument("--output", type=Path, default=OUT, help="output path (default: docs/catalog.json)")
    args = parser.parse_args()
    text = json.dumps(build(), indent=2, ensure_ascii=False) + "\n"
    if args.print:
        print(text, end="")
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    label = args.output.relative_to(ROOT) if args.output.is_relative_to(ROOT) else args.output
    print("wrote %s (%d types)" % (label, len(json.loads(text)["types"])))


if __name__ == "__main__":
    main()
