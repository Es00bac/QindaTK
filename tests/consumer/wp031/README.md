<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed WP-031 analytical views consumer

This standalone consumer compiles against the staged public C++ models,
imports all eight QML views across WP-031's seven families, and checks known
source values and live model bindings. It covers the installed module/API
boundary; the package's focused tests cover rendering, interactions and
exports in depth.

Configure against a staged QindaTK prefix, build with configured system
`MAKEOPTS` unchanged, and run headless:

```sh
cmake -S tests/consumer/wp031 -B build/consumer-wp031 -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-stage -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-wp031
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/consumer-wp031/qindatk_wp031_consumer
```

At runtime, include the staged QindaTK library directory in
`LD_LIBRARY_PATH`. When using a separate Qt SDK, include its library directory
and set `QT_PLUGIN_PATH` to that SDK's plugins so the Qt minor versions match.
