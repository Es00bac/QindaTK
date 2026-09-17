#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "QindaTK::qindatk" for configuration "Release"
set_property(TARGET QindaTK::qindatk APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(QindaTK::qindatk PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE "Qt6::Svg"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib64/libqindatk.so.0.1.0"
  IMPORTED_SONAME_RELEASE "libqindatk.so.0"
  )

list(APPEND _cmake_import_check_targets QindaTK::qindatk )
list(APPEND _cmake_import_check_files_for_QindaTK::qindatk "${_IMPORT_PREFIX}/lib64/libqindatk.so.0.1.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
