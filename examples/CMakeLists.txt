# SPDX-License-Identifier: LGPL-3.0-or-later

# Examples are plain QML files run through qtk-preview, so they need no
# build step:  build/dev/tools/preview/qtk-preview examples/gallery/Gallery.qml
# Each one is also a headless smoke test: it must instantiate and dump.
if(QINDATK_BUILD_TOOLS AND BUILD_TESTING)
    file(GLOB example_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
    foreach(dir IN LISTS example_dirs)
        if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/Main.qml")
            get_filename_component(name "${dir}" NAME)
            # The workbench QML requires its local C++ DocumentManager host;
            # its standalone --check is the smoke gate for that example.
            if(name STREQUAL "workbench" OR name STREQUAL "render-queue-live")
                continue()
            endif()
            add_test(NAME example_${name}
                COMMAND qtk-preview "${dir}/Main.qml" --check --size 1280x800)
            set_tests_properties(example_${name} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        endif()
    endforeach()
    # The live plugin host example scans the test CLAP plugins (tests/plugins).
    set_tests_properties(example_plugin-host PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;CLAP_PATH=${CMAKE_BINARY_DIR}/tests/clap-fixtures")
    add_test(NAME example_document_lifecycle
        COMMAND qtk-preview "${CMAKE_CURRENT_SOURCE_DIR}/document-lifecycle.qml"
                --check --size 1120x720)
    set_tests_properties(example_document_lifecycle PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_test(NAME example_platform_services
        COMMAND qtk-preview "${CMAKE_CURRENT_SOURCE_DIR}/platform-services.qml"
                --check --size 920x620)
    set_tests_properties(example_platform_services PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_test(NAME example_color_picker_model
        COMMAND qtk-preview "${CMAKE_CURRENT_SOURCE_DIR}/spatial-canvas/ColorPickerScene.qml"
                --check --size 520x640)
    set_tests_properties(example_color_picker_model PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_test(NAME example_media_source_browser
        COMMAND qtk-preview "${CMAKE_CURRENT_SOURCE_DIR}/media-source-browser.qml"
                --check --size 1120x680)
    set_tests_properties(example_media_source_browser PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_test(NAME example_device_settings_panel
        COMMAND qtk-preview "${CMAKE_CURRENT_SOURCE_DIR}/device-settings-panel.qml"
                --check --size 760x640)
    set_tests_properties(example_device_settings_panel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_test(NAME example_mixer_routing
        COMMAND qtk-preview "${CMAKE_CURRENT_SOURCE_DIR}/mixer-routing.qml"
                --check --size 1280x740)
    set_tests_properties(example_mixer_routing PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    # qtk-preview must tear the scene down before the application: examples
    # such as sequence-edit-live still have media jobs running at --check.
    add_test(NAME preview_teardown_jobs
        COMMAND qtk-preview "${PROJECT_SOURCE_DIR}/tests/fixtures/preview_teardown_jobs.qml"
                --check --wait 300)
    set_tests_properties(preview_teardown_jobs PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
endif()

# Android: only the smoke app builds (docs/android.md). The render-queue demo host
# below links desktop-only services and is run through the desktop build.
if(ANDROID)
    add_subdirectory(android-smoke)
    return()
endif()

# Live render queue needs a host to prepare hashed source frames and route UI requests.
qt_add_executable(qtk-render-queue-live render-queue-live/main.cpp
    render-queue-live/demo_host.cpp render-queue-live/demo_host.h)
qindatk_enable_warnings(qtk-render-queue-live)
target_link_libraries(qtk-render-queue-live PRIVATE qindatk Qt6::Quick)
target_compile_definitions(qtk-render-queue-live PRIVATE
    RENDER_QUEUE_LIVE_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/render-queue-live/Main.qml"
    QINDATK_BUILD_QML_DIR="${CMAKE_BINARY_DIR}/qml")
if(BUILD_TESTING)
    add_test(NAME example_render_queue_live
        COMMAND qtk-render-queue-live --check --output-root "${CMAKE_BINARY_DIR}/render-queue-live-check")
    set_tests_properties(example_render_queue_live PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
