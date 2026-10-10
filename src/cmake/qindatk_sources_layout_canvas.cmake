# SPDX-License-Identifier: LGPL-3.0-or-later

# qindatk C++ sources (included by src/CMakeLists.txt; paths relative to src/).
# Layout, docking, charts, scientific and volume views, graph, icons,
# meters, canvas documents and theme.
set(qindatk_layout_canvas_sources
    core/density.cpp core/density.h
    core/focus_manager.cpp core/focus_manager.h
    core/dock_model.cpp core/dock_model.h core/dock_model_p.h
    core/dock_model_ops.cpp
    core/dock_model_queries.cpp
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
    core/voxel_volume.cpp core/voxel_volume_info.cpp core/voxel_volume.h
    core/volume_cursor.cpp core/volume_cursor.h
    core/transfer_function_model.cpp core/transfer_function_history.cpp
    core/transfer_function_ramps.cpp core/transfer_function_model.h
    core/image_slice_item.cpp core/image_slice_reslice.cpp core/image_slice_settings.cpp
    core/image_slice_item.h
    core/volume_raycast.cpp core/volume_raycast.h
    core/volume_view_item.cpp core/volume_view_render.cpp core/volume_view_item.h
    core/volume_render_worker.cpp core/volume_render_worker.h
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
    core/gantt_task_model.cpp core/gantt_task_planning.cpp core/gantt_task_model.h
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
    core/canvas_style_model_fill.cpp
    core/canvas_path_edit_model.cpp core/canvas_path_edit_model.h core/canvas_path_editing.cpp
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
    core/theme_ramp.cpp core/theme_ramp.h)
