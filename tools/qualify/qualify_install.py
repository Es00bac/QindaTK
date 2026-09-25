# SPDX-License-Identifier: LGPL-3.0-or-later
"""Install, relocation and optional-engine consumer checks (WP-088 steps 3-4).

For one built variant: install to a stage prefix, build the lightweight
consumer (tests/consumer/qualify) against the stage and install it there,
move the whole stage to another path, then run the consumer, the QML-only
consumer and the installed qtk-preview from the moved stage. They must work
without source-tree, build-tree or old-stage paths, which is checked three
ways: QML_IMPORT_TRACE and LD_DEBUG while running, ldd, and `strings` over
every installed file (debug sections stripped first).
"""
import json
import os
import re
import shutil
import subprocess
import tempfile

from qualify_build import ROOT, build_environment, run_logged, BUILD_JOBS
from qualify_checks import result
from qualify_env import clean_environment
from qualify_matrix import FEATURE_CAPABILITIES

CONSUMER_SOURCE = os.path.join(ROOT, "tests", "consumer", "qualify")
FIXTURE = os.path.join(ROOT, "tools", "qualify", "fixtures", "render_probe.qml")


def is_elf(path):
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == b"\x7fELF"
    except OSError:
        return False


def forbidden_prefixes(variant):
    """Paths an installed file must never name: the worktree (source, build
    and the original stage are all inside it) and the pre-move stage."""
    return sorted({os.path.realpath(ROOT), ROOT, variant.stage})


def scan_installed_files(prefix, forbidden, scratch):
    """Returns (hits, dependency_paths, runpaths)."""
    hits, dependency, runpaths = [], set(), {}
    for directory, _, files in os.walk(prefix):
        for name in files:
            path = os.path.join(directory, name)
            if os.path.islink(path):
                continue
            relative = os.path.relpath(path, prefix)
            if is_elf(path):
                stripped = os.path.join(scratch, "stripped")
                subprocess.run(["objcopy", "--strip-debug", path, stripped], capture_output=True)
                text = subprocess.run(["strings", "-a", stripped if os.path.exists(stripped) else path],
                                      capture_output=True, text=True, errors="replace").stdout
                dynamic = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
                match = re.search(r"R(?:UN)?PATH\)\s+Library r(?:un)?path: \[(.*)\]", dynamic)
                if match:
                    runpaths[relative] = match.group(1)
            else:
                with open(path, "rb") as handle:
                    text = handle.read().decode("utf-8", "replace")
            for prefix_path in forbidden:
                if prefix_path in text:
                    hits.append("%s names %s" % (relative, prefix_path))
            for found in re.findall(r"/home/[^\s\"':;]+", text):
                if not any(found.startswith(p) for p in forbidden):
                    dependency.add(found)
    return hits, sorted(dependency), runpaths


def expected_capabilities(features):
    return {capability: bool(features.get(feature)) for feature, capability in FEATURE_CAPABILITIES.items()}


def install_and_relocate(variant, log_dir):
    """Returns (relocation result, optional-engine result)."""
    log = os.path.join(log_dir, variant.name + "-install.log")
    env = build_environment()
    for path in (variant.stage, variant.relocated, variant.consumer_build):
        shutil.rmtree(path, ignore_errors=True)
    if run_logged(["cmake", "--install", variant.build_dir, "--prefix", variant.stage], log, env=env) != 0:
        failure = result("failed", "cmake --install failed", log=log)
        return failure, result("failed", "not installed", log=log)
    prefix_path = variant.stage + (";" + variant.qt_prefix if variant.qt_prefix else "")
    configure = ["cmake", "-S", CONSUMER_SOURCE, "-B", variant.consumer_build, "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_PREFIX_PATH=" + prefix_path,
                 "-DCMAKE_INSTALL_PREFIX=" + variant.stage]
    if run_logged(configure, log, env=env) != 0:
        failure = result("failed", "the consumer did not configure against the installed prefix", log=log)
        return failure, failure
    if run_logged(["cmake", "--build", variant.consumer_build, "--"] + BUILD_JOBS, log, env=env) != 0:
        failure = result("failed", "the consumer did not build or link against the installed prefix", log=log)
        return failure, failure
    if run_logged(["cmake", "--install", variant.consumer_build], log, env=env) != 0:
        failure = result("failed", "the consumer did not install", log=log)
        return failure, failure

    # Move the stage: nothing below may use the old path, the build or the source tree.
    os.makedirs(os.path.dirname(variant.relocated), exist_ok=True)
    shutil.move(variant.stage, variant.relocated)
    prefix = variant.relocated
    forbidden = forbidden_prefixes(variant)
    evidence = ["stage %s moved to %s" % (os.path.relpath(variant.stage, ROOT), os.path.relpath(prefix, ROOT))]
    problems = []

    run_env = clean_environment()
    run_env.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                   XDG_RUNTIME_DIR=tempfile.mkdtemp(prefix="q88-install-"))
    os.chmod(run_env["XDG_RUNTIME_DIR"], 0o700)
    if variant.qt_prefix:
        # The SDK Qt is not on the default linker path; this names the Qt
        # runtime only, never a QindaTK path.
        run_env["LD_LIBRARY_PATH"] = os.path.join(variant.qt_prefix, "lib")
        evidence.append("Qt runtime from LD_LIBRARY_PATH=%s/lib (the isolated SDK)" % variant.qt_prefix)
    traced = dict(run_env, QML_IMPORT_TRACE="1", LD_DEBUG="libs")

    bin_dir = os.path.join(prefix, "bin")
    consumer = os.path.join(bin_dir, "qtk_qualify_consumer")
    completed = subprocess.run([consumer], env=traced, capture_output=True, text=True, timeout=120, cwd=prefix)
    with open(os.path.join(log_dir, variant.name + "-consumer.log"), "w") as out:
        out.write(completed.stdout + "\n--- stderr ---\n" + completed.stderr)
    report = {}
    try:
        report = json.loads(completed.stdout[completed.stdout.index("{"):])
    except ValueError:
        problems.append("the consumer printed no JSON report (exit %d)" % completed.returncode)
    if completed.returncode != 0:
        problems.append("qtk_qualify_consumer exit %d" % completed.returncode)
    loaded = [line for line in completed.stderr.splitlines() if "calling init:" in line]
    for line in loaded + [l for l in completed.stderr.splitlines() if "QQmlImport" in l or "qmldir" in l]:
        for bad in forbidden:
            if bad in line and prefix not in line:
                problems.append("run-time load from a developer path: %s" % line.strip()[:200])
    plugin_lines = [l for l in loaded if "libqindatkplugin" in l or "libqindatk.so" in l]
    evidence += ["loaded %s" % l.split("calling init:", 1)[1].strip().replace(prefix, "<moved stage>")
                 for l in plugin_lines[:3]]

    qml_only = subprocess.run([os.path.join(bin_dir, "qtk_qualify_qml_only")], env=traced, capture_output=True,
                              text=True, timeout=120, cwd=prefix)
    with open(os.path.join(log_dir, variant.name + "-qml-only.log"), "w") as out:
        out.write(qml_only.stdout + "\n--- stderr ---\n" + qml_only.stderr)
    if qml_only.returncode != 0:
        tail = " / ".join(qml_only.stderr.strip().splitlines()[-2:])
        problems.append("the QML-only consumer failed (exit %d): %s" % (qml_only.returncode, tail[:300]))
    else:
        evidence.append(qml_only.stdout.strip().replace(prefix, "<moved stage>"))

    # The installed tool, run on a fixture copied out of the source tree.
    fixture_dir = tempfile.mkdtemp(prefix="q88-fixture-")
    fixture = shutil.copy(FIXTURE, fixture_dir)
    preview = subprocess.run([os.path.join(bin_dir, "qtk-preview"), fixture, "--check"], env=traced,
                             capture_output=True, text=True, timeout=120, cwd=prefix)
    with open(os.path.join(log_dir, variant.name + "-preview.log"), "w") as out:
        out.write(preview.stdout + "\n--- stderr ---\n" + preview.stderr)
    if preview.returncode != 0:
        tail = " / ".join(preview.stderr.strip().splitlines()[-2:])
        problems.append("the installed qtk-preview failed (exit %d): %s" % (preview.returncode, tail[:300]))
    else:
        evidence.append("installed qtk-preview --check: exit 0")
    shutil.rmtree(fixture_dir, ignore_errors=True)
    shutil.rmtree(run_env["XDG_RUNTIME_DIR"], ignore_errors=True)

    ldd = subprocess.run(["ldd", consumer], env=run_env, capture_output=True, text=True).stdout
    qindatk_line = next((l.strip() for l in ldd.splitlines() if "libqindatk.so" in l), "libqindatk not linked")
    evidence.append("ldd: " + qindatk_line.replace(prefix, "<moved stage>"))
    if prefix not in qindatk_line:
        problems.append("ldd resolves libqindatk outside the moved stage: %s" % qindatk_line)

    with tempfile.TemporaryDirectory(prefix="q88-strings-") as scratch:
        hits, dependency, runpaths = scan_installed_files(prefix, forbidden, scratch)
    problems += ["installed file %s" % hit for hit in hits]
    bad_runpaths = {k: v for k, v in runpaths.items()
                    if any(not (p.startswith("$ORIGIN") or p.startswith("/usr/")) for p in v.split(":") if p)}
    for name, value in bad_runpaths.items():
        problems.append("RUNPATH of %s is not relocatable: %s" % (name, value))
    evidence.append("strings/readelf over %d RUNPATH-bearing ELF files: %d developer-path hits"
                    % (len(runpaths), len(hits)))
    if dependency:
        evidence.append("other absolute /home paths (dependencies, not QindaTK): %s"
                        % ", ".join(sorted(dependency)[:4]))
    relocation = result("failed" if problems else "passed", "; ".join(problems[:8]), evidence,
                        log=os.path.relpath(log, ROOT))

    # Optional engines: the consumer's report must match what the build resolved.
    capabilities = report.get("capabilities", {})
    mismatches, lines = [], []
    for capability, wanted in expected_capabilities(variant.features).items():
        entry = capabilities.get(capability)
        if entry is None:
            mismatches.append("%s missing from the report" % capability)
            continue
        available, reason = entry.get("available"), entry.get("reason", "")
        lines.append("%s: %s" % (capability, "available" if available else "unavailable - " + reason))
        if bool(available) != wanted:
            mismatches.append("%s available=%s but the build resolved %s" % (capability, available, wanted))
        if not available and not reason:
            mismatches.append("%s is unavailable without a reason" % capability)
    if not report.get("qmlLoaded"):
        mismatches.append("the consumer did not import QindaTK and instantiate its scene")
    optional = result("failed" if mismatches else "passed", "; ".join(mismatches), lines,
                      log=os.path.relpath(os.path.join(log_dir, variant.name + "-consumer.log"), ROOT))
    return relocation, optional
