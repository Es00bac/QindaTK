# SPDX-License-Identifier: LGPL-3.0-or-later
# Independent of Quick: native content engines can expose the same contract.
add_library(qindatk_agent STATIC agent_provider.h agent_provider.cpp
    agent_host.h agent_host.cpp agent_schema.h agent_schema.cpp
    agent_server.h agent_server.cpp agent_service.h agent_service.cpp
    scoped_agent_types.h scoped_agent_policy.h scoped_agent_policy.cpp
    scoped_agent_host.h scoped_agent_host.cpp)
add_library(QindaTK::agent ALIAS qindatk_agent)
set_target_properties(qindatk_agent PROPERTIES EXPORT_NAME agent POSITION_INDEPENDENT_CODE ON)
target_link_libraries(qindatk_agent PUBLIC Qt6::Core Qt6::Network)
# Scoped value equality is part of the installed C++20 public headers.
target_compile_features(qindatk_agent PUBLIC cxx_std_20)
target_include_directories(qindatk_agent PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindatk>)
install(TARGETS qindatk_agent EXPORT QindaTKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES agent_provider.h agent_host.h agent_schema.h agent_server.h agent_service.h
    scoped_agent_types.h scoped_agent_policy.h scoped_agent_host.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindatk)
