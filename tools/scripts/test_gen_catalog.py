#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Focused source fixtures and preservation checks for gen_catalog.py."""
import importlib.util
import json
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).with_name("gen_catalog.py")
SPEC = importlib.util.spec_from_file_location("gen_catalog", SCRIPT)
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def fixture_checks():
    with tempfile.TemporaryDirectory(dir=GEN.ROOT) as temp:
        header = Path(temp) / "fixture.h"
        header.write_text("""// SPDX-License-Identifier: LGPL-3.0-or-later
class FixtureValue {
    Q_GADGET
    QML_VALUE_TYPE(fixtureValue)
    Q_PROPERTY(QindaTK::ThemeRamp *ramp READ ramp)
    Q_PROPERTY(qreal pressure MEMBER pressure)
    Q_INVOKABLE QVariantMap registerPanel(
        const QString &workspace,
        const QString &panelId);
};
class FixtureHelper {
    Q_OBJECT
    QML_ANONYMOUS
};
class FixturePrivate {
    Q_OBJECT
};
""")
        entries = GEN.parse_header(header)
        by_name = {entry["qmlName"]: entry for entry in entries}
        require(by_name["fixtureValue"]["valueType"], "QML_VALUE_TYPE was not classified")
        require(by_name["fixtureValue"]["exported"], "value type should be marked exported")
        prop = by_name["fixtureValue"]["properties"][0]
        require(prop["name"] == "ramp" and prop["type"] == "QindaTK::ThemeRamp *",
                "pointer property spacing was not retained")
        member_prop = by_name["fixtureValue"]["properties"][1]
        require(member_prop["name"] == "pressure" and not member_prop["readonly"],
                "MEMBER property was not parsed as writable")
        method = by_name["fixtureValue"]["methods"][0]
        require(method["name"] == "registerPanel" and "panelId" in method["parameters"],
                "multiline invokable was not parsed")
        require(by_name["FixtureHelper"]["anonymous"] and not by_name["FixtureHelper"]["exported"],
                "anonymous helper classification was lost")
        require("FixturePrivate" not in by_name, "unregistered private class entered catalog")
        bridge = Path(temp) / "BridgeFixture.qml"
        bridge.write_text("""// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
QtObject {
    property bool active: true
}
""")
        bridge_entry = GEN.parse_qml(bridge, "QindaTK.QindaQt", "bridge_qml")
        require(bridge_entry["bridge"] and bridge_entry["module"] == "QindaTK.QindaQt",
                "bridge QML module classification was lost")


def repository_checks():
    before = json.loads(GEN.OUT.read_text())
    after = GEN.build()
    require(after["_license"] == "SPDX-License-Identifier: LGPL-3.0-or-later",
            "generated catalog lacks its source license marker")
    before_names = {item["name"] for item in before["types"]}
    after_names = {item["name"] for item in after["types"]}
    require(before_names <= after_names, "one or more existing catalog entries disappeared")
    require(len(after_names) == 101, "catalog should contain the expected 101 source entries")
    require({"stylusSample", "QindaQtTheme"} <= after_names,
            "value type or QindaQt bridge is missing")
    entries = {item["name"]: item for item in after["types"]}
    ramp = next(p for p in entries["GraphSeries"]["properties"] if p["name"] == "ramp")
    require(ramp["type"] == "QindaTK::ThemeRamp *", "GraphSeries.ramp pointer type was missed")
    dock = next(m for m in entries["DockModel"]["methods"] if m["name"] == "registerPanel")
    require("definition" in dock["parameters"], "DockModel.registerPanel declaration was missed")
    series_at = next(m for m in entries["Graph"]["methods"] if m["name"] == "seriesAt")
    require(series_at["returns"] == "QindaTK::GraphSeries *", "Graph.seriesAt pointer return was missed")
    stylus = entries["stylusSample"]
    expected_stylus = {"position", "scenePosition", "pressure", "tiltX", "tiltY", "rotation",
                       "pointerType", "buttons", "modifiers", "synthesizedFromMouse", "timestamp"}
    actual_stylus = {prop["name"] for prop in stylus["properties"]}
    require(actual_stylus == expected_stylus,
            "StylusSample property set differs from its Q_PROPERTY declarations")
    require(stylus["registrationKind"] == "qml_value_type" and stylus["exported"]
            and stylus["valueType"], "StylusSample registration provenance is incorrect")
    require(all(not prop["readonly"] for prop in stylus["properties"] if prop["name"] != "pointerType"),
            "StylusSample MEMBER property writability is incorrect")
    pointer_type = next(prop for prop in stylus["properties"] if prop["name"] == "pointerType")
    require(pointer_type["readonly"], "StylusSample READ property should be readonly")
    bridge = entries["QindaQtTheme"]
    require(bridge["module"] == "QindaTK.QindaQt" and bridge["bridge"],
            "bridge module metadata is incorrect")
    ordered = sorted(after["types"], key=lambda e: (e["module"] != "QindaTK",
                                                     e["kind"] != "qml", e["name"]))
    require([e["name"] for e in after["types"]] == [e["name"] for e in ordered],
            "catalog ordering is not deterministic by module, kind and name")


if __name__ == "__main__":
    fixture_checks()
    repository_checks()
    print("gen_catalog fixtures and preservation checks passed")
