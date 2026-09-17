
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was QindaTKConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)
# QuickPrivate: <qindatk/stylus_handler.h> derives from a private QtQuick class (D-013).
find_dependency(Qt6QuickPrivate)
find_dependency(Qt6 COMPONENTS Core Gui Qml Quick Svg)

include("${CMAKE_CURRENT_LIST_DIR}/QindaTKTargets.cmake")

# The QML module directory (contains qmldir, qmltypes and the plugin).
# Informational (plain set: a DESTDIR-staged install must still configure).
set(QindaTK_QML_DIR "${PACKAGE_PREFIX_DIR}/lib64/qt6/qml/QindaTK")

check_required_components(QindaTK)
