#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""WP-088 Linux qualification driver (docs/qualification.md).

Builds each (Qt, variant) in build/qualify/<qt>-<variant>, runs the
configuration x operation matrix on the matrix variant under private
headless displays, installs every variant to a stage, moves it and runs the
qualification consumer from the moved stage, then writes
build/qualify/results/<run>/results.{json,md}.

    python3 tools/qualify/qualify.py --full --evidence   # the whole matrix
    python3 tools/qualify/qualify.py --smoke             # Qt 6.11, FFmpeg off
    python3 tools/qualify/qualify.py --list              # what would run

Exit status 0 when no row failed (unqualified rows are allowed), else 1.
"""
import argparse
import datetime
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from qualify_build import ROOT, Variant, build, configure  # noqa: E402
from qualify_checks import (check_atspi, check_cups, check_plugins, check_qtests,  # noqa: E402
                            check_render, check_tablet, result)
from qualify_env import DisplaySession, PrivateBus, configuration_environment  # noqa: E402
from qualify_install import install_and_relocate  # noqa: E402
from qualify_matrix import CONFIGURATIONS, OPERATIONS, QT_INSTALLS, VARIANTS, find  # noqa: E402
from qualify_report import Results  # noqa: E402

EVIDENCE_DIR = os.path.join(ROOT, "docs", "universal-toolkit", "implementation", "evidence")
ANY = "any (platform-independent)"


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--full", action="store_true", help="Qt 6.11 and 6.8, every variant, matrix on 'full'")
    parser.add_argument("--smoke", action="store_true", help="Qt 6.11, variant no-ffmpeg, matrix and install")
    parser.add_argument("--qt", default="", help="comma list of %s" % ", ".join(QT_INSTALLS))
    parser.add_argument("--variants", default="", help="comma list of %s" % ", ".join(VARIANTS))
    parser.add_argument("--matrix-variant", default="", help="the variant that runs the configuration matrix")
    parser.add_argument("--configs", default="", help="comma list (default: all)")
    parser.add_argument("--ops", default="", help="comma list (default: all)")
    parser.add_argument("--skip-build", action="store_true", help="configure only; reuse existing binaries")
    parser.add_argument("--no-install", action="store_true", help="skip install, relocation and consumer checks")
    parser.add_argument("--evidence", action="store_true",
                        help="also write WP-088-results.{json,md} into the evidence directory")
    parser.add_argument("--out", default=os.path.join(ROOT, "build", "qualify"), help="build/qualify root")
    parser.add_argument("--list", action="store_true", help="print the plan and exit")
    args = parser.parse_args(argv)
    if args.full:
        args.qt = args.qt or "6.11,6.8"
        args.variants = args.variants or ",".join(VARIANTS)
        args.matrix_variant = args.matrix_variant or "full"
    if args.smoke:
        args.qt = args.qt or "6.11"
        args.variants = args.variants or "no-ffmpeg"
        args.matrix_variant = args.matrix_variant or "no-ffmpeg"
    args.qt = [q for q in (args.qt or "6.11").split(",") if q]
    args.variants = [v for v in (args.variants or "full").split(",") if v]
    args.matrix_variant = args.matrix_variant or args.variants[0]
    args.configs = [find(CONFIGURATIONS, c) for c in args.configs.split(",") if c] or CONFIGURATIONS
    args.ops = [find(OPERATIONS, o) for o in args.ops.split(",") if o] or OPERATIONS
    for name in args.qt:
        if name not in QT_INSTALLS:
            parser.error("unknown Qt %s" % name)
    for name in args.variants + [args.matrix_variant]:
        if name not in VARIANTS:
            parser.error("unknown variant %s" % name)
    if args.matrix_variant not in args.variants:
        args.variants.insert(0, args.matrix_variant)
    return args


def run_operation(operation, configuration, variant, env, bus, log_dir, tag, activates=True):
    if operation.kind == "qtest":
        return check_qtests(operation, configuration, variant, env, bus, log_dir, tag, activates)
    if operation.kind == "atspi":
        return check_atspi(configuration, variant, env, bus, log_dir, tag)
    if operation.kind == "plugins":
        return check_plugins(configuration, variant, env, bus, log_dir, tag, ROOT)
    if operation.kind == "cups":
        return check_cups(operation, configuration, variant, env, bus, log_dir, tag)
    if operation.kind == "tablet":
        return check_tablet()
    raise ValueError(operation.kind)


def run_matrix(variant, args, results, log_dir, scratch):
    per_config = [op for op in args.ops if op.per_configuration and op.kind != "render"]
    for configuration in args.configs:
        tag = "%s-%s" % (variant.name, configuration.name)
        print("  %s" % configuration.name, flush=True)
        with DisplaySession(configuration.display, configuration.name, log_dir) as display:
            if display.unavailable:
                for operation in [op for op in args.ops if op.per_configuration]:
                    results.add(variant.qt, variant.variant, configuration.name, operation.name,
                                result("unqualified", display.unavailable))
                continue
            env = configuration_environment(configuration, display, variant.qt_prefix)
            bus = PrivateBus(scratch)
            gate, activates = "", True
            if any(op.kind == "render" for op in args.ops):
                render, fields = check_render(configuration, variant, env, bus, log_dir, tag)
                activates = fields.get("active", "true") == "true"
                render["evidence"].insert(0, display.detail or "no display server (offscreen)")
                results.add(variant.qt, variant.variant, configuration.name, "render", render)
                if render["status"] == "unqualified":
                    gate = "configuration not established: " + render["reason"]
            for operation in per_config:
                if gate:
                    outcome = result("unqualified", gate)
                else:
                    outcome = run_operation(operation, configuration, variant, env, bus, log_dir,
                                            "%s-%s" % (tag, operation.name), activates)
                print("    %-22s %s %s" % (operation.name, outcome["status"], outcome["reason"][:100]), flush=True)
                results.add(variant.qt, variant.variant, configuration.name, operation.name, outcome)
    once = [op for op in args.ops if not op.per_configuration]
    if once:
        baseline = find(CONFIGURATIONS, "offscreen-software")
        with DisplaySession("none", "independent", log_dir) as display:
            env = configuration_environment(baseline, display, variant.qt_prefix)
            bus = PrivateBus(scratch)
            for operation in once:
                outcome = run_operation(operation, baseline, variant, env, bus, log_dir,
                                        "%s-any-%s" % (variant.name, operation.name))
                print("  %-24s %s %s" % (operation.name, outcome["status"], outcome["reason"][:100]), flush=True)
                results.add(variant.qt, variant.variant, ANY, operation.name, outcome)


def main(argv):
    args = parse_arguments(argv)
    if args.list:
        print("Qt: %s" % ", ".join(args.qt))
        print("variants: %s (matrix on %s)" % (", ".join(args.variants), args.matrix_variant))
        print("configurations: %s" % ", ".join(c.name for c in args.configs))
        print("operations: %s" % ", ".join(o.name for o in args.ops))
        return 0
    run_id = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(args.out, "results", run_id)
    log_dir = os.path.join(out_dir, "logs")
    os.makedirs(log_dir, exist_ok=True)
    results = Results(run_id, out_dir, argv)
    scratch = tempfile.mkdtemp(prefix="q88-")
    try:
        for qt in args.qt:
            resolved = {}
            for name in args.variants:
                variant = Variant(qt, name, args.out, matrix=(name == args.matrix_variant))
                print("== %s" % variant.label, flush=True)
                configure(variant, log_dir)
                twin = next((other for other, features in resolved.items()
                             if features == variant.features and features), None)
                if twin is not None and not variant.matrix and not variant.error and not variant.unavailable:
                    # Same resolved engines as an earlier variant: the same binaries.
                    note = "same resolved engines as %s; not rebuilt, results reused" % twin
                    results.add_build(variant, "passed", note)
                    for row in [r for r in results.rows if r["qt"] == qt and r["variant"] == twin
                                and r["configuration"] == "installed"]:
                        results.add(qt, name, "installed", row["operation"],
                                    {"status": row["status"], "reason": row["reason"],
                                     "evidence": [note] + row["evidence"], "log": row["log"]})
                    continue
                if not variant.error and not variant.unavailable and not args.skip_build:
                    build(variant, log_dir)
                if variant.unavailable or variant.error:
                    status = "unqualified" if variant.unavailable else "failed"
                    reason = variant.unavailable or variant.error
                    results.add_build(variant, status, reason)
                    for check in ("install-relocation", "optional-engines"):
                        results.add(qt, name, "installed", check, result(status, reason))
                    if variant.matrix:
                        for configuration in args.configs:
                            for operation in args.ops:
                                cfg = configuration.name if operation.per_configuration else ANY
                                if not any(r["configuration"] == cfg and r["operation"] == operation.name
                                           and r["qt"] == qt and r["variant"] == name for r in results.rows):
                                    results.add(qt, name, cfg, operation.name, result(status, reason))
                    continue
                results.add_build(variant, "passed")
                resolved[name] = variant.features
                if variant.matrix:
                    run_matrix(variant, args, results, log_dir, scratch)
                if not args.no_install:
                    relocation, optional = install_and_relocate(variant, log_dir)
                    results.add(qt, name, "installed", "install-relocation", relocation)
                    results.add(qt, name, "installed", "optional-engines", optional)
                    print("  install-relocation %s %s" % (relocation["status"], relocation["reason"][:120]))
                    print("  optional-engines   %s %s" % (optional["status"], optional["reason"][:120]))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
        written = results.write((EVIDENCE_DIR,) if args.evidence else ())
        print("results: %s" % ", ".join(os.path.relpath(p, ROOT) for p in written))
    return 1 if any(row["status"] == "failed" for row in results.rows) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
