<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Qualification consumer (WP-088)

A lightweight application built only from an installed QindaTK prefix. It is
driven by `tools/qualify/qualify.py` ([docs/qualification.md](../../../docs/qualification.md)),
which installs it into the QindaTK stage, moves the stage, and runs it from
the moved location.

- `qtk_qualify_consumer` links `QindaTK::qindatk` (and `QindaTK::qindatk_widgets`
  when the prefix has it), imports `QindaTK as Tk` from the prefix, and prints a
  JSON report of the optional engines: FFmpeg media, ALSA devices, Poppler PDF
  annotations, GPU volume shaders, Qt Quick 3D, native file dialogs and native
  print. An omitted engine must report `available: false` with a reason; the
  exit status is 3 when one does not.
- `qtk_qualify_qml_only` links Qt Quick only. It proves the installed QML plugin
  finds `libqindatk` by itself.

Both find the import root and the library relative to their own executable;
nothing absolute is compiled in. By hand:

```sh
cmake -S tests/consumer/qualify -B build/qualify/consumer -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/stage -DCMAKE_INSTALL_PREFIX=/path/to/stage
cmake --build build/qualify/consumer
cmake --install build/qualify/consumer
QT_QPA_PLATFORM=offscreen /path/to/stage/bin/qtk_qualify_consumer
```
