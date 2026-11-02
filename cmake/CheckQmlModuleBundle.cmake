# SPDX-License-Identifier: LGPL-3.0-or-later
# Usage: cmake -DMODULE_DIR=<dir holding qmldir> [-DANDROID_BUNDLE=ON] -P CheckQmlModuleBundle.cmake
#
# AGENT-CONTRACT (D-355, docs/android.md): every file the qmldir names must
# exist in MODULE_DIR (that directory is what `cmake --install` stages). With
# ANDROID_BUNDLE, a script (.js/.mjs) entry is also an error: androiddeployqt's
# android_rcc_bundle holds only qmldir and typeinfo, and the QML engine looks a
# script up there, so the module would fail to import on the device.
if(NOT EXISTS "${MODULE_DIR}/qmldir")
    message(FATAL_ERROR "qmldir check: ${MODULE_DIR}/qmldir does not exist")
endif()
file(STRINGS "${MODULE_DIR}/qmldir" lines)
set(problems "")
foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    string(REGEX REPLACE "[ \t]+" ";" words "${line}")
    list(GET words 0 keyword)
    set(file "")
    if(keyword STREQUAL "typeinfo")
        list(GET words 1 file)
    elseif(keyword MATCHES "^(module|linktarget|optional|classname|designersupported|depends|import|prefer|plugin)$")
        continue()
    else()
        # [singleton|internal] Name Version File
        list(LENGTH words count)
        math(EXPR last "${count} - 1")
        list(GET words ${last} file)
    endif()
    if(file STREQUAL "")
        continue()
    endif()
    if(NOT EXISTS "${MODULE_DIR}/${file}")
        string(APPEND problems "\n  ${file}: named by qmldir but missing from ${MODULE_DIR}")
    elseif(ANDROID_BUNDLE AND file MATCHES "\\.m?js$")
        string(APPEND problems "\n  ${file}: a script entry in qmldir is unreachable from the Android bundle")
    endif()
endforeach()
if(problems)
    message(FATAL_ERROR "QindaTK qmldir check failed:${problems}")
endif()
message(STATUS "QindaTK qmldir check passed (${MODULE_DIR})")
