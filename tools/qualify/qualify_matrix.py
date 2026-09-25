# SPDX-License-Identifier: LGPL-3.0-or-later
"""What WP-088 qualifies on Linux: Qt installs, build variants, runtime
configurations and operations (docs/qualification.md).

AGENT-NOTE: this file is the single list of claims. A configuration or
operation that is added here is run by qualify.py and appears in the result
table; nothing else decides what "qualified" covers.
"""
import os
from dataclasses import dataclass, field

HOME = os.path.expanduser("~")

# Qt installs. The 6.8 SDK is an isolated aqtinstall tree (docs/platform-support.md).
QT_INSTALLS = {
    "6.11": {"key": "qt611", "prefix": None},
    "6.8": {"key": "qt68", "prefix": os.environ.get(
        "QINDATK_QT68_PREFIX",
        os.path.join(HOME, "work_SPaC3/QindaTK-universal/build/qt68-sdk/6.8.3/gcc_64"))},
}

# Build variants: each optional engine omitted on its own, and all at once.
# Option names are the top-level CMakeLists.txt switches (D-201).
OPTIONAL_SWITCHES = {
    "poppler": "QINDATK_ENABLE_POPPLER_QT6",
    "alsa": "QINDATK_ENABLE_ALSA",
    "ffmpeg": "QINDATK_ENABLE_FFMPEG",
    "quick3d": "QINDATK_ENABLE_QUICK3D",
    "widgets": "QINDATK_ENABLE_WIDGETS",
    "nativePrint": "QINDATK_ENABLE_NATIVE_PRINT",
    "shaderTools": "QINDATK_ENABLE_SHADER_TOOLS",
}
VARIANTS = {"full": {}}
VARIANTS.update({"no-" + name.lower(): {option: "OFF"} for name, option in OPTIONAL_SWITCHES.items()})
VARIANTS["minimal"] = {option: "OFF" for option in OPTIONAL_SWITCHES.values()}

# Which consumer capability (tests/consumer/qualify) each resolved feature
# (build/<variant>/qindatk-features.json) must switch on.
FEATURE_CAPABILITIES = {
    "ffmpeg": "media.ffmpeg",
    "alsa": "audio.alsa",
    "poppler": "pdf.poppler",
    "shaderTools": "volume.gpuShaders",
    "quick3d": "scene3d.quick3d",
    "widgets": "dialogs.native",
    "nativePrint": "print.native",
}

# Targets a variant builds. Matrix variants add the test binaries their
# operations run; the others build only what install and the consumer need.
LIBRARY_TARGETS = ["qindatk", "qindatkplugin", "qindatk_qindaqt", "qindatk_qindaqtplugin",
                   "qindatk_widgets", "qtk-preview"]


@dataclass
class Configuration:
    name: str
    platform: str            # QT_QPA_PLATFORM
    backend: str             # QT_QUICK_BACKEND: software | rhi
    display: str             # none | weston-pixman | weston-gl | xwayland
    expect_api: str          # software | opengl | vulkan | rhi (any RHI API)
    rhi: str = ""            # QSG_RHI_BACKEND when backend == rhi

    @property
    def gpu(self):
        return self.backend == "rhi"


CONFIGURATIONS = [
    Configuration("offscreen-software", "offscreen", "software", "none", "software"),
    Configuration("offscreen-rhi", "offscreen", "rhi", "none", "rhi"),
    Configuration("wayland-software", "wayland", "software", "weston-pixman", "software"),
    Configuration("wayland-opengl", "wayland", "rhi", "weston-gl", "opengl", "opengl"),
    Configuration("wayland-vulkan", "wayland", "rhi", "weston-gl", "vulkan", "vulkan"),
    Configuration("xcb-software", "xcb", "software", "xwayland", "software"),
    Configuration("xcb-opengl", "xcb", "rhi", "xwayland", "opengl", "opengl"),
]


@dataclass
class TestRun:
    binary: str
    functions: list = field(default_factory=list)   # empty: every function
    renderer: str = "any"        # any | software | gpu: the configurations it applies to
    # The test waits for an *active* window. A compositor without a seat
    # (headless Weston) never activates one; the run is then recorded as
    # unqualified instead of failing (the render probe reports activation).
    needs_activation: bool = False


@dataclass
class Operation:
    name: str
    title: str
    kind: str = "qtest"          # qtest | render | atspi | cups | tablet | plugins
    tests: list = field(default_factory=list)
    per_configuration: bool = True   # False: platform-independent, run once per build
    missing: str = ""                # reason when a test target is absent from the build


OPERATIONS = [
    Operation("render", "Scene graph renders on the claimed platform and graphics API", kind="render"),
    Operation("native-windows", "Native top-level windows and detached panels (WP-080)",
              tests=[TestRun("tst_detached_panel_host"), TestRun("tst_window_host")]),
    Operation("file-dialogs", "Native file dialog adapter (WP-018)",
              tests=[TestRun("tst_native_file_dialog")],
              missing="QindaTK built without Qt Widgets (QINDATK_ENABLE_WIDGETS)"),
    Operation("print-pdf", "Print to PDF and the native print engine's file destination (WP-053)",
              tests=[TestRun("tst_print_output"), TestRun("tst_native_print")],
              missing="QindaTK built without Qt PrintSupport (QINDATK_ENABLE_NATIVE_PRINT)"),
    Operation("print-cups", "CUPS printer destinations (WP-053)", kind="cups",
              tests=[TestRun("tst_native_print", ["destinationsListPrintersAndTheFileEngine"])],
              per_configuration=False,
              missing="QindaTK built without Qt PrintSupport (QINDATK_ENABLE_NATIVE_PRINT)"),
    Operation("input", "Pointer, touch, wheel, gesture and synthetic tablet event paths",
              tests=[TestRun("tst_stylus_handler"), TestRun("tst_raster_stroke_input"),
                     TestRun("tst_canvas_input"), TestRun("tst_gesture_router")]),
    Operation("input-tablet-hardware", "A physical pen tablet", kind="tablet", per_configuration=False),
    Operation("accessibility", "In-process accessibility interfaces (QAccessible)",
              tests=[TestRun("tst_accessibility_adapter"),
                     TestRun("tst_sheet_accessibility", needs_activation=True)]),
    Operation("atspi", "External AT-SPI client reads and presses controls (silent)", kind="atspi"),
    Operation("media-devices", "Audio devices: built-in null/loopback and ALSA null/file PCMs (WP-060)",
              tests=[TestRun("tst_audio_engine", ["loopbackRecordingIsLatencyAlignedAndLoops",
                                                  "deviceLossDuringRecordingKeepsTake",
                                                  "backendsReportOptionalAlsaWithAReason",
                                                  "alsaFixturePcmsCarryExactSamples"])],
              per_configuration=False),
    Operation("plugin-editors", "Audio plugin hosting and editor windows (WP-066)", kind="plugins"),
    Operation("render-context-loss", "Scene-graph invalidation and GPU resource recovery (WP-040, WP-076)",
              tests=[TestRun("tst_canvas_render", ["contextLossAndBackendFallback"], renderer="software"),
                     TestRun("tst_canvas_render", ["sceneGraphBackend"], renderer="gpu"),
                     TestRun("tst_canvas_render_lifecycle"),
                     TestRun("tst_scene_view_3d_render", renderer="gpu")]),
]


def matrix_test_targets():
    names = []
    for operation in OPERATIONS:
        for run in operation.tests:
            if run.binary not in names:
                names.append(run.binary)
    return names


def find(items, name):
    for item in items:
        if item.name == name:
            return item
    raise KeyError(name)
