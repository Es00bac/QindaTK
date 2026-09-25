# SPDX-License-Identifier: LGPL-3.0-or-later

# qindatk C++ sources (included by src/CMakeLists.txt; paths relative to src/).
# Audio engine, plugins, MIDI and score.
set(qindatk_audio_sources
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
    core/midi_smf.cpp core/midi_smf.h core/midi_launcher_model.cpp core/midi_launcher_record.cpp core/midi_launcher_model.h
    core/midi_input_port.cpp core/midi_input_port.h
    core/score_types.cpp core/score_rhythm.cpp core/score_codec.cpp core/score_types.h
    core/score_layout.cpp core/score_layout_notes.cpp core/score_render.cpp core/score_layout.h
    core/score_paint.cpp core/score_glyphs.cpp core/score_paint.h core/score_canvas_item.cpp core/score_canvas_item.h
    core/score_model.cpp core/score_model_structure.cpp core/score_model_edit.cpp core/score_model.h
    core/score_musicxml_write.cpp core/score_musicxml_read.cpp core/score_musicxml_read_notes.cpp
    core/score_musicxml_read_p.h
    core/score_midi.cpp core/score_midi.h core/score_playback.cpp core/score_playback.h
    core/audio_channel_meters.cpp core/audio_channel_meters.h
    core/audio_mixer_types.cpp core/audio_mixer_codec.cpp core/audio_mixer_spatial.cpp core/audio_mixer_types.h
    core/audio_mixer_model.cpp core/audio_mixer_ops.cpp core/audio_mixer_model_io.cpp core/audio_mixer_model.h
    core/audio_mixer_strip_model.cpp core/audio_mixer_strip_model.h
    core/audio_control_surface.cpp core/audio_control_surface_port.cpp core/audio_control_surface.h
    core/audio_engine.cpp core/audio_engine_transport.cpp core/audio_engine_rt.cpp core/audio_engine_meters.cpp
    core/audio_engine_midi.cpp core/audio_engine_midi_rt.cpp
    core/audio_engine.h core/audio_engine_p.h
    core/audio_session_history.cpp core/audio_session_history.h
    core/audio_session.cpp core/audio_session_transport.cpp core/audio_session_render.cpp core/audio_session_midi.cpp
    core/audio_session_comp.cpp
    core/audio_session.h
    core/audio_dsp.cpp core/audio_dsp.h core/audio_peaks.cpp core/audio_peaks.h
    core/audio_spectrogram.cpp core/audio_spectrogram.h
    core/audio_source_edit.cpp core/audio_source_edit.h
    core/audio_source_edit_model.cpp core/audio_source_edit_model.h
    core/audio_analysis_source.cpp core/audio_spectrogram_source.cpp core/audio_analysis_source.h
    core/audio_pitch.cpp core/audio_pitch_correct.cpp core/audio_pitch.h core/audio_pitch_p.h
    core/audio_pitch_edit.cpp core/audio_pitch_edit.h
    core/audio_pitch_source.cpp core/audio_pitch_source_edit.cpp core/audio_pitch_source.h)
