# SPDX-License-Identifier: LGPL-3.0-or-later
"""Configure and build one qualification variant in build/qualify/<name>.

AGENT-NOTE: the job limit is the user's explicit setting for this work
(2026-09-23): `cmake --build <dir> -- -j12 -l12`, command-local. Never add a
persistent MAKEOPTS, preset or job pool. ccache shares objects between the
worktrees through CCACHE_BASEDIR.
"""
import json
import os
import subprocess
import time

from qualify_matrix import LIBRARY_TARGETS, QT_INSTALLS, VARIANTS, matrix_test_targets

BUILD_JOBS = ["-j12", "-l12"]


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def build_environment():
    env = dict(os.environ)
    env["CCACHE_BASEDIR"] = os.path.dirname(ROOT)  # ~/work_SPaC3: shared by every worktree
    env["CCACHE_NOHASHDIR"] = "1"
    return env


class Variant:
    """A (Qt install, build variant) pair and its directories."""

    def __init__(self, qt, variant, out_root, matrix):
        self.qt = qt
        self.variant = variant
        self.qt_prefix = QT_INSTALLS[qt]["prefix"]
        self.name = "%s-%s" % (QT_INSTALLS[qt]["key"], variant)
        self.matrix = matrix  # also runs the configuration x operation matrix
        self.build_dir = os.path.join(out_root, self.name)
        self.stage = os.path.join(out_root, "stage", self.name)
        self.relocated = os.path.join(out_root, "relocated", self.name)
        self.consumer_build = os.path.join(out_root, "consumer", self.name)
        self.features = {}
        self.error = ""
        self.unavailable = ""

    @property
    def label(self):
        return "Qt %s / %s" % (self.qt, self.variant)

    def cmake_arguments(self):
        args = ["-S", ROOT, "-B", self.build_dir, "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
                "-DQINDATK_BUILD_EXAMPLES=OFF", "-DQINDATK_BUILD_TOOLS=ON",
                "-DBUILD_TESTING=" + ("ON" if self.matrix else "OFF"),
                "-DCMAKE_INSTALL_PREFIX=" + self.stage]
        if self.qt_prefix:
            args.append("-DCMAKE_PREFIX_PATH=" + self.qt_prefix)
            # The desktop bridge links system-Qt archives; tools/preview refuses them anyway.
            args.append("-DQINDATK_PREVIEW_QINDAQT=OFF")
        for option, value in VARIANTS[self.variant].items():
            args.append("-D%s=%s" % (option, value))
        return args

    def targets(self):
        present = self.available_targets()
        wanted = list(LIBRARY_TARGETS)
        if self.matrix:
            wanted += matrix_test_targets() + sorted(t for t in present if t.startswith("tst_plugin"))
        return [t for t in wanted if t in present]

    def available_targets(self):
        result = subprocess.run(["ninja", "-C", self.build_dir, "-t", "targets", "all"],
                                capture_output=True, text=True)
        names = set()
        for line in result.stdout.splitlines():
            name = line.split(":", 1)[0].strip()
            if "/" not in name and "." not in name:
                names.add(name)
        return names

    def binary(self, name):
        for directory in ("tests", "tools/preview", "src"):
            path = os.path.join(self.build_dir, directory, name)
            if os.path.isfile(path) and os.access(path, os.X_OK):
                return path
        return None


def run_logged(command, log_path, env=None, cwd=None, timeout=None):
    with open(log_path, "a") as log:
        log.write("$ %s\n" % " ".join(command))
        log.flush()
        started = time.monotonic()
        try:
            code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=cwd,
                                  timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = -1
        log.write("# exit %d after %.0f s\n" % (code, time.monotonic() - started))
    return code


def configure(variant, log_dir):
    """Configures; sets variant.features, or variant.error / variant.unavailable."""
    log = os.path.join(log_dir, variant.name + "-build.log")
    if variant.qt_prefix and not os.path.isdir(variant.qt_prefix):
        variant.unavailable = "Qt %s SDK not found at %s" % (variant.qt, variant.qt_prefix)
        return False
    if run_logged(["cmake"] + variant.cmake_arguments(), log, env=build_environment()) != 0:
        variant.error = "configure failed (see %s)" % os.path.relpath(log, ROOT)
        return False
    try:
        with open(os.path.join(variant.build_dir, "qindatk-features.json")) as summary:
            variant.features = json.load(summary)
    except (OSError, ValueError) as error:
        variant.error = "no qindatk-features.json after configure: %s" % error
        return False
    return True


def build(variant, log_dir):
    """Builds only the targets the variant's checks need."""
    log = os.path.join(log_dir, variant.name + "-build.log")
    command = ["cmake", "--build", variant.build_dir, "--target"] + variant.targets() + ["--"] + BUILD_JOBS
    if run_logged(command, log, env=build_environment()) != 0:
        variant.error = "build failed (see %s)" % os.path.relpath(log, ROOT)
        return False
    return True
