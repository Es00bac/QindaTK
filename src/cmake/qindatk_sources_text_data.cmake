# SPDX-License-Identifier: LGPL-3.0-or-later

# qindatk C++ sources (included by src/CMakeLists.txt; paths relative to src/).
# Recovery, property sync, rich text, printing, editable grids, workbook,
# query and local data design.
set(qindatk_text_data_sources
    core/recovery_journal.cpp core/recovery_journal.h
    core/recovery_manager.cpp core/recovery_manager.h
    core/property_document.cpp core/property_document.h
    core/local_property_sync.cpp core/local_property_sync_restore.cpp core/local_property_sync.h
    core/local_property_sync_structure.cpp core/local_property_sync_text.cpp
    core/local_property_sync_undo.cpp core/local_property_sync_service.cpp
    core/local_property_sync_state.cpp
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
    core/rich_text_steps.h core/rich_text_review_track.cpp core/rich_text_review_structure.cpp
    core/rich_text_codec_track.cpp core/rich_text_notes.cpp
    core/rich_text_review_snapshots.cpp core/rich_text_fields.cpp core/rich_text_fields_p.h
    core/rich_text_fields_snapshots.cpp
    core/rich_text_equation.cpp core/rich_text_equation.h core/rich_text_equation_text.cpp
    core/rich_text_equation_layout.cpp core/rich_text_equation_edit.cpp
    core/rich_text_equation_editor.cpp core/rich_text_equation_editor.h
    core/rich_text_equation_editor_p.h core/rich_text_equation_caret.cpp
    core/equation_canvas.cpp core/equation_canvas.h
    core/rich_text_pdf_export.cpp
    core/print_ticket.cpp core/print_ticket.h
    core/print_output.cpp core/print_output.h
    core/print_preflight.cpp core/print_preflight.h
    core/print_adapter.cpp core/print_adapter.h core/print_adapter_p.h
    core/print_adapter_jobs.cpp core/print_adapter_repair.cpp
    core/print_adapter_preview.cpp core/print_preview_renderer.cpp core/print_preview_renderer_p.h
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
    core/shortcut_map.cpp core/shortcut_map_metadata.cpp core/shortcut_map.h)
