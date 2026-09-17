# SPDX-License-Identifier: LGPL-3.0-or-later
qt_add_executable(qtk-preview main.cpp qst_support.cpp qst_support.h)
qindatk_enable_warnings(qtk-preview)
target_link_libraries(qtk-preview PRIVATE qindatk Qt6::Quick Qt6::QuickControls2)
target_compile_definitions(qtk-preview PRIVATE
    QINDATK_BUILD_QML_DIR="${CMAKE_BINARY_DIR}/qml"
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
    if(QINDAQT_INCLUDE_DIR AND QINDAQT_TOKENS_QML_LIB AND QINDAQT_DESIGN_TOKENS_LIB AND QINDAQT_THEMES_LIB)
        message(STATUS "qtk-preview: QindaQt desktop themes enabled (--qst)")
        target_compile_definitions(qtk-preview PRIVATE QINDATK_HAVE_QINDAQT)
        target_include_directories(qtk-preview PRIVATE ${QINDAQT_INCLUDE_DIR})
        target_link_libraries(qtk-preview PRIVATE
            ${QINDAQT_TOKENS_QML_LIB}
            ${QINDAQT_DESIGN_TOKENS_LIB}
            ${QINDAQT_THEMES_LIB})
        get_filename_component(qindaqt_tokens_dir "${QINDAQT_TOKENS_QML_LIB}" DIRECTORY)
        set_target_properties(qtk-preview PROPERTIES BUILD_RPATH "${qindaqt_tokens_dir}" INSTALL_RPATH "${qindaqt_tokens_dir}")
    else()
        message(STATUS "qtk-preview: QindaQt desktop libraries not found; --qst disabled")
    endif()
endif()

install(TARGETS qtk-preview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
