# SPDX-License-Identifier: LGPL-3.0-or-later
"""Private, silent display and bus sessions for the qualification runs.

AGENT-GUARD: nothing here may reach the user's desktop. Every run drops
DISPLAY, WAYLAND_DISPLAY and the session bus, uses its own XDG_RUNTIME_DIR,
and gets a headless Weston (and, for xcb, a rootful Xwayland inside that
Weston). Tests that open a file dialog or talk to AT-SPI then do so on a
private D-Bus that can start only the accessibility bus. Using the user's
DISPLAY instead would put windows and dialogs on their screen.
"""
import os
import shutil
import subprocess
import tempfile
import time

SCRUBBED = ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "QT_QPA_PLATFORM",
            "QT_QPA_PLATFORMTHEME", "QT_QUICK_BACKEND", "QSG_RHI_BACKEND", "QML_IMPORT_PATH",
            "QML2_IMPORT_PATH", "QT_PLUGIN_PATH", "QT_IM_MODULE", "GTK_IM_MODULE", "XMODIFIERS",
            "XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE", "XDG_SESSION_ID", "XDG_RUNTIME_DIR",
            "QT_LINUX_ACCESSIBILITY_ALWAYS_ON", "QT_ACCESSIBILITY", "LD_LIBRARY_PATH",
            "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_AUTO_SCREEN_SCALE_FACTOR")

BUS_CONFIG = """<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <keep_umask/>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>{services}</servicedir>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
"""
A11Y_SERVICE = "/usr/share/dbus-1/services/org.a11y.Bus.service"


def clean_environment():
    env = {k: v for k, v in os.environ.items() if k not in SCRUBBED}
    env["QT_QPA_PLATFORMTHEME"] = ""  # the generic theme; no desktop integration plugin
    return env


def wait_for(predicate, timeout, step=0.1):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return None


class PrivateBus:
    """A dbus-run-session config whose bus can start only org.a11y.Bus."""

    def __init__(self, scratch):
        self.dir = tempfile.mkdtemp(prefix="bus-", dir=scratch)
        services = os.path.join(self.dir, "services")
        os.mkdir(services)
        if os.path.exists(A11Y_SERVICE):
            shutil.copy(A11Y_SERVICE, services)
        self.config = os.path.join(self.dir, "session.conf")
        with open(self.config, "w") as out:
            out.write(BUS_CONFIG.format(services=services))

    def wrap(self, command):
        if not shutil.which("dbus-run-session"):
            return command
        return ["dbus-run-session", "--config-file=" + self.config, "--"] + list(command)


class DisplaySession:
    """One headless display for one configuration, torn down afterwards.

    kind: none | weston-pixman | weston-gl | xwayland.
    `unavailable` is set to the reason when the display cannot start; the
    caller then records the configuration as unqualified.
    """

    def __init__(self, kind, tag, log_dir):
        self.kind = kind
        self.tag = tag
        self.log_dir = log_dir
        self.processes = []
        self.unavailable = ""
        self.detail = ""
        # A Unix socket path must fit in 108 bytes: keep the runtime dir short.
        base = "/run/user/%d" % os.getuid()
        if not os.path.isdir(base):
            base = tempfile.gettempdir()
        self.runtime = tempfile.mkdtemp(prefix="q88-", dir=base)
        os.chmod(self.runtime, 0o700)
        self.env = {"XDG_RUNTIME_DIR": self.runtime}

    def __enter__(self):
        try:
            self._start()
        except Exception as error:  # reported as unqualified, never raised
            self.unavailable = "display did not start: %r" % (error,)
        return self

    def __exit__(self, *exc):
        for process in reversed(self.processes):
            process.terminate()
            try:
                process.wait(10)
            except subprocess.TimeoutExpired:
                process.kill()
        shutil.rmtree(self.runtime, ignore_errors=True)

    def _spawn(self, command, env, name):
        log = open(os.path.join(self.log_dir, "%s-%s.log" % (self.tag, name)), "w")
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, cwd=self.runtime)
        self.processes.append(process)
        return process

    def _start(self):
        if self.kind == "none":
            return
        if not shutil.which("weston"):
            self.unavailable = "weston is not installed"
            return
        renderer = "pixman" if self.kind == "weston-pixman" else "gl"
        socket = "q88-" + self.tag[:24]
        env = dict(clean_environment(), XDG_RUNTIME_DIR=self.runtime)
        weston = self._spawn(["weston", "--no-config", "--backend=headless", "--renderer=" + renderer,
                              "--socket=" + socket, "--width=1280", "--height=800", "--idle-time=0"],
                             env, "weston")
        if not wait_for(lambda: os.path.exists(os.path.join(self.runtime, socket)) or weston.poll() is not None, 20):
            self.unavailable = "weston (%s renderer) did not create its socket" % renderer
            return
        if weston.poll() is not None:
            self.unavailable = "weston (%s renderer) exited with %d" % (renderer, weston.returncode)
            return
        self.env["WAYLAND_DISPLAY"] = socket
        self.detail = "headless weston, %s renderer" % renderer
        if self.kind != "xwayland":
            return
        if not shutil.which("Xwayland"):
            self.unavailable = "no private X server: Xwayland is not installed (the user's DISPLAY is never used)"
            return
        number = next((n for n in range(180, 240) if not os.path.exists("/tmp/.X11-unix/X%d" % n)
                       and not os.path.exists("/tmp/.X%d-lock" % n)), None)
        if number is None:
            self.unavailable = "no free X display number for a private Xwayland"
            return
        xenv = dict(env, WAYLAND_DISPLAY=socket)
        xserver = self._spawn(["Xwayland", ":%d" % number, "-geometry", "1280x800", "-nolisten", "tcp",
                               "-noreset"], xenv, "xwayland")
        if not wait_for(lambda: os.path.exists("/tmp/.X11-unix/X%d" % number) or xserver.poll() is not None, 20):
            self.unavailable = "private Xwayland did not start"
            return
        if xserver.poll() is not None:
            self.unavailable = "private Xwayland exited with %d" % xserver.returncode
            return
        del self.env["WAYLAND_DISPLAY"]
        self.env["DISPLAY"] = ":%d" % number
        self.detail = "rootful Xwayland :%d inside headless weston (gl renderer)" % number


def configuration_environment(configuration, display, qt_prefix=None):
    """The environment of every process run for `configuration`."""
    env = clean_environment()
    env.update(display.env)
    env["QT_QPA_PLATFORM"] = configuration.platform
    env["QT_QUICK_BACKEND"] = configuration.backend
    if configuration.rhi:
        env["QSG_RHI_BACKEND"] = configuration.rhi
    if qt_prefix:
        env["LD_LIBRARY_PATH"] = os.path.join(qt_prefix, "lib")
    return env


def tablet_devices():
    """Names of attached input devices that look like pen tablets."""
    try:
        with open("/proc/bus/input/devices") as devices:
            text = devices.read()
    except OSError:
        return []
    names = [line.split("=", 1)[1].strip().strip('"') for line in text.splitlines() if line.startswith("N: Name=")]
    keys = ("pen", "stylus", "tablet", "wacom", "huion", "xp-pen", "digitizer")
    return [name for name in names if any(key in name.lower() for key in keys)]


def cups_state():
    """(scheduler running, printer queue names)."""
    if not shutil.which("lpstat"):
        return False, []
    env = clean_environment()
    running = subprocess.run(["lpstat", "-r"], capture_output=True, text=True, env=env)
    if "is running" not in running.stdout or "not" in running.stdout:
        return False, []
    queues = subprocess.run(["lpstat", "-e"], capture_output=True, text=True, env=env)
    return True, [line.strip() for line in queues.stdout.splitlines() if line.strip()]
