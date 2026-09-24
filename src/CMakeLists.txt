# SPDX-License-Identifier: LGPL-3.0-or-later

# AGENT-CONTRACT: one QML module, `QindaTK`, backed by the shared library
# `qindatk` (C++ types: layout engines, Theme, Density, Icon, DockModel) and
# the QML files under qml/. QML files are globbed so adding a control is
# "drop a .qml file here and re-run cmake"; the type name is the file name.
# Applications link QindaTK::qindatk only when they use the C++ API
# (DockModel from C++, Theme::instance()); QML-only users need the plugin
# on the import path and nothing else.

file(GLOB qindatk_qml_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
file(GLOB qindatk_js_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js")

# Singleton QML types are marked here (pragma Singleton alone is not enough
# for a compiled module), and every file is published flat (Box.qml, not
# qml/Box.qml) so the installed module reads like any other Qt module.
foreach(qml_file IN LISTS qindatk_qml_files qindatk_js_files)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${qml_file}" singleton_pragma REGEX "^pragma Singleton")
    if(singleton_pragma)
        set_source_files_properties("${qml_file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

qt_add_library(qindatk SHARED)
set_target_properties(qindatk PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME qindatk)
qindatk_enable_warnings(qindatk)

qt_add_qml_module(qindatk
    URI QindaTK
    VERSION 1.0
    CLASS_NAME QindaTKPlugin
    PLUGIN_TARGET qindatkplugin
    DEPENDENCIES QtQuick QtQuick.Controls QtQuick.Templates
    SOURCES
        core/application_contracts.cpp core/application_contracts.h
        core/color_profile.cpp core/color_profile.h
        core/color_context.h core/color_engine.cpp core/color_engine.h
        core/color_picker_model.cpp core/color_picker_model.h
        core/color_lcms_p.h
        core/curve_evaluator.cpp core/curve_evaluator.h
        core/curve_validation.cpp core/curve_evaluator_p.h
        core/curve_editor_model.cpp core/curve_editor_model.h
        core/gradient_evaluator.cpp core/gradient_evaluator.h
        core/gradient_editor_model.cpp core/gradient_editor_model.h
        core/capability_registry.cpp core/capability_registry.h
        core/command_context.cpp core/command_context.h
        core/command_registry.cpp core/command_registry.h
        core/edit_transaction.cpp core/edit_transaction.h
        core/document_model.h
        core/document_codec.cpp core/document_codec.h
        core/document_store.cpp core/document_store.h
        core/document_session.cpp core/document_session.h
        core/document_manager.cpp core/document_manager.h
        core/settings_model.cpp core/settings_model.h
        core/state_store.cpp core/state_store.h
        core/settings_persistence.cpp core/settings_persistence.h
        core/workspace_session.cpp core/workspace_session.h
        core/job_model.cpp core/job_model.h
        core/notification_model.cpp core/notification_model.h
        core/expression_evaluator.cpp core/expression_evaluator.h
        core/find_replace_service.cpp core/find_replace_service.h
        core/diff_model.cpp core/diff_model.h
        core/virtual_collection_adapter.cpp core/virtual_collection_adapter.h
        core/event_log_model.cpp core/event_log_model.h
        core/transport_model.cpp core/transport_model.h
        core/test_transport_clock.cpp core/test_transport_clock.h
        core/uri_validator.cpp core/uri_validator.h
        core/token_editor_service.cpp core/token_editor_service.h
        core/resource_resolver.cpp core/resource_resolver.h
        core/resource_resolver_async.cpp
        core/document_resource_links.cpp core/document_resource_links.h
        core/transfer_payload.cpp core/transfer_payload.h
        core/gesture_router.cpp core/gesture_router.h
        core/time_map.cpp core/time_map.h
        core/timeline_time.cpp core/timeline_time.h core/timeline_types.h
        core/timeline_policy.cpp core/timeline_policy.h
        core/timeline_codec.cpp core/timeline_codec.h
        core/timeline_snap.cpp core/timeline_snap.h
        core/timeline_planner.cpp core/timeline_planner_misc.cpp core/timeline_planner_ripple.cpp
        core/timeline_planner.h
        core/timeline_model.cpp core/timeline_model_edit.cpp core/timeline_model_history.cpp
        core/timeline_model_io.cpp core/timeline_model_qml.cpp core/timeline_model.h
        core/timeline_visible_list.cpp core/timeline_visible_list.h
        core/timeline_viewport.cpp core/timeline_viewport_rows.cpp core/timeline_viewport.h
        core/timeline_media_provider.cpp core/timeline_media_provider.h
        core/timeline_transfer.cpp core/timeline_transfer.h
        core/clipboard_service.cpp core/clipboard_service.h core/clipboard_ranges.cpp
        core/drag_drop_service.cpp core/drag_drop_service.h
        core/property_collection_drop_target.cpp
        core/locale_service.cpp core/locale_service.h
        core/font_catalog.cpp core/font_catalog.h
        core/accessibility_adapter.cpp core/accessibility_adapter.h
        core/window_host.cpp core/window_host.h
        core/detached_panel_host.cpp core/detached_panel_host.h
        core/file_dialog_adapter.cpp core/file_dialog_adapter.h
        core/desktop_integration.cpp core/desktop_integration.h
        core/format_registry.cpp core/format_registry.h
        core/format_io.cpp core/format_io.h
        core/native_reference_format.cpp
        core/plain_text_format.cpp
        core/asset_provider.cpp core/asset_provider.h
        core/file_asset_backend.cpp
        core/recovery_journal.cpp core/recovery_journal.h
        core/recovery_manager.cpp core/recovery_manager.h
        core/property_document.cpp core/property_document.h
        core/reference_document_codec.cpp core/reference_document_codec.h
        core/rich_text_types.cpp core/rich_text_types.h core/rich_text_spans.h
        core/rich_text_document.cpp core/rich_text_document.h
        core/rich_text_edit.cpp core/rich_text_history.cpp
        core/rich_text_ops.cpp core/rich_text_ops_p.h
        core/rich_text_structure.cpp core/rich_text_fragment.cpp
        core/rich_text_codec.cpp core/rich_text_codec_read.cpp
        core/rich_text_codec.h core/rich_text_codec_p.h
        core/rich_text_layout.h core/rich_text_layout_p.h core/rich_text_shaping_p.h
        core/rich_text_shaping.cpp core/rich_text_pagination.cpp core/rich_text_table_flow.cpp
        core/rich_text_pages.cpp core/rich_text_mapping.cpp core/rich_text_navigation.cpp
        core/rich_text_painter.cpp core/rich_text_objects.cpp core/rich_text_objects.h
        core/rich_text_graphemes.cpp core/rich_text_graphemes.h
        core/rich_text_area.cpp core/rich_text_area.h core/rich_text_area_input.cpp
        core/rich_text_area_edit.cpp core/rich_text_area_commands.cpp
        core/rich_text_format_state.cpp core/rich_text_format_state.h
        core/rich_text_page_model.cpp core/rich_text_page_model.h core/rich_text_visible_pages.h
        core/rich_text_tile.cpp core/rich_text_tile.h
        core/rich_text_clipboard.cpp core/rich_text_clipboard.h
        core/rich_text_html.cpp core/rich_text_html.h
        core/rich_text_session_adapter.cpp core/rich_text_session_adapter.h
        core/property_binding.cpp core/property_binding.h
        core/undo_service.cpp core/undo_service.h
        core/undo_history.cpp
        core/validation_model.cpp core/validation_model.h
        core/selection_model.cpp core/selection_model.h
        core/editable_grid_model.cpp core/editable_grid_model.h
        core/editable_grid_structure.cpp
        core/editable_grid_source.cpp
        core/grid_axis_ids.cpp core/grid_axis_ids.h
        core/resource_plan_model.cpp core/resource_plan_model.h
        core/editable_grid_mode.cpp
        core/workbook_value.cpp core/workbook_value.h core/workbook_formula.h
        core/workbook_parser.cpp core/workbook_refs.cpp core/workbook_eval.cpp
        core/workbook_lexer.cpp core/workbook_lexer_p.h core/workbook_functions.cpp
        core/workbook_numfmt.cpp core/workbook_numfmt.h
        core/workbook_features.cpp core/workbook_features_p.h
        core/workbook_rules.cpp core/workbook_rules_p.h
        core/workbook_styles.cpp core/workbook_validation.cpp
        core/workbook_sheetops.cpp core/workbook_ruleops.cpp
        core/workbook_codec_features.cpp core/workbook_transfer.cpp
        core/workbook_delimited.cpp core/workbook_delimited.h
        core/workbook_session.cpp core/workbook_session.h
        core/workbook.cpp core/workbook.h core/workbook_p.h
        core/workbook_recalc.cpp core/workbook_mutations.cpp core/workbook_ops.cpp
        core/workbook_codec.cpp core/workbook_codec_p.h
        core/workbook_persistence.cpp core/workbook_format.h
        core/workbook_sheet_adapter.cpp core/workbook_sheet_adapter.h
        core/query_spec.cpp core/query_eval.cpp core/query_pivot.cpp core/query_core.h
        core/query_detail.cpp core/query_join.cpp core/query_persistence.cpp
        core/query_grid_adapter.cpp core/query_grid_adapter.h
        core/query_controller.cpp core/query_controller.h
        core/shortcut_map.cpp core/shortcut_map_metadata.cpp core/shortcut_map.h
        core/density.cpp core/density.h
        core/focus_manager.cpp core/focus_manager.h
        core/dock_model.cpp core/dock_model.h core/dock_model_p.h
        core/dock_model_ops.cpp
        core/dock_model_store.cpp
        core/flex.cpp core/flex.h
        core/grid.cpp core/grid.h
        core/chart_axis.cpp core/chart_axis.h
        core/chart_series.cpp core/chart_series.h
        core/chart_data.cpp core/chart_data.h
        core/chart_dataset.cpp core/chart_dataset.h
        core/chart_renderer.cpp core/chart_renderer.h
        core/chart_renderer_annotations.cpp
        core/chart_renderer_data.cpp
        core/chart_annotation_model.cpp core/chart_annotation_model.h
        core/chart_histogram_selection.cpp core/chart_histogram_selection.h
        core/chart_table_model.cpp core/chart_table_model.h
        core/chart_histogram.cpp core/chart_histogram.h
        core/distribution_model.cpp core/distribution_model.h
        core/gantt_calendar.cpp core/gantt_calendar.h
        core/gantt_task_model.cpp core/gantt_task_model.h
        core/heatmap_model.cpp core/heatmap_model.h
        core/heatmap_renderer.cpp core/heatmap_renderer.h
        core/ohlc_model.cpp core/ohlc_model.h
        core/part_whole_model.cpp core/part_whole_model.h
        core/part_whole_layout.cpp core/part_whole_layout.h
        core/part_whole_renderer.cpp core/part_whole_renderer.h
        core/polar_transform.cpp core/polar_transform.h
        core/polar_data_model.cpp core/polar_data_model.h
        core/polar_renderer.cpp core/polar_renderer_paint.cpp core/polar_renderer.h
        core/target_model.cpp core/target_model.h
        core/uncertainty_model.cpp core/uncertainty_model.h
        core/uncertainty_renderer.cpp core/uncertainty_renderer.h
        core/graph.cpp core/graph.h
        core/graph_mesh.cpp core/graph_mesh.h
        core/graph_nodes.cpp
        core/graph_raster.cpp core/graph_raster.h
        core/grid_tracks.cpp core/grid_tracks.h
        core/icon.cpp core/icon.h
        core/icon_data.cpp core/icon_data.h
        core/meter.cpp core/meter.h
        core/layout_attached.cpp core/layout_attached.h
        core/layout_info.cpp core/layout_info.h
        core/screen_work_area.cpp core/screen_work_area.h
        core/stack.cpp core/stack.h
        core/stylus_handler.cpp core/stylus_handler.h
        core/canvas_transform.cpp core/canvas_transform.h
        core/canvas_object.h
        core/canvas_geometry.cpp core/canvas_geometry.h
        core/canvas_document.cpp core/canvas_document.h
        core/canvas_document_state.h
        core/canvas_document_edit.cpp core/canvas_document_p.h
        core/canvas_document_format.cpp core/canvas_document_format.h
        core/canvas_document_codec.cpp
        core/canvas_document_qml.cpp
        core/canvas_native_format.cpp
        core/hit_test_service.cpp core/hit_test_service.h
        core/tool_controller.cpp core/tool_controller.h
        core/canvas_selection_model.cpp core/canvas_selection_model.h
        core/canvas_alignment_model.cpp core/canvas_alignment_model.h
        core/canvas_tools.cpp
        core/canvas_render.h
        core/canvas_painter.cpp
        core/canvas_tessellator.cpp
        core/render_surface_adapter.cpp core/render_surface_adapter.h
        core/canvas_view_state.cpp core/canvas_view_state.h
        core/canvas_snap_model.cpp core/canvas_snap_model.h
        core/canvas_transform_controller.cpp core/canvas_transform_controller.h
        core/canvas_surface.cpp core/canvas_surface.h
        core/canvas_surface_input.cpp
        core/canvas_surface_nodes.cpp core/canvas_surface_nodes.h
        core/canvas_overlay.cpp core/canvas_overlay.h
        core/svg_path.cpp core/svg_path.h
        core/theme.cpp core/theme.h
        core/theme_presets.cpp core/theme_presets.h
        core/theme_ramp.cpp core/theme_ramp.h
    QML_FILES ${qindatk_qml_files} ${qindatk_js_files}
)


target_include_directories(qindatk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(qindatk
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE Qt6::Svg Qt6::Concurrent PkgConfig::LCMS2)
# Qt Quick's private tablet-event hook is confined to stylus_handler.cpp.
target_link_libraries(qindatk PRIVATE Qt6::QuickPrivate)

if(TARGET Qt6::Widgets)
    add_library(qindatk_widgets STATIC
        platform_widgets/native_file_dialog_backend.cpp
        platform_widgets/native_file_dialog_backend.h)
    set_target_properties(qindatk_widgets PROPERTIES EXPORT_NAME qindatk_widgets)
    qindatk_enable_warnings(qindatk_widgets)
    target_include_directories(qindatk_widgets PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/platform_widgets>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindatk>)
    target_link_libraries(qindatk_widgets PUBLIC qindatk Qt6::Widgets)
    install(TARGETS qindatk_widgets EXPORT QindaTKWidgetsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT QindaTKWidgetsTargets NAMESPACE QindaTK::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QindaTKWidgets)
    install(FILES platform_widgets/native_file_dialog_backend.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindatk)
    set(QINDATK_WITH_WIDGETS TRUE PARENT_SCOPE)
endif()

# ---- optional bridge to the QindaQt desktop's QST-1 tokens (pure QML) ----
# Importing QindaTK.QindaQt requires QindaQt.Tokens at run time; the module
# itself builds everywhere so a package can ship it unconditionally.
set_source_files_properties(bridge/QindaQtTheme.qml PROPERTIES QT_RESOURCE_ALIAS QindaQtTheme.qml)
qt_add_library(qindatk_qindaqt SHARED)
qt_add_qml_module(qindatk_qindaqt
    URI QindaTK.QindaQt
    VERSION 1.0
    CLASS_NAME QindaTKQindaQtPlugin
    PLUGIN_TARGET qindatk_qindaqtplugin
    QML_FILES bridge/QindaQtTheme.qml
)
target_link_libraries(qindatk_qindaqt PRIVATE Qt6::Qml)

# ---- install -------------------------------------------------------------
if(NOT DEFINED QINDATK_QML_INSTALL_DIR)
    set(QINDATK_QML_INSTALL_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}" CACHE PATH
        "Root QML import directory to install the QindaTK modules into")
endif()
set(QINDATK_QML_INSTALL_DIR "${QINDATK_QML_INSTALL_DIR}" PARENT_SCOPE)

install(TARGETS qindatk EXPORT QindaTKTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS qindatkplugin
    LIBRARY DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK"
    RUNTIME DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK")
install(TARGETS qindatk_qindaqt qindatk_qindaqtplugin
    LIBRARY DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK/QindaQt"
    RUNTIME DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK/QindaQt")
install(DIRECTORY "${CMAKE_BINARY_DIR}/qml/QindaTK/"
    DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK"
    FILES_MATCHING PATTERN "qmldir" PATTERN "*.qmltypes" PATTERN "*.qml" PATTERN "*.js")
install(DIRECTORY "${CMAKE_BINARY_DIR}/qml/QindaTK/QindaQt/"
    DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK/QindaQt"
    FILES_MATCHING PATTERN "qmldir" PATTERN "*.qmltypes" PATTERN "*.qml")
install(DIRECTORY core/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/qindatk"
    FILES_MATCHING PATTERN "*.h" PATTERN "*_p.h" EXCLUDE)
install(EXPORT QindaTKTargets NAMESPACE QindaTK::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QindaTK")
