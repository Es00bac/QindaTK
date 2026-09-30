# SPDX-License-Identifier: LGPL-3.0-or-later

# qindatk C++ sources (included by src/CMakeLists.txt; paths relative to src/).
# Documents and application services: code, geographic, scene, notebook,
# mesh-rig and slide documents; colour and curves; commands, document
# sessions, settings, local productivity, mail and calendar; jobs and search.
set(qindatk_app_sources
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
    core/notebook_document_html.cpp core/notebook_document_reconcile.cpp core/notebook_document_selection.cpp
    core/notebook_document_services.cpp core/notebook_document_text.cpp
    core/mesh_rig_document.h core/mesh_rig_document.cpp
    core/mesh_rig_document_persistence.cpp core/mesh_rig_history.cpp
    core/mesh_rig_pose.cpp core/mesh_rig_uv.cpp core/mesh_rig_paint.cpp
    core/mesh_rig_animation.cpp core/mesh_rig_authoring.cpp core/mesh_rig_camera.cpp
    core/mesh_rig_document_codec.cpp
    core/raster_brush_p.h
    core/slide_deck_types.h core/slide_deck_types.cpp
    core/slide_deck.h core/slide_deck.cpp core/slide_deck_edit.cpp core/slide_deck_codec.cpp
    core/slide_deck_limits.cpp core/slide_canvas_document.h core/slide_canvas_document.cpp
    core/slide_timeline.h core/slide_timeline.cpp
    core/slide_render.h core/slide_render.cpp
    core/slide_show.h core/slide_show.cpp core/slide_show_annotations.cpp
    core/slide_preview.h core/slide_preview.cpp
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
    core/local_productivity_store.cpp core/local_productivity_events.cpp core/local_productivity_store.h
    core/local_attachment_store.cpp core/local_attachment_store.h
    core/local_calendar_projection_model.cpp core/local_calendar_projection_model.h
    core/protocol_client.cpp core/protocol_client.h
    core/mail_mime.cpp core/mail_mime_parse.cpp core/mail_mime.h
    core/mail_address_validator.cpp core/mail_address_validator.h
    core/smtp_mail_provider.cpp core/smtp_mail_provider.h
    core/imap_session.cpp core/imap_session.h
    core/imap_mail_jobs.cpp core/imap_mail_jobs.h
    core/imap_mail_provider.cpp core/imap_mail_provider.h
    core/mail_record.cpp core/mail_record.h
    core/pop3_session.cpp core/pop3_session.h
    core/pop3_mail_jobs.cpp core/pop3_mail_jobs.h
    core/pop3_mail_provider.cpp core/pop3_mail_provider.h
    core/ical_codec.cpp core/ical_overrides.cpp core/ical_codec.h core/ical_codec_p.h
    core/caldav_jobs.cpp core/caldav_jobs.h
    core/caldav_provider.cpp core/caldav_provider.h
    core/caldav_discovery.cpp core/caldav_discovery.h
    core/ical_calendar.cpp
    core/imap_mutf7.cpp core/imap_mutf7.h
    core/imap_folders.cpp core/imap_folders.h
    core/ics_calendar_jobs.cpp core/ics_calendar_jobs.h
    core/ics_calendar_provider.cpp core/ics_calendar_provider.h
    core/state_store.cpp core/state_store.h
    core/settings_persistence.cpp core/settings_persistence.h
    core/workspace_session.cpp core/workspace_session.h
    core/job_model.cpp core/job_model.h
    core/script_runtime.cpp core/script_runtime.h
    core/terminal_session.cpp core/terminal_session.h
    core/terminal_session_screen.cpp core/terminal_session_input.cpp
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
    core/test_transport_clock.cpp core/test_transport_clock.h)
