# SPDX-License-Identifier: LGPL-3.0-or-later
qt_add_executable(qtk-preview main.cpp qst_support.cpp qst_support.h)
qindatk_enable_warnings(qtk-preview)
target_link_libraries(qtk-preview PRIVATE qindatk Qt6::Quick Qt6::QuickControls2)
# AGENT-CONTRACT (D-202, main.cpp defaultImportRoot()): the tool finds the
# QindaTK module relative to its own executable, from the build tree or from
# an installed (and possibly moved) prefix. No absolute source, build or
# install path is compiled in; tools/qualify checks the installed binary.
file(RELATIVE_PATH qtk_preview_build_qml "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_BINARY_DIR}/qml")
cmake_path(ABSOLUTE_PATH QINDATK_QML_INSTALL_DIR BASE_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
    NORMALIZE OUTPUT_VARIABLE qtk_preview_abs_qml)
cmake_path(ABSOLUTE_PATH CMAKE_INSTALL_BINDIR BASE_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
    NORMALIZE OUTPUT_VARIABLE qtk_preview_abs_bin)
file(RELATIVE_PATH qtk_preview_install_qml "${qtk_preview_abs_bin}" "${qtk_preview_abs_qml}")
target_compile_definitions(qtk-preview PRIVATE
    QINDATK_PREVIEW_BUILD_QML_REL="${qtk_preview_build_qml}"
    QINDATK_PREVIEW_INSTALL_QML_REL="${qtk_preview_install_qml}"
    QINDATK_VERSION="${PROJECT_VERSION}")

# AGENT-CONTRACT: optional QindaQt desktop integration (`--qst <theme>`).
# The desktop (gui-wm/qindaqt-desktop) installs headers and static archives
# without CMake package configs, so they are resolved by name exactly as
# QindaStudio does. Absent libraries only disable the option.
option(QINDATK_PREVIEW_QINDAQT "Let qtk-preview publish QindaQt desktop themes (--qst)" ON)
if(QINDATK_PREVIEW_QINDAQT)
    find_path(QINDAQT_INCLUDE_DIR qindaqt/design_tokens/token_facade.h)
    find_library(QINDAQT_TOKENS_QML_LIB NAMES qindaqt_tokens_qml
        PATHS /usr/lib64/qt6/qml/QindaQt/Tokens /usr/lib/qt6/qml/QindaQt/Tokens)
    find_library(QINDAQT_DESIGN_TOKENS_LIB NAMES qindaqt_design_tokens)
    find_library(QINDAQT_THEMES_LIB NAMES qindaqt_themes)
    # AGENT-GUARD: an SDK-selected Qt must not link desktop archives built
    # against the system Qt. Qt 6.8 + a host Qt 6.11 Tokens library fails
    # with unresolved Qt 6.11 symbols. Match the selected Qt install prefix.
    get_filename_component(qindatk_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    if(QINDAQT_TOKENS_QML_LIB)
        cmake_path(IS_PREFIX qindatk_qt_prefix "${QINDAQT_TOKENS_QML_LIB}"
            NORMALIZE qindatk_qindaqt_same_prefix)
    endif()
    if(QINDAQT_INCLUDE_DIR AND QINDAQT_TOKENS_QML_LIB AND QINDAQT_DESIGN_TOKENS_LIB AND QINDAQT_THEMES_LIB
       AND qindatk_qindaqt_same_prefix)
        message(STATUS "qtk-preview: QindaQt desktop themes enabled (--qst)")
        target_compile_definitions(qtk-preview PRIVATE QINDATK_HAVE_QINDAQT)
        target_include_directories(qtk-preview PRIVATE ${QINDAQT_INCLUDE_DIR})
        target_link_libraries(qtk-preview PRIVATE
            ${QINDAQT_TOKENS_QML_LIB}
            ${QINDAQT_DESIGN_TOKENS_LIB}
            ${QINDAQT_THEMES_LIB})
        get_filename_component(qindaqt_tokens_dir "${QINDAQT_TOKENS_QML_LIB}" DIRECTORY)
        # Append: replacing INSTALL_RPATH dropped Qt's $ORIGIN/../lib entry,
        # so an installed qtk-preview outside the default linker path could
        # not find libqindatk (found by the WP-088 relocation check).
        set_target_properties(qtk-preview PROPERTIES BUILD_RPATH "${qindaqt_tokens_dir}"
            INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${qindaqt_tokens_dir}")
    elseif(QINDAQT_TOKENS_QML_LIB AND NOT qindatk_qindaqt_same_prefix)
        message(STATUS "qtk-preview: QindaQt desktop library is outside the selected Qt prefix; --qst disabled")
    else()
        message(STATUS "qtk-preview: QindaQt desktop libraries not found; --qst disabled")
    endif()
endif()

install(TARGETS qtk-preview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
