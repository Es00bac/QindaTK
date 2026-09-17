# SPDX-License-Identifier: LGPL-3.0-or-later

# Examples are plain QML files run through qtk-preview, so they need no
# build step:  build/dev/tools/preview/qtk-preview examples/gallery/Gallery.qml
# Each one is also a headless smoke test: it must instantiate and dump.
if(QINDATK_BUILD_TOOLS AND BUILD_TESTING)
    file(GLOB example_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
    foreach(dir IN LISTS example_dirs)
        if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/Main.qml")
            get_filename_component(name "${dir}" NAME)
            add_test(NAME example_${name}
                COMMAND qtk-preview "${dir}/Main.qml" --check --size 1280x800)
            set_tests_properties(example_${name} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        endif()
    endforeach()
endif()
