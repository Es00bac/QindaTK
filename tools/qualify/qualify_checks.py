# SPDX-License-Identifier: LGPL-3.0-or-later
"""Operation checks for one runtime configuration of one build variant.

Status rules (docs/qualification.md): "passed" only when every selected
check ran and passed on this configuration; "failed" when one failed or the
process crashed or timed out; "unqualified" when the configuration or a
check could not run here (a QSKIP, a missing device, service or build
target). A skip is never counted as a pass.
"""
import glob
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

from qualify_env import cups_state, tablet_devices

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
TEST_TIMEOUT = 600
PROBE_MARK = "QTK_QUALIFY"
PROBE_RGB = (0x12, 0xA4, 0xD6)  # tools/qualify/fixtures/render_probe.qml


def result(status, reason="", evidence=None, log=""):
    return {"status": status, "reason": reason, "evidence": evidence or [], "log": log}


def parse_qtest_xml(path):
    """Returns (passed, failed, skipped) lists of (function, message)."""
    passed, failed, skipped = [], [], []
    try:
        tree = ET.parse(path)
    except (OSError, ET.ParseError) as error:
        return [], [("<xml>", "unreadable QtTest XML: %s" % error)], []
    for function in tree.getroot().iter("TestFunction"):
        name = function.get("name", "?")
        states = []
        for node in list(function.iter("Incident")) + list(function.iter("Message")):
            kind = node.get("type", "")
            text = "".join(node.findtext("Description") or "").strip()
            if kind in ("fail", "xpass", "qfatal"):
                states.append(("fail", text))
            elif kind == "skip":
                states.append(("skip", text))
            elif kind in ("pass", "xfail", "bpass", "bfail"):
                states.append(("pass", text))
        if any(state == "fail" for state, _ in states):
            failed.append((name, next(text for state, text in states if state == "fail")))
        elif any(state == "skip" for state, _ in states):
            skipped.append((name, next(text for state, text in states if state == "skip")))
        elif states:
            passed.append((name, ""))
    return passed, failed, skipped


def run_qtest(variant, run, env, bus, log_dir, tag):
    """Runs one QtTest binary; returns (status, evidence line, reason)."""
    binary = variant.binary(run.binary)
    if binary is None:
        return "missing", run.binary, "%s is not built in %s" % (run.binary, variant.name)
    suffix = run.binary + ("-" + "-".join(run.functions) if run.functions else "")
    xml = os.path.join(log_dir, "%s-%s.xml" % (tag, suffix[:120]))
    txt = os.path.join(log_dir, "%s-%s.txt" % (tag, suffix[:120]))
    env = dict(env, QML_IMPORT_PATH=os.path.join(variant.build_dir, "qml"))
    command = bus.wrap([binary] + list(run.functions) + ["-o", xml + ",xml", "-o", txt + ",txt"])
    try:
        completed = subprocess.run(command, env=env, capture_output=True, text=True,
                                   timeout=TEST_TIMEOUT, cwd=variant.build_dir)
        code, output = completed.returncode, completed.stdout + completed.stderr
    except subprocess.TimeoutExpired as error:
        code, output = "timeout", str(error.stdout or "")
    with open(txt + ".stderr", "w") as extra:
        extra.write(output)
    passed, failed, skipped = parse_qtest_xml(xml)
    real = [f for f in passed if f[0] not in ("initTestCase", "cleanupTestCase")]
    line = "%s: %d passed, %d failed, %d skipped" % (run.binary, len(real), len(failed), len(skipped))
    if code == "timeout":
        return "failed", line, "%s timed out after %d s" % (run.binary, TEST_TIMEOUT)
    if failed or code != 0:
        detail = "; ".join("%s: %s" % (name, text[:160]) for name, text in failed) or "exit status %s" % code
        return "failed", line, "%s: %s" % (run.binary, detail)
    if skipped:
        detail = "; ".join("%s: %s" % (name, text[:160]) for name, text in skipped)
        return "skipped", line, "%s skipped %s" % (run.binary, detail)
    if not real:
        return "skipped", line, "%s ran no test function" % run.binary
    return "passed", line, ""


def check_qtests(operation, configuration, variant, env, bus, log_dir, tag, activates=True):
    evidence, failures, skips, missing = [], [], [], []
    for run in operation.tests:
        if (run.renderer == "gpu" and not configuration.gpu) or (run.renderer == "software" and configuration.gpu):
            continue
        if run.needs_activation and not activates:
            skips.append("%s not run: this compositor never activates a window (headless Weston has no seat)"
                         % run.binary)
            continue
        status, line, reason = run_qtest(variant, run, env, bus, log_dir, tag)
        evidence.append(line)
        if status == "failed":
            failures.append(reason)
        elif status == "skipped":
            skips.append(reason)
        elif status == "missing":
            missing.append(reason)
    if failures:
        return result("failed", " | ".join(failures), evidence)
    if missing and operation.missing:
        return result("unqualified", "%s (%s)" % (operation.missing, "; ".join(missing)), evidence)
    if missing:
        return result("failed", "; ".join(missing), evidence)
    if skips:
        return result("unqualified", " | ".join(skips), evidence)
    return result("passed", "", evidence)


def check_render(configuration, variant, env, bus, log_dir, tag):
    """Grabs the probe scene; verifies API, platform and pixels.

    Returns the result and the probe's fields (api, platform, active), so
    the caller can refuse to qualify a configuration whose renderer fell back
    and knows whether the compositor activates windows.
    """
    preview = variant.binary("qtk-preview")
    if preview is None:
        return result("failed", "qtk-preview is not built"), {}
    png = os.path.join(log_dir, "%s-render.png" % tag)
    command = [preview, os.path.join(FIXTURES, "render_probe.qml"), "--grab", png, "--size", "320x200",
               "--wait", "4500"]
    if configuration.gpu:
        command.append("--gpu")
    try:
        completed = subprocess.run(bus.wrap(command), env=env, capture_output=True, text=True, timeout=120)
        output, code = completed.stdout + completed.stderr, completed.returncode
    except subprocess.TimeoutExpired:
        return result("failed", "qtk-preview timed out"), {}
    with open(os.path.join(log_dir, "%s-render.log" % tag), "w") as log:
        log.write(output)
    fields = {}
    for line in output.splitlines():
        if PROBE_MARK in line:
            for token in line.split(PROBE_MARK, 1)[1].split():
                key, _, value = token.partition("=")
                fields[key] = value
    api, platform = fields.get("api", ""), fields.get("platform", "")
    evidence = ["qtk-preview exit %d, api=%s, platform=%s, window activated=%s"
                % (code, api or "?", platform or "?", fields.get("active", "?"))]
    no_gpu = next((line.strip() for line in output.splitlines()
                   if "Failed to create RHI" in line or "Failed to initialize graphics backend" in line), "")
    if configuration.gpu and not api and no_gpu:
        # Qt aborts when the platform offers no graphics context for the
        # requested API (the offscreen platform has none without an X server).
        return result("unqualified", "Qt could not create a %s context here: %s"
                      % (configuration.rhi or "GPU", no_gpu), evidence), fields
    if code != 0 or not api:
        return result("failed", "render probe exited %d without reporting its API" % code, evidence), fields
    if platform != configuration.platform:
        return result("unqualified", "requested platform %s, Qt used %s" % (configuration.platform, platform),
                      evidence), fields
    expected = configuration.expect_api
    if (expected == "rhi" and api in ("software", "unknown")) or (expected != "rhi" and api != expected):
        return result("unqualified", "requested %s rendering, the scene graph used %s" % (expected, api),
                      evidence), fields
    try:
        from PIL import Image
    except ImportError:
        return result("unqualified", "Pillow is not installed; the grabbed pixels were not checked", evidence), fields
    image = Image.open(png).convert("RGB")
    pixel = image.getpixel((image.width // 2, image.height // 2))
    evidence.append("grab %dx%d, centre pixel #%02x%02x%02x" % ((image.width, image.height) + pixel))
    if max(abs(a - b) for a, b in zip(pixel, PROBE_RGB)) > 6:
        return result("failed", "centre pixel #%02x%02x%02x, expected #%02x%02x%02x" % (pixel + PROBE_RGB),
                      evidence), fields
    return result("passed", "", evidence), fields


def check_atspi(configuration, variant, env, bus, log_dir, tag):
    if configuration.platform == "offscreen":
        return result("unqualified", "the offscreen platform has no AT-SPI bridge")
    preview = variant.binary("qtk-preview")
    if preview is None:
        return result("failed", "qtk-preview is not built")
    out = os.path.join(log_dir, "%s-atspi.json" % tag)
    command = bus.wrap([sys.executable, os.path.join(HERE, "atspi_probe.py"), "--preview", preview,
                        "--qml", os.path.join(FIXTURES, "atspi_probe.qml"), "--out", out])
    probe_env = dict(env, QT_LINUX_ACCESSIBILITY_ALWAYS_ON="1", QT_ACCESSIBILITY="1")
    try:
        completed = subprocess.run(command, env=probe_env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return result("failed", "AT-SPI probe timed out")
    with open(os.path.join(log_dir, "%s-atspi.log" % tag), "w") as log:
        log.write(completed.stdout + completed.stderr)
    if completed.returncode == 3:
        return result("unqualified", completed.stdout.strip().splitlines()[-1] if completed.stdout.strip()
                      else "AT-SPI client libraries are unavailable")
    if completed.returncode != 0:
        tail = (completed.stdout + completed.stderr).strip().splitlines()[-3:]
        return result("failed", "AT-SPI probe exit %d: %s" % (completed.returncode, " / ".join(tail)))
    return result("passed", "", ["AT-SPI: button and check box found, Press delivered and observed (%s)"
                                 % os.path.basename(out)])


def check_cups(operation, configuration, variant, env, bus, log_dir, tag):
    running, queues = cups_state()
    if not running:
        return result("unqualified", "the CUPS scheduler is not running on this host")
    if not queues:
        return result("unqualified", "CUPS is running but has no printer queue")
    outcome = check_qtests(operation, configuration, variant, env, bus, log_dir, tag)
    outcome["evidence"].append("CUPS queues: %s" % ", ".join(queues))
    return outcome


def check_tablet():
    devices = tablet_devices()
    if not devices:
        return result("unqualified", "no pen tablet is attached; synthetic tablet events are covered by 'input'")
    return result("unqualified", "tablet present (%s) but a physical pen run needs a person; not automated"
                  % ", ".join(devices))


def check_plugins(configuration, variant, env, bus, log_dir, tag, root):
    headers = glob.glob(os.path.join(root, "src/core/plugin_host*.h")) + \
        glob.glob(os.path.join(root, "src/core/plugin_editor*.h"))
    tests = sorted(os.path.basename(path) for path in glob.glob(os.path.join(variant.build_dir, "tests", "tst_plugin*"))
                   if os.access(path, os.X_OK) and not path.endswith((".xml", ".txt")))
    if not headers and not tests:
        return result("unqualified", "not yet available: WP-066 plugin host and editor types are not in this tree")
    if not tests:
        return result("failed", "plugin host sources exist but no tst_plugin* test is built")

    class Op:  # the plugin tests are discovered, not listed in the matrix
        missing = ""
        tests = []
    from qualify_matrix import TestRun
    Op.tests = [TestRun(name) for name in tests]
    return check_qtests(Op, configuration, variant, env, bus, log_dir, tag)
