<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed workbook consumer

This standalone application checks the installed package for WP-055. Its
window creates a workbook through a `DocumentManager` session and imports
`WorkbookTabs`, `FormulaBar`, `SheetView`, `ConditionalFormatEditor` and
`DataValidationEditor` through `QindaTK as Tk`, with `FormatRegistry` and a
second standalone workbook. `main.cpp` compiles against the installed
`workbook*.h`, `document_manager.h`, `undo_service.h` and `format_registry.h`
headers and checks, with one exit code per step:

1. every view instantiates;
2. formulas, a whole-column `COUNTA(A:A)`, the name `Amounts` and a
   `#,##0.00` number format give the known results;
3. the installed SheetView renders cells through the installed adapter, and
   the FormulaBar shows the active cell's formula and formatted result;
4. a conditional format and a stop validation rule apply, and a
   `skipInvalid` paste reports one invalid cell;
5. a copied range pastes into a second workbook with moved references and
   its formats, and undoes in one step;
6. a CSV export runs through the registry with the Workbook as a snapshot
   source;
7. the session's `UndoService` records and replays the workbook's steps;
8. the `DocumentSession` saves on a worker, and a second `DocumentManager`
   reopens the file as a clean session with identical content.

Stage an install the way the integration stages are made: the build's
install prefix and QML directory both point into the stage. The package
config records the QML directory as given, so a `DESTDIR` install of the
default layout would send the consumer to a system-installed module. Then
build and run the consumer against the stage:

```sh
cmake -S . -B build/wp055 -DCMAKE_INSTALL_PREFIX=$PWD/build/wp055-stage \
  -DQINDATK_QML_INSTALL_DIR=$PWD/build/wp055-stage/qml
cmake --build build/wp055 --parallel 4
cmake --install build/wp055
cmake -S tests/consumer/workbook -B build/consumer-workbook -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$PWD/build/wp055-stage
cmake --build build/consumer-workbook --parallel 4
LD_LIBRARY_PATH=$PWD/build/wp055-stage/lib64 QML_IMPORT_TRACE=1 \
  QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/consumer-workbook/qindatk_workbook_consumer
```

For a Qt SDK outside the system library path (the Qt 6.8 check), add the
SDK to `CMAKE_PREFIX_PATH` when configuring the consumer and its `lib`
directory to `LD_LIBRARY_PATH`. Otherwise the installed library, which
carries no build RPATH, resolves Qt modules such as `Qt6Svg` from the
system Qt. Confirm with `ldd` and the import trace that the staged
`libqindatk` and `qml/QindaTK/qmldir` are the ones loaded. The four-job
limit reflects the concurrent-build rule in force for this run.
