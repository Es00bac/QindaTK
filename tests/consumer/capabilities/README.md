<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed capability consumer fixture

This separate CMake project verifies the installed public consumer boundary.
It finds the installed `QindaTK` package, links its executable only to
`QindaTK::qindatk`, imports `QindaTK as Tk`, and creates `Tk.CapabilityBinding`
against an application-owned C++ `CapabilityRegistry`. It checks the initial
unavailable reason, then publishes one backend operation and checks the live
availability, backend ID and version. Publishing metadata is sufficient; the
fixture has no optional engine target or engine loader.

The same executable exercises the installed `SnapshotHeader` and `ExactTime`
decimal-string codec, including a revision/reset generation above 2^53.

Configure against an installed prefix, then build and run:

```sh
cmake -S tests/consumer/capabilities -B build/consumer-capabilities -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-prefix -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-capabilities
QT_QPA_PLATFORM=offscreen build/consumer-capabilities/qindatk_capabilities_consumer
```

The direct build inherits the system's configured parallel and load limits.
Do not set command-local `MAKEOPTS` or reduce build parallelism.
