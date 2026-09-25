# SPDX-License-Identifier: LGPL-3.0-or-later

# AGENT-CONTRACT: one QML module, `QindaTK`, backed by the shared library
# `qindatk` (C++ types: layout engines, Theme, Density, Icon, DockModel) and
# the QML files under qml/. QML files are globbed so adding a control is
# "drop a .qml file here and re-run cmake"; the type name is the file name.
# Applications link QindaTK::qindatk only when they use the C++ API
# (DockModel from C++, Theme::instance()); QML-only users need the plugin
# on the import path and nothing else.

file(GLOB qindatk_qml_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
# Scene data stays renderer-independent. SDKs without Quick3D still expose
# SceneView3D, which reports its renderer unavailable through the lazy Loader.
if(QINDATK_ENABLE_QUICK3D)
    find_package(Qt6Quick3D 6.8 QUIET)
endif()
# AGENT-GUARD: test QINDATK_WITH_QUICK3D, never TARGET Qt6::Quick3D alone;
# another package may define the target while the option is OFF (D-201).
if(QINDATK_ENABLE_QUICK3D AND TARGET Qt6::Quick3D)
    set(QINDATK_WITH_QUICK3D TRUE)
else()
    set(QINDATK_WITH_QUICK3D FALSE)
endif()
set(QINDATK_WITH_QUICK3D ${QINDATK_WITH_QUICK3D} PARENT_SCOPE)
set(qindatk_quick3d_sources)
if(NOT QINDATK_WITH_QUICK3D)
    list(FILTER qindatk_qml_files EXCLUDE REGEX "(SceneView3DRenderer|MaterialPreviewRenderer|ScientificPlot3DRenderer)\\.qml$")
    message(STATUS "QindaTK: optional SceneView3D renderer unavailable (Qt Quick 3D not installed)")
else()
    list(APPEND qindatk_quick3d_sources
        core/mesh_rig_geometry.h core/mesh_rig_geometry.cpp
        core/scientific_geometry3d.h core/scientific_geometry3d.cpp)
endif()
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
        ${qindatk_quick3d_sources}
        core/code_document.cpp core/code_document.h
        core/code_language_service.cpp core/code_language_service.h
        core/geographic_feature_model.h core/geographic_feature_model.cpp
        core/geographic_feature_edit.cpp
        core/geographic_feature_index.cpp
        core/geographic_map_canvas.h core/geographic_map_canvas.cpp
        core/geographic_map_tiles.cpp
        core/geographic_map_export.cpp
        core/geographic_tile_source.h core/geographic_tile_source.cpp
        core/geographic_tile_network.cpp
        core/scene_model_3d.h core/scene_model_3d.cpp core/scene_model_3d_codec.cpp
        core/scene_model_3d_history.cpp core/scene_model_3d_parse.cpp core/scene_document_codec.cpp
        core/notebook_document.h core/notebook_document_p.h core/notebook_document.cpp
        core/notebook_document_blocks.cpp core/notebook_document_codec.cpp core/notebook_document_history.cpp
        core/notebook_document_html.cpp core/notebook_document_selection.cpp
        core/notebook_document_services.cpp core/notebook_document_text.cpp
        core/mesh_rig_document.h core/mesh_rig_document.cpp
        core/mesh_rig_document_persistence.cpp core/mesh_rig_history.cpp
        core/mesh_rig_pose.cpp core/mesh_rig_uv.cpp core/mesh_rig_paint.cpp
        core/raster_brush_p.h
        core/slide_deck_types.h core/slide_deck_types.cpp
        core/slide_deck.h core/slide_deck.cpp core/slide_deck_edit.cpp core/slide_deck_codec.cpp
        core/slide_timeline.h core/slide_timeline.cpp
        core/slide_render.h core/slide_render.cpp
        core/slide_show.h core/slide_show.cpp
        core/slide_frame_view.h core/slide_frame_view.cpp
        core/presentation_windows.h core/presentation_windows.cpp
        core/application_contracts.cpp core/application_contracts.h
        core/color_profile.cpp core/color_profile.h
        core/color_context.h core/color_engine.cpp core/color_engine.h
        core/color_grade_processor.cpp core/color_grade_processor.h
        core/color_grade_edit_session.cpp core/color_grade_edit_session.h
        core/color_grade_preview_model.cpp core/color_grade_preview_model.h
        core/color_picker_model.cpp core/color_picker_model.h
        core/color_lcms_p.h
        core/curve_evaluator.cpp core/curve_evaluator.h
        core/curve_validation.cpp core/curve_evaluator_p.h
        core/curve_editor_model.cpp core/curve_editor_model.h
        core/gradient_evaluator.cpp core/gradient_evaluator.h
        core/canvas_managed_paint.cpp core/canvas_managed_paint.h
        core/gradient_editor_model.cpp core/gradient_editor_model.h
        core/capability_registry.cpp core/capability_registry.h
        core/extension_registry.cpp core/extension_registry.h core/extension_provider.h
        core/command_context.cpp core/command_context.h
        core/command_registry.cpp core/command_registry.h
        core/edit_transaction.cpp core/edit_transaction.h
        core/document_model.h
        core/document_codec.cpp core/document_codec.h
        core/document_store.cpp core/document_store.h
        core/document_session.cpp core/document_session.h
        core/document_manager.cpp core/document_manager.h
        core/settings_model.cpp core/settings_model.h
        core/local_productivity_store.cpp core/local_productivity_store.h
        core/local_attachment_store.cpp core/local_attachment_store.h
        core/local_calendar_projection_model.cpp core/local_calendar_projection_model.h
        core/state_store.cpp core/state_store.h
        core/settings_persistence.cpp core/settings_persistence.h
        core/workspace_session.cpp core/workspace_session.h
        core/job_model.cpp core/job_model.h
        core/script_runtime.cpp core/script_runtime.h
        core/terminal_session.cpp core/terminal_session.h
        core/render_queue_model.cpp core/render_queue_model.h core/render_queue_provider.cpp
        core/notification_model.cpp core/notification_model.h
        core/pdf_annotation_document.cpp core/pdf_annotation_document.h
        core/expression_evaluator.cpp core/expression_evaluator.h
        core/find_replace_service.cpp core/find_replace_service.h
        core/rich_text_find_replace_service.cpp core/rich_text_find_replace_service.h
        core/diff_model.cpp core/diff_model.h
        core/virtual_collection_adapter.cpp core/virtual_collection_adapter.h
        core/event_log_model.cpp core/event_log_model.h
        core/transport_model.cpp core/transport_model.h
        core/test_transport_clock.cpp core/test_transport_clock.h
        core/audio_ring.h core/audio_media.cpp core/audio_media.h
        core/audio_device.cpp core/audio_device.h
        core/audio_graph.cpp core/audio_graph_mix.cpp core/audio_graph_midi.cpp core/audio_graph_comp.cpp core/audio_graph_plugins.cpp core/audio_graph.h
        core/audio_take_comp.cpp core/audio_take_comp.h
        core/audio_take_comp_model.cpp core/audio_take_comp_view.cpp core/audio_take_comp_model.h
        core/audio_plugin_ipc_p.h core/audio_plugin_types.cpp core/audio_plugin_types.h
        core/audio_plugin_registry.cpp core/audio_plugin_registry.h
        core/audio_plugin_processor.cpp core/audio_plugin_processor.h
        core/audio_plugin_instance.cpp core/audio_plugin_instance_editor.cpp core/audio_plugin_instance.h
        core/audio_plugin_editor.cpp core/audio_plugin_editor.h
        core/audio_plugin_host.cpp core/audio_plugin_host_content.cpp core/audio_plugin_host.h
        core/midi_types.cpp core/midi_types.h core/midi_program.cpp core/midi_program.h
        core/midi_sequence_model.cpp core/midi_sequence_edit.cpp core/midi_sequence_ops.cpp core/midi_sequence_model.h
        core/midi_smf.cpp core/midi_smf.h core/midi_launcher_model.cpp core/midi_launcher_model.h
        core/score_types.cpp core/score_rhythm.cpp core/score_codec.cpp core/score_types.h
        core/score_layout.cpp core/score_layout_notes.cpp core/score_render.cpp core/score_layout.h
        core/score_model.cpp core/score_model_structure.cpp core/score_model_edit.cpp core/score_model.h
        core/score_musicxml_write.cpp core/score_musicxml_read.cpp
        core/score_midi.cpp core/score_midi.h core/score_playback.cpp core/score_playback.h
        core/audio_channel_meters.cpp core/audio_channel_meters.h
        core/audio_mixer_types.cpp core/audio_mixer_codec.cpp core/audio_mixer_types.h
        core/audio_mixer_model.cpp core/audio_mixer_ops.cpp core/audio_mixer_model.h
        core/audio_mixer_strip_model.cpp core/audio_mixer_strip_model.h
        core/audio_engine.cpp core/audio_engine_transport.cpp core/audio_engine_rt.cpp core/audio_engine_meters.cpp
        core/audio_engine_midi.cpp core/audio_engine_midi_rt.cpp
        core/audio_engine.h core/audio_engine_p.h
        core/audio_session.cpp core/audio_session_transport.cpp core/audio_session_render.cpp core/audio_session_midi.cpp
        core/audio_session_comp.cpp
        core/audio_session.h
        core/audio_dsp.cpp core/audio_dsp.h core/audio_peaks.cpp core/audio_peaks.h
        core/audio_spectrogram.cpp core/audio_spectrogram.h
        core/audio_source_edit.cpp core/audio_source_edit.h
        core/audio_source_edit_model.cpp core/audio_source_edit_model.h
        core/audio_analysis_source.cpp core/audio_spectrogram_source.cpp core/audio_analysis_source.h
        core/uri_validator.cpp core/uri_validator.h
        core/token_editor_service.cpp core/token_editor_service.h
        core/resource_resolver.cpp core/resource_resolver.h
        core/asset_dependency_index.cpp core/asset_dependency_index.h
        core/trace_index.cpp core/trace_index.h
        core/asset_dependency_qml.cpp core/asset_dependency_scan.cpp
        core/trace_import.cpp
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
        core/timeline_planner_nle.cpp core/timeline_planner_links.cpp
        core/timeline_planner.h
        core/timeline_model.cpp core/timeline_model_edit.cpp core/timeline_model_history.cpp
        core/timeline_model_io.cpp core/timeline_model_qml.cpp core/timeline_model.h
        core/timeline_model_musical.cpp
        core/tempo_map_model.cpp core/tempo_map_model_edit.cpp core/tempo_map_model.h
        core/automation_types.h core/automation_curve.cpp
        core/automation_model.cpp core/automation_model_edit.cpp core/automation_model_qml.cpp
        core/automation_record.cpp core/automation_model.h
        core/motion_composition.cpp core/motion_composition_edit.cpp core/motion_composition.h
        core/motion_effect_registry.cpp core/motion_effect_registry.h
        core/motion_frame_renderer.cpp core/motion_frame_renderer.h
        core/tracking_roto_model.cpp core/tracking_roto_mask.cpp core/tracking_roto_codec.cpp core/tracking_roto_model.h
        core/tracking_roto_media.cpp core/tracking_engine.cpp core/tracking_engine.h
        core/node_graph_model.cpp core/node_graph_types.cpp core/node_graph_groups.cpp
        core/node_graph_render.cpp core/node_graph_codec.cpp core/node_graph_model.h
        core/diagram_document.cpp core/diagram_document_io.cpp core/diagram_document_layout.cpp
        core/diagram_document_flow.cpp core/diagram_document_route.cpp core/diagram_document_export.cpp
        core/diagram_document.h
        core/cad_document.cpp core/cad_document_solver.cpp core/cad_document_io.cpp
        core/cad_document_export.cpp
        core/cad_document.h
        core/motion_onion_skin_item.cpp core/motion_onion_skin_item.h
        core/engine_schedule.cpp core/engine_schedule.h
        core/mock_automation_engine.cpp core/mock_automation_engine.h
        core/timeline_visible_list.cpp core/timeline_visible_list.h
        core/timeline_viewport.cpp core/timeline_viewport_rows.cpp core/timeline_viewport.h
        core/timeline_media_provider.cpp core/timeline_media_provider.h
        core/timeline_transfer.cpp core/timeline_transfer.h
        core/clipboard_service.cpp core/clipboard_service.h core/clipboard_ranges.cpp
        core/clipboard_canvas.cpp
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
        core/rich_text_native_format.cpp core/rich_text_native_format.h
        core/native_reference_format.cpp
        core/plain_text_format.cpp
        core/asset_provider.cpp core/asset_provider.h
        core/file_asset_backend.cpp
        core/pdf_asset_backend.cpp core/pdf_asset_backend.h
        core/video_asset_backend.cpp core/video_asset_backend.h
        core/media_source_info.cpp core/media_source_info.h
        core/media_decoder.h core/media_ffmpeg_p.h
        core/media_decoder_ffmpeg.cpp core/media_decoder_frames.cpp core/media_decoder_audio.cpp
        core/media_encoder.h core/media_encoder_ffmpeg.cpp
        core/media_captions.h core/media_captions_ffmpeg.cpp
        core/media_finishing.cpp core/media_finishing.h
        core/video_finishing.cpp core/video_finishing.h
        core/media_backend_stub.cpp
        core/media_program.cpp core/media_program.h core/media_program_render.cpp
        core/media_transition.cpp core/media_transition.h
        core/media_export.cpp core/media_export.h core/media_export_jobs.cpp
        core/media_decode_worker.cpp core/media_decode_worker.h
        core/media_playback_engine.cpp core/media_playback_engine.h
        core/media_player.cpp core/media_player.h
        core/media_frame_view.cpp core/media_frame_view.h
        core/media_catalog.cpp core/media_catalog.h core/media_catalog_jobs.cpp
        core/media_frame_feed.cpp core/media_frame_feed.h
        core/sequence_editor.cpp core/sequence_editor.h
        core/multicam_model.cpp core/multicam_model.h
        core/video_scopes_model.cpp core/video_scopes_model.h
        core/video_scope_analysis.cpp core/video_scope_analysis.h
        core/caption_track_model.cpp core/caption_track_model.h core/caption_track_edits.cpp
        core/caption_track_codec.cpp core/caption_track_codec.h
        core/caption_frame_renderer.cpp core/caption_frame_renderer.h
        core/caption_sequence_exporter.cpp core/caption_sequence_exporter.h
        core/recovery_journal.cpp core/recovery_journal.h
        core/recovery_manager.cpp core/recovery_manager.h
        core/property_document.cpp core/property_document.h
        core/local_property_sync.cpp core/local_property_sync_restore.cpp core/local_property_sync.h
        core/local_property_sync_structure.cpp core/local_property_sync_text.cpp
        core/local_property_sync_undo.cpp
        core/property_sync_codec.cpp core/property_sync_detail_p.h
        core/property_sync_channel.cpp core/property_sync_channel.h
        core/property_sync_socket.cpp core/property_sync_socket_client.cpp core/property_sync_socket.h
        core/property_version_history.cpp core/property_version_history.h
        core/reference_document_codec.cpp core/reference_document_codec.h
        core/reference_collection_value.cpp core/reference_collection_value.h
        core/rich_text_types.cpp core/rich_text_types.h core/rich_text_spans.h
        core/rich_text_document.cpp core/rich_text_document.h
        core/rich_text_edit.cpp core/rich_text_history.cpp
        core/rich_text_ops.cpp core/rich_text_ops_p.h
        core/rich_text_structure.cpp core/rich_text_fragment.cpp core/rich_text_table_edit.cpp
        core/rich_text_table_spans.cpp core/rich_text_style_edit.cpp core/rich_text_frames.cpp
        core/rich_text_publishing.cpp core/rich_text_publishing.h
        core/rich_text_page_setup_mm.cpp
        core/rich_text_review.cpp core/rich_text_review.h core/rich_text_review_edit.cpp
        core/rich_text_review_snapshots.cpp core/rich_text_fields.cpp core/rich_text_fields_p.h
        core/rich_text_fields_snapshots.cpp
        core/rich_text_equation.cpp core/rich_text_equation.h core/rich_text_equation_text.cpp
        core/rich_text_equation_layout.cpp core/rich_text_equation_edit.cpp
        core/rich_text_pdf_export.cpp
        core/print_ticket.cpp core/print_ticket.h
        core/print_output.cpp core/print_output.h
        core/print_preflight.cpp core/print_preflight.h
        core/print_adapter.cpp core/print_adapter.h core/print_adapter_p.h
        core/print_adapter_jobs.cpp core/print_adapter_repair.cpp
        core/print_sheet_view.cpp core/print_sheet_view.h
        core/rich_text_codec.cpp core/rich_text_codec_read.cpp core/rich_text_codec_publishing.cpp
        core/rich_text_codec_review.cpp
        core/rich_text_codec.h core/rich_text_codec_p.h
        core/rich_text_layout.h core/rich_text_layout_p.h core/rich_text_shaping_p.h
        core/rich_text_shaping.cpp core/rich_text_pagination.cpp core/rich_text_table_flow.cpp
        core/rich_text_regions.cpp
        core/rich_text_pages.cpp core/rich_text_mapping.cpp core/rich_text_navigation.cpp
        core/rich_text_painter.cpp core/rich_text_objects.cpp core/rich_text_objects.h
        core/rich_text_asset_renderer.cpp core/rich_text_asset_renderer.h
        core/rich_text_graphemes.cpp core/rich_text_graphemes.h
        core/rich_text_area.cpp core/rich_text_area.h core/rich_text_area_input.cpp
        core/rich_text_area_edit.cpp core/rich_text_area_commands.cpp core/rich_text_area_structure.cpp
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
        core/workbook_history.cpp core/workbook_document_codec.cpp
        core/workbook.cpp core/workbook.h core/workbook_p.h
        core/workbook_recalc.cpp core/workbook_mutations.cpp core/workbook_ops.cpp
        core/workbook_codec.cpp core/workbook_codec_p.h
        core/workbook_persistence.cpp core/workbook_format.h
        core/workbook_sheet_adapter.cpp core/workbook_sheet_adapter.h
        core/query_spec.cpp core/query_eval.cpp core/query_pivot.cpp core/query_core.h
        core/query_detail.cpp core/query_join.cpp core/query_persistence.cpp
        core/query_grid_adapter.cpp core/query_grid_adapter.h
        core/query_controller.cpp core/query_controller.h
        core/local_database_provider.cpp core/local_database_provider.h
        core/local_data_design_store.cpp core/local_data_design_store.h
        core/local_report_document_model.cpp core/local_report_document_model.h
        core/local_report_spec.cpp core/local_report_spec.h core/local_report_engine.cpp
        core/sqlite_query_source.cpp core/sqlite_query_source.h
        core/local_data_design_document.cpp core/local_data_design_document.h
        core/local_data_design_edits.cpp core/local_data_design_codec.cpp
        core/recurrence_service.cpp core/recurrence_service.h
        core/shortcut_map.cpp core/shortcut_map_metadata.cpp core/shortcut_map.h
        core/density.cpp core/density.h
        core/focus_manager.cpp core/focus_manager.h
        core/dock_model.cpp core/dock_model.h core/dock_model_p.h
        core/dock_model_ops.cpp
        core/dock_model_store.cpp
        core/flex.cpp core/flex.h
        core/grid.cpp core/grid.h
        core/chart_axis.cpp core/chart_axis.h
        core/scientific_dataset.cpp core/scientific_dataset.h
        core/scientific_grid.cpp core/scientific_grid.h
        core/scientific_sampling.cpp core/scientific_sampling.h
        core/scientific_plot_renderer.cpp core/scientific_plot_renderer.h
        core/scientific_plot_paint.cpp
        core/scientific_figure_composer.cpp core/scientific_figure_composer.h
        core/scientific_scene3d.cpp core/scientific_scene3d.h
        core/scientific_scene3d_render.cpp
        core/voxel_volume.cpp core/voxel_volume.h
        core/volume_cursor.cpp core/volume_cursor.h
        core/transfer_function_model.cpp core/transfer_function_history.cpp core/transfer_function_model.h
        core/image_slice_item.cpp core/image_slice_reslice.cpp core/image_slice_item.h
        core/volume_raycast.cpp core/volume_raycast.h
        core/volume_view_item.cpp core/volume_view_item.h
        core/volume_render_node.cpp core/volume_render_node.h
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
        core/image_compare_item.cpp core/image_compare_item.h
        core/grid_tracks.cpp core/grid_tracks.h
        core/icon.cpp core/icon.h
        core/icon_data.cpp core/icon_data.h
        core/meter.cpp core/meter.h
        core/audio_meter_model.cpp core/audio_meter_model.h
        core/audio_meter_tap.cpp core/audio_meter_tap.h
        core/audio_loudness_model.cpp core/audio_loudness_model.h
        core/audio_parameter_map.cpp core/audio_parameter_map.h
        core/audio_parameter_binding.cpp core/audio_parameter_binding.h
        core/layout_attached.cpp core/layout_attached.h
        core/layout_info.cpp core/layout_info.h
        core/screen_work_area.cpp core/screen_work_area.h
        core/stack.cpp core/stack.h
        core/stylus_handler.cpp core/stylus_handler.h
        core/canvas_transform.cpp core/canvas_transform.h
        core/canvas_object.h
        core/canvas_geometry.cpp core/canvas_geometry.h
        core/canvas_path.cpp core/canvas_path.h
        core/canvas_path_boolean.cpp core/canvas_path_boolean.h
        core/canvas_appearance.cpp core/canvas_appearance_codec.cpp core/canvas_appearance.h
        core/canvas_svg_export.cpp core/canvas_svg_export.h
        core/canvas_document.cpp core/canvas_document.h
        core/canvas_document_state.h
        core/canvas_document_edit.cpp core/canvas_document_p.h
        core/canvas_document_format.cpp core/canvas_document_format_sizes.cpp core/canvas_document_format.h
        core/canvas_document_object_validation.cpp
        core/canvas_document_misc_validation.cpp
        core/canvas_document_codec.cpp
        core/canvas_document_guides.cpp
        core/canvas_document_layers.cpp
        core/canvas_document_artboards.cpp
        core/canvas_document_scene_format.cpp
        core/canvas_document_qml.cpp
        core/canvas_document_path.cpp
        core/canvas_document_boolean.cpp
        core/canvas_parametric.cpp core/canvas_parametric.h
        core/canvas_document_parametric.cpp
        core/canvas_document_contours.cpp
        core/canvas_native_format.cpp
        core/hit_test_service.cpp core/hit_test_service.h
        core/tool_controller.cpp core/tool_controller.h
        core/canvas_selection_model.cpp core/canvas_selection_model.h
        core/canvas_layer_tree_model.cpp core/canvas_layer_tree_commands.cpp core/canvas_layer_tree_model.h
        core/canvas_artboard_model.cpp core/canvas_artboard_model.h
        core/canvas_alignment_model.cpp core/canvas_alignment_model.h
        core/canvas_style_model.cpp core/canvas_style_model_selection.cpp core/canvas_style_model.h
        core/canvas_appearance_model.cpp core/canvas_appearance_model.h
        core/canvas_tools.cpp
        core/canvas_render.h
        core/canvas_painter.cpp
        core/canvas_appearance_paint.cpp core/canvas_appearance_paint.h
        core/canvas_effects.cpp core/canvas_effects.h
        core/canvas_tessellator.cpp
        core/render_surface_adapter.cpp core/render_surface_adapter.h
        core/canvas_view_state.cpp core/canvas_view_state.h
        core/canvas_snap_model.cpp core/canvas_snap_model.h
        core/canvas_transform_controller.cpp core/canvas_transform_controller.h
        core/canvas_surface.cpp core/canvas_surface.h
        core/tiled_raster_document.cpp core/tiled_raster_document_output.cpp core/tiled_raster_document_persistence.cpp core/tiled_raster_document.h
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
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network
    PRIVATE Qt6::Svg Qt6::Concurrent PkgConfig::LCMS2 SQLite3::SQLite3
    PUBLIC PkgConfig::VTERM)
if(QINDATK_WITH_QUICK3D)
    target_link_libraries(qindatk PRIVATE Qt6::Quick3D)
endif()
# GPU volume raycasting (D-147) is optional. Without Qt Shader Tools the
# resources are absent, VolumeView reports "GPU volume shaders are not built"
# and draws the reference path. 3D samplers need GLSL 300 es / 330.
# AGENT-CONTRACT (core/volume_render_node.cpp): PREFIX + file path is the
# resource path the node loads, ":/qindatk/shaders/volume_raycast.*.qsb".
if(QINDATK_ENABLE_SHADER_TOOLS)
    find_package(Qt6ShaderTools 6.8 QUIET)
endif()
set(QINDATK_WITH_SHADER_TOOLS FALSE PARENT_SCOPE)
if(QINDATK_ENABLE_SHADER_TOOLS AND TARGET Qt6::ShaderTools)
    set(QINDATK_WITH_SHADER_TOOLS TRUE PARENT_SCOPE)
    qt_add_shaders(qindatk "qindatk_volume_shaders"
        PREFIX "/qindatk"
        GLSL "300es,330"
        NOHLSL
        NOMSL
        FILES
            shaders/volume_raycast.vert
            shaders/volume_raycast.frag)
else()
    message(STATUS "QindaTK: GPU volume shaders unavailable (Qt Shader Tools not installed)")
endif()
if(QINDATK_ENABLE_POPPLER_QT6 AND POPPLER_QT6_FOUND)
    target_link_libraries(qindatk PRIVATE PkgConfig::POPPLER_QT6)
    target_compile_definitions(qindatk PRIVATE QINDATK_HAVE_POPPLER_QT6=1)
endif()
if(QINDATK_ENABLE_ALSA AND QINDATK_ALSA_FOUND)
    target_sources(qindatk PRIVATE core/audio_device_alsa.cpp)
    target_link_libraries(qindatk PRIVATE PkgConfig::QINDATK_ALSA)
    target_compile_definitions(qindatk PRIVATE QINDATK_HAVE_ALSA=1)
endif()
# The media backend's FFmpeg headers stay inside qindatk's .cpp files and
# media_ffmpeg_p.h, so consumers never need them (D-096).
if(QINDATK_ENABLE_FFMPEG AND FFMPEG_FOUND)
    target_link_libraries(qindatk PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(qindatk PRIVATE QINDATK_HAVE_FFMPEG=1)
endif()
# Qt Quick's private tablet-event hook is confined to stylus_handler.cpp.
target_link_libraries(qindatk PRIVATE Qt6::QuickPrivate)

# ---- out-of-process plugin host (WP-066, D-190, docs/audio-plugins.md) ----
# AGENT-CONTRACT: qindatk never loads plugin code. It starts this executable
# (one process per plugin instance, and one per scanned binary) and talks to
# it through core/audio_plugin_ipc_p.h. The CLAP headers (MIT, vendored in
# third_party/clap) are used only here and by the test plugins.
qt_add_executable(qindatk-plugin-host
    pluginhost/plugin_host_main.cpp
    pluginhost/plugin_host_library.cpp
    pluginhost/plugin_host_instance.cpp
    pluginhost/plugin_host_editor.cpp
    pluginhost/plugin_host_audio.cpp
    pluginhost/plugin_host_clap.h)
qindatk_enable_warnings(qindatk-plugin-host)
target_include_directories(qindatk-plugin-host PRIVATE core "${PROJECT_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(qindatk-plugin-host PRIVATE Qt6::Core ${CMAKE_DL_LIBS})
set_target_properties(qindatk-plugin-host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libexec")
install(TARGETS qindatk-plugin-host RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/qindatk")
set_source_files_properties(core/audio_plugin_types.cpp PROPERTIES COMPILE_DEFINITIONS
    "QINDATK_PLUGIN_HOST_BUILD_PATH=\"$<TARGET_FILE:qindatk-plugin-host>\";QINDATK_PLUGIN_HOST_INSTALL_PATH=\"${CMAKE_INSTALL_FULL_LIBEXECDIR}/qindatk/qindatk-plugin-host\"")
add_dependencies(qindatk qindatk-plugin-host)

if(QINDATK_ENABLE_WIDGETS AND TARGET Qt6::Widgets)
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
    # AGENT-NOTE (WP-053): the native print backend needs Qt PrintSupport,
    # which depends on Widgets; base qindatk keeps only the PDF writer.
    if(QINDATK_ENABLE_NATIVE_PRINT AND TARGET Qt6::PrintSupport)
        target_sources(qindatk_widgets PRIVATE
            platform_widgets/native_print_backend.cpp
            platform_widgets/native_print_backend.h)
        target_link_libraries(qindatk_widgets PUBLIC Qt6::PrintSupport)
        target_compile_definitions(qindatk_widgets PUBLIC QINDATK_HAVE_NATIVE_PRINT=1)
        install(FILES platform_widgets/native_print_backend.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindatk)
        set(QINDATK_WIDGETS_PRINT TRUE PARENT_SCOPE)
    endif()
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
# AGENT-NOTE (D-202): installing into Qt's own prefix (a distro package)
# puts the modules on Qt's default import path. Any other prefix keeps them
# inside that prefix, relative, so a staged install can be moved; the
# WP-088 relocation check (tools/qualify) runs a consumer from the moved stage.
if(NOT DEFINED QINDATK_QML_INSTALL_DIR)
    cmake_path(COMPARE "${CMAKE_INSTALL_PREFIX}" EQUAL "${QT6_INSTALL_PREFIX}" qindatk_into_qt_prefix)
    if(qindatk_into_qt_prefix)
        set(qindatk_default_qml_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    else()
        set(qindatk_default_qml_dir "${CMAKE_INSTALL_LIBDIR}/qml")
    endif()
    set(QINDATK_QML_INSTALL_DIR "${qindatk_default_qml_dir}" CACHE PATH
        "Root QML import directory to install the QindaTK modules into (relative: under the install prefix)")
endif()
set(QINDATK_QML_INSTALL_DIR "${QINDATK_QML_INSTALL_DIR}" PARENT_SCOPE)

# AGENT-GUARD (D-202): the QML plugins live in <qml>/QindaTK and
# <qml>/QindaTK/QindaQt, not in bindir, so Qt's standard install RPATH
# ($ORIGIN/../lib) does not reach libqindatk from them. A QML-only
# application (one that does not link qindatk itself) then fails to load the
# plugin from any prefix outside the linker's default path. Each plugin gets
# an $ORIGIN-relative RUNPATH to the library directory instead.
cmake_path(ABSOLUTE_PATH QINDATK_QML_INSTALL_DIR BASE_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
    NORMALIZE OUTPUT_VARIABLE qindatk_abs_qml_dir)
cmake_path(ABSOLUTE_PATH CMAKE_INSTALL_LIBDIR BASE_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
    NORMALIZE OUTPUT_VARIABLE qindatk_abs_lib_dir)
file(RELATIVE_PATH qindatk_plugin_to_lib "${qindatk_abs_qml_dir}/QindaTK" "${qindatk_abs_lib_dir}")
file(RELATIVE_PATH qindatk_bridge_to_lib "${qindatk_abs_qml_dir}/QindaTK/QindaQt" "${qindatk_abs_lib_dir}")
set_target_properties(qindatkplugin PROPERTIES INSTALL_RPATH "$ORIGIN/${qindatk_plugin_to_lib}")
set_target_properties(qindatk_qindaqt qindatk_qindaqtplugin PROPERTIES
    INSTALL_RPATH "$ORIGIN;$ORIGIN/${qindatk_bridge_to_lib}")

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
