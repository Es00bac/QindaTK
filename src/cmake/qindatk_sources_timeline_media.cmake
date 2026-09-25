# SPDX-License-Identifier: LGPL-3.0-or-later

# qindatk C++ sources (included by src/CMakeLists.txt; paths relative to src/).
# Resources and transfers, timeline, automation, motion, diagram and CAD
# documents, clipboard and platform services, formats, media and captions.
set(qindatk_timeline_media_sources
    core/uri_validator.cpp core/uri_validator.h
    core/token_editor_service.cpp core/token_editor_service.h
    core/resource_resolver.cpp core/resource_resolver.h
    core/asset_dependency_index.cpp core/asset_dependency_index.h
    core/trace_index.cpp core/trace_index.h
    core/asset_dependency_qml.cpp core/asset_dependency_scan.cpp
    core/trace_import.cpp core/trace_layout.cpp
    core/asset_dependency_graph.cpp
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
    core/timeline_planner_nle.cpp core/timeline_planner_links.cpp core/timeline_planner_transitions.cpp
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
    core/tracking_roto_media.cpp core/tracking_roto_analysis.cpp core/tracking_roto_propagation.cpp core/tracking_engine.cpp core/tracking_engine.h
    core/node_graph_model.cpp core/node_graph_types.cpp core/node_graph_groups.cpp
    core/node_graph_render.cpp core/node_graph_codec.cpp core/node_graph_model.h
    core/diagram_document.cpp core/diagram_document_io.cpp core/diagram_document_layout.cpp
    core/diagram_document_flow.cpp core/diagram_document_route.cpp core/diagram_document_export.cpp
    core/diagram_document_edit.cpp
    core/diagram_document.h
    core/cad_document.cpp core/cad_document_solver.cpp core/cad_document_io.cpp
    core/cad_document_export.cpp core/cad_document_edit.cpp
    core/cad_document.h
    core/motion_onion_skin_item.cpp core/motion_onion_skin_item.h
    core/engine_schedule.cpp core/engine_schedule.h
    core/mock_automation_engine.cpp core/mock_automation_engine.h
    core/timeline_visible_list.cpp core/timeline_visible_list.h
    core/timeline_viewport.cpp core/timeline_viewport_rows.cpp core/timeline_viewport.h
    core/timeline_media_provider.cpp core/timeline_media_provider.h core/timeline_media_decoded.cpp
    core/timeline_transfer.cpp core/timeline_transfer.h
    core/clipboard_service.cpp core/clipboard_service.h core/clipboard_ranges.cpp
    core/clipboard_canvas.cpp
    core/clipboard_paste.cpp
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
    core/caption_sequence_exporter.cpp core/caption_sequence_exporter.h)
