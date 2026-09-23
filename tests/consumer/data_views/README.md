<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed data views consumer

This standalone application checks the public installed package for WP-027,
WP-028 and WP-029. It imports the virtual collections, editable grid, filter,
query builder and pivot controls through `QindaTK as Tk`; it also compiles
against the installed grid and query controller headers. A two-row source grid
feeds an asynchronous aggregate, and the consumer checks the read-only result,
value and source-row drill-through by stable IDs.

Configure against a staged prefix, then build with the configured job and load
limits (do not override `MAKEOPTS`):

```sh
cmake -S tests/consumer/data_views -B build/consumer-data-views -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-prefix -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-data-views
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/consumer-data-views/qindatk_data_views_consumer
```
