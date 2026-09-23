<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Complete proposal coverage

**All 212 proposals are mandatory in this implementation program**, including P3.
Each has exactly one primary work package. Other packages may consume or extend
its API. Priority is copied from the proposal book and is not an effort estimate.
The linked original purpose, contract and completion check remain acceptance
requirements; a package summary does not replace them.

[Plan](README.md) · [Supporting scope](supporting-scope.md) · [Execution ledger](execution-ledger.md)

| Proposal | Name and original contract | Priority | Primary package | Implementation lead |
| --- | --- | --- | --- | --- |
| APP-01 | [CommandRegistry](../proposed/01-application-services.md#app-01) | P0 | [WP-011](phase-01-services.md#wp-011) | Sol |
| APP-02 | [CommandContext](../proposed/01-application-services.md#app-02) | P0 | [WP-011](phase-01-services.md#wp-011) | Sol |
| APP-03 | [ShortcutMap](../proposed/01-application-services.md#app-03) | P0 | [WP-011](phase-01-services.md#wp-011) | Sol |
| APP-04 | [UndoService](../proposed/01-application-services.md#app-04) | P0 | [WP-012](phase-01-services.md#wp-012) | Sol |
| APP-05 | [EditTransaction](../proposed/01-application-services.md#app-05) | P0 | [WP-012](phase-01-services.md#wp-012) | Sol |
| APP-06 | [SelectionModel](../proposed/01-application-services.md#app-06) | P0 | [WP-012](phase-01-services.md#wp-012) | Sol |
| APP-07 | [DocumentSession](../proposed/01-application-services.md#app-07) | P0 | [WP-013](phase-01-services.md#wp-013) | Sol |
| APP-08 | [DocumentManager](../proposed/01-application-services.md#app-08) | P0 | [WP-013](phase-01-services.md#wp-013) | Sol |
| APP-09 | [WorkspaceSession](../proposed/01-application-services.md#app-09) | P1 | [WP-014](phase-01-services.md#wp-014) | Sol |
| APP-10 | [PropertyBinding](../proposed/01-application-services.md#app-10) | P0 | [WP-012](phase-01-services.md#wp-012) | Sol |
| APP-11 | [ValidationModel](../proposed/01-application-services.md#app-11) | P1 | [WP-012](phase-01-services.md#wp-012) | Sol |
| APP-12 | [SettingsModel](../proposed/01-application-services.md#app-12) | P1 | [WP-014](phase-01-services.md#wp-014) | Sol |
| APP-13 | [JobModel](../proposed/01-application-services.md#app-13) | P1 | [WP-015](phase-01-services.md#wp-015) | Sol |
| APP-14 | [AssetProvider](../proposed/01-application-services.md#app-14) | P1 | [WP-015](phase-01-services.md#wp-015) | Sol |
| APP-15 | [ClipboardService](../proposed/01-application-services.md#app-15) | P1 | [WP-016](phase-01-services.md#wp-016) | Sol |
| APP-16 | [DragDropService](../proposed/01-application-services.md#app-16) | P1 | [WP-016](phase-01-services.md#wp-016) | Sol |
| APP-17 | [HistoryView](../proposed/01-application-services.md#app-17) | P1 | [WP-026](phase-02-controls-data.md#wp-026) | Luna |
| APP-18 | [RecoveryManager](../proposed/01-application-services.md#app-18) | P0 | [WP-013](phase-01-services.md#wp-013) | Sol |
| APP-19 | [ResourceResolver](../proposed/01-application-services.md#app-19) | P1 | [WP-015](phase-01-services.md#wp-015) | Sol |
| APP-20 | [CapabilityRegistry](../proposed/01-application-services.md#app-20) | P1 | [WP-010](phase-01-services.md#wp-010) | Sol |
| AUD-01 | [TimeMap](../proposed/06-audio-music.md#aud-01) | P0 | [WP-046](phase-03-authoring.md#wp-046) | Sol |
| AUD-02 | [TransportModel](../proposed/06-audio-music.md#aud-02) | P1 | [WP-047](phase-03-authoring.md#wp-047) | Sol |
| AUD-03 | [TransportBar](../proposed/06-audio-music.md#aud-03) | P1 | [WP-047](phase-03-authoring.md#wp-047) | Sol |
| AUD-04 | [EditTimeline](../proposed/06-audio-music.md#aud-04) | P1 | [WP-048](phase-03-authoring.md#wp-048) | Sol |
| AUD-05 | [TrackHeader](../proposed/06-audio-music.md#aud-05) | P1 | [WP-048](phase-03-authoring.md#wp-048) | Sol |
| AUD-06 | [ClipItem](../proposed/06-audio-music.md#aud-06) | P1 | [WP-048](phase-03-authoring.md#wp-048) | Sol |
| AUD-07 | [TimeSelection](../proposed/06-audio-music.md#aud-07) | P1 | [WP-048](phase-03-authoring.md#wp-048) | Sol |
| AUD-08 | [MarkerLane](../proposed/06-audio-music.md#aud-08) | P1 | [WP-048](phase-03-authoring.md#wp-048) | Sol |
| AUD-09 | [TempoLane](../proposed/06-audio-music.md#aud-09) | P2 | [WP-049](phase-03-authoring.md#wp-049) | Sol |
| AUD-10 | [Knob](../proposed/06-audio-music.md#aud-10) | P1 | [WP-061](phase-05-audio.md#wp-061) | Sol |
| AUD-11 | [Fader](../proposed/06-audio-music.md#aud-11) | P1 | [WP-061](phase-05-audio.md#wp-061) | Sol |
| AUD-12 | [AudioMeter](../proposed/06-audio-music.md#aud-12) | P1 | [WP-061](phase-05-audio.md#wp-061) | Sol |
| AUD-13 | [ChannelStrip](../proposed/06-audio-music.md#aud-13) | P2 | [WP-062](phase-05-audio.md#wp-062) | Sol |
| AUD-14 | [MixerView](../proposed/06-audio-music.md#aud-14) | P2 | [WP-062](phase-05-audio.md#wp-062) | Sol |
| AUD-15 | [PanningControl](../proposed/06-audio-music.md#aud-15) | P2 | [WP-062](phase-05-audio.md#wp-062) | Sol |
| AUD-16 | [RoutingMatrix](../proposed/06-audio-music.md#aud-16) | P2 | [WP-062](phase-05-audio.md#wp-062) | Sol |
| AUD-17 | [PluginRack](../proposed/06-audio-music.md#aud-17) | P2 | [WP-066](phase-05-audio.md#wp-066) | Sol |
| AUD-18 | [PluginBrowser](../proposed/06-audio-music.md#aud-18) | P2 | [WP-066](phase-05-audio.md#wp-066) | Sol |
| AUD-19 | [AutomationLane](../proposed/06-audio-music.md#aud-19) | P1 | [WP-049](phase-03-authoring.md#wp-049) | Sol |
| AUD-20 | [WaveformEditor](../proposed/06-audio-music.md#aud-20) | P2 | [WP-063](phase-05-audio.md#wp-063) | Sol |
| AUD-21 | [SpectrogramView](../proposed/06-audio-music.md#aud-21) | P2 | [WP-063](phase-05-audio.md#wp-063) | Sol |
| AUD-22 | [PianoRoll](../proposed/06-audio-music.md#aud-22) | P2 | [WP-064](phase-05-audio.md#wp-064) | Sol |
| AUD-23 | [MidiControllerLane](../proposed/06-audio-music.md#aud-23) | P2 | [WP-064](phase-05-audio.md#wp-064) | Sol |
| AUD-24 | [StepSequencer](../proposed/06-audio-music.md#aud-24) | P2 | [WP-064](phase-05-audio.md#wp-064) | Sol |
| AUD-25 | [ClipLauncher](../proposed/06-audio-music.md#aud-25) | P2 | [WP-064](phase-05-audio.md#wp-064) | Sol |
| AUD-26 | [TakeCompView](../proposed/06-audio-music.md#aud-26) | P2 | [WP-065](phase-05-audio.md#wp-065) | Sol |
| AUD-27 | [ScoreView](../proposed/06-audio-music.md#aud-27) | P2 | [WP-067](phase-05-audio.md#wp-067) | Sol |
| AUD-28 | [PitchEditor](../proposed/06-audio-music.md#aud-28) | P3 | [WP-068](phase-05-audio.md#wp-068) | Sol |
| AUD-29 | [LoudnessHistory](../proposed/06-audio-music.md#aud-29) | P2 | [WP-061](phase-05-audio.md#wp-061) | Sol |
| CAN-01 | [CanvasView](../proposed/04-canvas-color.md#can-01) | P1 | [WP-040](phase-03-authoring.md#wp-040) | Sol |
| CAN-02 | [ToolController](../proposed/04-canvas-color.md#can-02) | P1 | [WP-040](phase-03-authoring.md#wp-040) | Sol |
| CAN-03 | [HitTestService](../proposed/04-canvas-color.md#can-03) | P1 | [WP-040](phase-03-authoring.md#wp-040) | Sol |
| CAN-04 | [SnapModel](../proposed/04-canvas-color.md#can-04) | P1 | [WP-041](phase-03-authoring.md#wp-041) | Sol |
| CAN-05 | [SelectionOverlay](../proposed/04-canvas-color.md#can-05) | P1 | [WP-041](phase-03-authoring.md#wp-041) | Sol |
| CAN-06 | [TransformHandles](../proposed/04-canvas-color.md#can-06) | P1 | [WP-041](phase-03-authoring.md#wp-041) | Sol |
| CAN-07 | [GuideLayer](../proposed/04-canvas-color.md#can-07) | P1 | [WP-041](phase-03-authoring.md#wp-041) | Sol |
| CAN-08 | [ArtboardView](../proposed/04-canvas-color.md#can-08) | P2 | [WP-042](phase-03-authoring.md#wp-042) | Luna |
| CAN-09 | [LayerTree](../proposed/04-canvas-color.md#can-09) | P1 | [WP-042](phase-03-authoring.md#wp-042) | Luna |
| CAN-10 | [MaskChannelPanel](../proposed/04-canvas-color.md#can-10) | P2 | [WP-045](phase-03-authoring.md#wp-045) | Sol |
| CAN-11 | [ColorContext](../proposed/04-canvas-color.md#can-11) | P1 | [WP-043](phase-03-authoring.md#wp-043) | Sol |
| CAN-12 | [ColorPicker](../proposed/04-canvas-color.md#can-12) | P1 | [WP-043](phase-03-authoring.md#wp-043) | Sol |
| CAN-13 | [GradientEditor](../proposed/04-canvas-color.md#can-13) | P2 | [WP-043](phase-03-authoring.md#wp-043) | Sol |
| CAN-14 | [CurveEditor](../proposed/04-canvas-color.md#can-14) | P1 | [WP-043](phase-03-authoring.md#wp-043) | Sol |
| CAN-15 | [BrushPresetView](../proposed/04-canvas-color.md#can-15) | P2 | [WP-045](phase-03-authoring.md#wp-045) | Sol |
| CAN-16 | [BrushDynamicsEditor](../proposed/04-canvas-color.md#can-16) | P2 | [WP-045](phase-03-authoring.md#wp-045) | Sol |
| CAN-17 | [StrokeStyleEditor](../proposed/04-canvas-color.md#can-17) | P2 | [WP-044](phase-03-authoring.md#wp-044) | Sol |
| CAN-18 | [FillStyleEditor](../proposed/04-canvas-color.md#can-18) | P2 | [WP-044](phase-03-authoring.md#wp-044) | Sol |
| CAN-19 | [PathEditor](../proposed/04-canvas-color.md#can-19) | P2 | [WP-044](phase-03-authoring.md#wp-044) | Sol |
| CAN-20 | [ShapePalette](../proposed/04-canvas-color.md#can-20) | P2 | [WP-044](phase-03-authoring.md#wp-044) | Sol |
| CAN-21 | [AlignDistributePanel](../proposed/04-canvas-color.md#can-21) | P1 | [WP-042](phase-03-authoring.md#wp-042) | Luna |
| CAN-22 | [NavigatorView](../proposed/04-canvas-color.md#can-22) | P1 | [WP-042](phase-03-authoring.md#wp-042) | Luna |
| CAN-23 | [CompareView](../proposed/04-canvas-color.md#can-23) | P2 | [WP-045](phase-03-authoring.md#wp-045) | Sol |
| CAN-24 | [AppearanceStack](../proposed/04-canvas-color.md#can-24) | P2 | [WP-044](phase-03-authoring.md#wp-044) | Sol |
| CTL-01 | [SplitButton](../proposed/02-controls-navigation.md#ctl-01) | P1 | [WP-020](phase-02-controls-data.md#wp-020) | Luna |
| CTL-02 | [MenuButton](../proposed/02-controls-navigation.md#ctl-02) | P1 | [WP-020](phase-02-controls-data.md#wp-020) | Luna |
| CTL-03 | [ToolShelf](../proposed/02-controls-navigation.md#ctl-03) | P1 | [WP-020](phase-02-controls-data.md#wp-020) | Luna |
| CTL-04 | [Ribbon](../proposed/02-controls-navigation.md#ctl-04) | P2 | [WP-021](phase-02-controls-data.md#wp-021) | Luna |
| CTL-05 | [CommandBar](../proposed/02-controls-navigation.md#ctl-05) | P1 | [WP-020](phase-02-controls-data.md#wp-020) | Luna |
| CTL-06 | [OverflowToolBar](../proposed/02-controls-navigation.md#ctl-06) | P1 | [WP-020](phase-02-controls-data.md#wp-020) | Luna |
| CTL-07 | [DocumentTabs](../proposed/02-controls-navigation.md#ctl-07) | P1 | [WP-021](phase-02-controls-data.md#wp-021) | Luna |
| CTL-08 | [NavigationRail](../proposed/02-controls-navigation.md#ctl-08) | P1 | [WP-021](phase-02-controls-data.md#wp-021) | Luna |
| CTL-09 | [BreadcrumbBar](../proposed/02-controls-navigation.md#ctl-09) | P1 | [WP-021](phase-02-controls-data.md#wp-021) | Luna |
| CTL-10 | [ShortcutEditor](../proposed/02-controls-navigation.md#ctl-10) | P1 | [WP-025](phase-02-controls-data.md#wp-025) | Luna |
| CTL-11 | [PreferencesDialog](../proposed/02-controls-navigation.md#ctl-11) | P1 | [WP-025](phase-02-controls-data.md#wp-025) | Luna |
| CTL-12 | [Inspector](../proposed/02-controls-navigation.md#ctl-12) | P1 | [WP-022](phase-02-controls-data.md#wp-022) | Luna |
| CTL-13 | [MixedValueField](../proposed/02-controls-navigation.md#ctl-13) | P1 | [WP-022](phase-02-controls-data.md#wp-022) | Luna |
| CTL-14 | [UnitField](../proposed/02-controls-navigation.md#ctl-14) | P1 | [WP-022](phase-02-controls-data.md#wp-022) | Luna |
| CTL-15 | [ExpressionField](../proposed/02-controls-navigation.md#ctl-15) | P2 | [WP-023](phase-02-controls-data.md#wp-023) | Sol |
| CTL-16 | [VectorField](../proposed/02-controls-navigation.md#ctl-16) | P1 | [WP-022](phase-02-controls-data.md#wp-022) | Luna |
| CTL-17 | [TransformEditor](../proposed/02-controls-navigation.md#ctl-17) | P1 | [WP-022](phase-02-controls-data.md#wp-022) | Luna |
| CTL-18 | [FontPicker](../proposed/02-controls-navigation.md#ctl-18) | P1 | [WP-024](phase-02-controls-data.md#wp-024) | Luna |
| CTL-19 | [FontFeaturesEditor](../proposed/02-controls-navigation.md#ctl-19) | P2 | [WP-024](phase-02-controls-data.md#wp-024) | Luna |
| CTL-20 | [FilePathField](../proposed/02-controls-navigation.md#ctl-20) | P1 | [WP-023](phase-02-controls-data.md#wp-023) | Sol |
| CTL-21 | [TokenField](../proposed/02-controls-navigation.md#ctl-21) | P1 | [WP-023](phase-02-controls-data.md#wp-023) | Sol |
| CTL-22 | [DateTimeField](../proposed/02-controls-navigation.md#ctl-22) | P2 | [WP-024](phase-02-controls-data.md#wp-024) | Luna |
| CTL-23 | [CalendarPicker](../proposed/02-controls-navigation.md#ctl-23) | P2 | [WP-024](phase-02-controls-data.md#wp-024) | Luna |
| CTL-24 | [FormGroup](../proposed/02-controls-navigation.md#ctl-24) | P1 | [WP-022](phase-02-controls-data.md#wp-022) | Luna |
| CTL-25 | [FindReplaceBar](../proposed/02-controls-navigation.md#ctl-25) | P1 | [WP-025](phase-02-controls-data.md#wp-025) | Luna |
| CTL-26 | [NotificationCenter](../proposed/02-controls-navigation.md#ctl-26) | P1 | [WP-026](phase-02-controls-data.md#wp-026) | Luna |
| CTL-27 | [Toast](../proposed/02-controls-navigation.md#ctl-27) | P1 | [WP-026](phase-02-controls-data.md#wp-026) | Luna |
| CTL-28 | [TaskCenter](../proposed/02-controls-navigation.md#ctl-28) | P1 | [WP-026](phase-02-controls-data.md#wp-026) | Luna |
| DAT-01 | [VirtualList](../proposed/03-data-views.md#dat-01) | P1 | [WP-027](phase-02-controls-data.md#wp-027) | Sol |
| DAT-02 | [VirtualTree](../proposed/03-data-views.md#dat-02) | P1 | [WP-027](phase-02-controls-data.md#wp-027) | Sol |
| DAT-03 | [AssetGrid](../proposed/03-data-views.md#dat-03) | P1 | [WP-027](phase-02-controls-data.md#wp-027) | Sol |
| DAT-04 | [EditableGrid](../proposed/03-data-views.md#dat-04) | P1 | [WP-028](phase-02-controls-data.md#wp-028) | Sol |
| DAT-05 | [HeaderView](../proposed/03-data-views.md#dat-05) | P1 | [WP-028](phase-02-controls-data.md#wp-028) | Sol |
| DAT-06 | [RangeSelectionOverlay](../proposed/03-data-views.md#dat-06) | P1 | [WP-028](phase-02-controls-data.md#wp-028) | Sol |
| DAT-07 | [FilterBar](../proposed/03-data-views.md#dat-07) | P1 | [WP-029](phase-02-controls-data.md#wp-029) | Sol |
| DAT-08 | [QueryBuilder](../proposed/03-data-views.md#dat-08) | P2 | [WP-029](phase-02-controls-data.md#wp-029) | Sol |
| DAT-09 | [PivotView](../proposed/03-data-views.md#dat-09) | P2 | [WP-029](phase-02-controls-data.md#wp-029) | Sol |
| DAT-10 | [ChartView](../proposed/03-data-views.md#dat-10) | P1 | [WP-030](phase-02-controls-data.md#wp-030) | Sol |
| DAT-11 | [ChartAxis](../proposed/03-data-views.md#dat-11) | P1 | [WP-030](phase-02-controls-data.md#wp-030) | Sol |
| DAT-12 | [ChartSeries](../proposed/03-data-views.md#dat-12) | P1 | [WP-030](phase-02-controls-data.md#wp-030) | Sol |
| DAT-13 | [ChartLegend](../proposed/03-data-views.md#dat-13) | P1 | [WP-030](phase-02-controls-data.md#wp-030) | Sol |
| DAT-14 | [Heatmap](../proposed/03-data-views.md#dat-14) | P2 | [WP-031](phase-02-controls-data.md#wp-031) | Sol |
| DAT-15 | [Histogram](../proposed/03-data-views.md#dat-15) | P1 | [WP-030](phase-02-controls-data.md#wp-030) | Sol |
| DAT-16 | [DistributionPlot](../proposed/03-data-views.md#dat-16) | P2 | [WP-031](phase-02-controls-data.md#wp-031) | Sol |
| DAT-17 | [GanttView](../proposed/03-data-views.md#dat-17) | P2 | [WP-031](phase-02-controls-data.md#wp-031) | Sol |
| DAT-18 | [EventLogView](../proposed/03-data-views.md#dat-18) | P1 | [WP-032](phase-02-controls-data.md#wp-032) | Luna |
| DAT-19 | [DiffView](../proposed/03-data-views.md#dat-19) | P2 | [WP-032](phase-02-controls-data.md#wp-032) | Luna |
| DAT-20 | [ResourceTable](../proposed/03-data-views.md#dat-20) | P2 | [WP-032](phase-02-controls-data.md#wp-032) | Luna |
| EXT-01 | [CodeEditor](../proposed/09-specialized-domains.md#ext-01) | P2 | [WP-081](phase-07-integrations.md#wp-081) | Sol |
| EXT-02 | [TerminalView](../proposed/09-specialized-domains.md#ext-02) | P3 | [WP-081](phase-07-integrations.md#wp-081) | Sol |
| EXT-03 | [DiagramCanvas](../proposed/09-specialized-domains.md#ext-03) | P2 | [WP-083](phase-07-integrations.md#wp-083) | Sol |
| EXT-04 | [CadViewport](../proposed/09-specialized-domains.md#ext-04) | P3 | [WP-083](phase-07-integrations.md#wp-083) | Sol |
| EXT-05 | [MapView](../proposed/09-specialized-domains.md#ext-05) | P3 | [WP-084](phase-07-integrations.md#wp-084) | Sol |
| EXT-06 | [ScientificPlot](../proposed/09-specialized-domains.md#ext-06) | P3 | [WP-085](phase-07-integrations.md#wp-085) | Sol |
| EXT-07 | [ImageSliceView](../proposed/09-specialized-domains.md#ext-07) | P3 | [WP-086](phase-07-integrations.md#wp-086) | Sol |
| EXT-08 | [VolumeView](../proposed/09-specialized-domains.md#ext-08) | P3 | [WP-086](phase-07-integrations.md#wp-086) | Sol |
| EXT-09 | [TransferFunctionEditor](../proposed/09-specialized-domains.md#ext-09) | P3 | [WP-086](phase-07-integrations.md#wp-086) | Sol |
| EXT-10 | [AssetDependencyView](../proposed/09-specialized-domains.md#ext-10) | P2 | [WP-087](phase-07-integrations.md#wp-087) | Sol |
| EXT-11 | [TraceView](../proposed/09-specialized-domains.md#ext-11) | P3 | [WP-087](phase-07-integrations.md#wp-087) | Sol |
| OFF-01 | [RichTextEditor](../proposed/05-documents-office.md#off-01) | P1 | [WP-050](phase-04-office.md#wp-050) | Sol |
| OFF-02 | [RichDocumentView](../proposed/05-documents-office.md#off-02) | P1 | [WP-050](phase-04-office.md#wp-050) | Sol |
| OFF-03 | [FormatBar](../proposed/05-documents-office.md#off-03) | P1 | [WP-050](phase-04-office.md#wp-050) | Sol |
| OFF-04 | [StyleManager](../proposed/05-documents-office.md#off-04) | P2 | [WP-051](phase-04-office.md#wp-051) | Sol |
| OFF-05 | [PageNavigator](../proposed/05-documents-office.md#off-05) | P1 | [WP-050](phase-04-office.md#wp-050) | Sol |
| OFF-06 | [PageSetupDialog](../proposed/05-documents-office.md#off-06) | P2 | [WP-051](phase-04-office.md#wp-051) | Sol |
| OFF-07 | [TextFrameEditor](../proposed/05-documents-office.md#off-07) | P2 | [WP-051](phase-04-office.md#wp-051) | Sol |
| OFF-08 | [TableEditor](../proposed/05-documents-office.md#off-08) | P2 | [WP-051](phase-04-office.md#wp-051) | Sol |
| OFF-09 | [EquationEditor](../proposed/05-documents-office.md#off-09) | P2 | [WP-052](phase-04-office.md#wp-052) | Sol |
| OFF-10 | [ReferenceManager](../proposed/05-documents-office.md#off-10) | P2 | [WP-052](phase-04-office.md#wp-052) | Sol |
| OFF-11 | [ReviewPane](../proposed/05-documents-office.md#off-11) | P2 | [WP-052](phase-04-office.md#wp-052) | Sol |
| OFF-12 | [PrintPreview](../proposed/05-documents-office.md#off-12) | P1 | [WP-053](phase-04-office.md#wp-053) | Sol |
| OFF-13 | [PreflightPanel](../proposed/05-documents-office.md#off-13) | P2 | [WP-053](phase-04-office.md#wp-053) | Sol |
| OFF-14 | [SheetView](../proposed/05-documents-office.md#off-14) | P1 | [WP-055](phase-04-office.md#wp-055) | Sol |
| OFF-15 | [FormulaBar](../proposed/05-documents-office.md#off-15) | P1 | [WP-055](phase-04-office.md#wp-055) | Sol |
| OFF-16 | [WorkbookTabs](../proposed/05-documents-office.md#off-16) | P1 | [WP-055](phase-04-office.md#wp-055) | Sol |
| OFF-17 | [ConditionalFormatEditor](../proposed/05-documents-office.md#off-17) | P2 | [WP-055](phase-04-office.md#wp-055) | Sol |
| OFF-18 | [DataValidationEditor](../proposed/05-documents-office.md#off-18) | P2 | [WP-055](phase-04-office.md#wp-055) | Sol |
| OFF-19 | [SlideCanvas](../proposed/05-documents-office.md#off-19) | P1 | [WP-056](phase-04-office.md#wp-056) | Sol |
| OFF-20 | [SlideSorter](../proposed/05-documents-office.md#off-20) | P1 | [WP-056](phase-04-office.md#wp-056) | Sol |
| OFF-21 | [SlideMasterEditor](../proposed/05-documents-office.md#off-21) | P2 | [WP-056](phase-04-office.md#wp-056) | Sol |
| OFF-22 | [AnimationPane](../proposed/05-documents-office.md#off-22) | P2 | [WP-056](phase-04-office.md#wp-056) | Sol |
| OFF-23 | [PresenterView](../proposed/05-documents-office.md#off-23) | P2 | [WP-056](phase-04-office.md#wp-056) | Sol |
| OFF-24 | [PdfView](../proposed/05-documents-office.md#off-24) | P1 | [WP-054](phase-04-office.md#wp-054) | Sol |
| OFF-25 | [PdfAnnotationTools](../proposed/05-documents-office.md#off-25) | P2 | [WP-054](phase-04-office.md#wp-054) | Sol |
| OFF-26 | [NotebookView](../proposed/05-documents-office.md#off-26) | P2 | [WP-057](phase-04-office.md#wp-057) | Sol |
| OFF-27 | [BlockEditor](../proposed/05-documents-office.md#off-27) | P2 | [WP-057](phase-04-office.md#wp-057) | Sol |
| OFF-28 | [MailView](../proposed/05-documents-office.md#off-28) | P2 | [WP-058](phase-04-office.md#wp-058) | Sol |
| OFF-29 | [RichMailComposer](../proposed/05-documents-office.md#off-29) | P2 | [WP-058](phase-04-office.md#wp-058) | Sol |
| OFF-30 | [CalendarView](../proposed/05-documents-office.md#off-30) | P2 | [WP-058](phase-04-office.md#wp-058) | Sol |
| OFF-31 | [RecurrenceEditor](../proposed/05-documents-office.md#off-31) | P2 | [WP-058](phase-04-office.md#wp-058) | Sol |
| OFF-32 | [TaskBoard](../proposed/05-documents-office.md#off-32) | P2 | [WP-058](phase-04-office.md#wp-058) | Sol |
| OFF-33 | [FormDesigner](../proposed/05-documents-office.md#off-33) | P2 | [WP-059](phase-04-office.md#wp-059) | Sol |
| OFF-34 | [DatabaseSchemaView](../proposed/05-documents-office.md#off-34) | P2 | [WP-059](phase-04-office.md#wp-059) | Sol |
| OFF-35 | [ReportDesigner](../proposed/05-documents-office.md#off-35) | P2 | [WP-059](phase-04-office.md#wp-059) | Sol |
| PLT-01 | [WindowHost](../proposed/08-platform-integrations.md#plt-01) | P1 | [WP-018](phase-01-services.md#wp-018) | Sol |
| PLT-02 | [DetachedPanelHost](../proposed/08-platform-integrations.md#plt-02) | P2 | [WP-080](phase-07-integrations.md#wp-080) | Sol |
| PLT-03 | [FileDialogAdapter](../proposed/08-platform-integrations.md#plt-03) | P1 | [WP-018](phase-01-services.md#wp-018) | Sol |
| PLT-04 | [PrintAdapter](../proposed/08-platform-integrations.md#plt-04) | P2 | [WP-053](phase-04-office.md#wp-053) | Sol |
| PLT-05 | [DesktopIntegration](../proposed/08-platform-integrations.md#plt-05) | P2 | [WP-018](phase-01-services.md#wp-018) | Sol |
| PLT-06 | [AccessibilityAdapter](../proposed/08-platform-integrations.md#plt-06) | P0 | [WP-017](phase-01-services.md#wp-017) | Sol |
| PLT-07 | [LocaleService](../proposed/08-platform-integrations.md#plt-07) | P1 | [WP-017](phase-01-services.md#wp-017) | Sol |
| PLT-08 | [FocusManager](../proposed/08-platform-integrations.md#plt-08) | P0 | [WP-011](phase-01-services.md#wp-011) | Sol |
| PLT-09 | [GestureRouter](../proposed/08-platform-integrations.md#plt-09) | P1 | [WP-017](phase-01-services.md#wp-017) | Sol |
| PLT-10 | [RenderSurfaceAdapter](../proposed/08-platform-integrations.md#plt-10) | P1 | [WP-040](phase-03-authoring.md#wp-040) | Sol |
| PLT-11 | [PluginHostAdapter](../proposed/08-platform-integrations.md#plt-11) | P2 | [WP-066](phase-05-audio.md#wp-066) | Sol |
| PLT-12 | [ExtensionRegistry](../proposed/08-platform-integrations.md#plt-12) | P2 | [WP-081](phase-07-integrations.md#wp-081) | Sol |
| PLT-13 | [ScriptConsole](../proposed/08-platform-integrations.md#plt-13) | P2 | [WP-081](phase-07-integrations.md#wp-081) | Sol |
| PLT-14 | [CollaborationAdapter](../proposed/08-platform-integrations.md#plt-14) | P3 | [WP-082](phase-07-integrations.md#wp-082) | Sol |
| PLT-15 | [PresenceLayer](../proposed/08-platform-integrations.md#plt-15) | P3 | [WP-082](phase-07-integrations.md#wp-082) | Sol |
| PLT-16 | [VersionBrowser](../proposed/08-platform-integrations.md#plt-16) | P2 | [WP-082](phase-07-integrations.md#wp-082) | Sol |
| PLT-17 | [ImportDialog](../proposed/08-platform-integrations.md#plt-17) | P1 | [WP-019](phase-01-services.md#wp-019) | Sol |
| PLT-18 | [ExportDialog](../proposed/08-platform-integrations.md#plt-18) | P1 | [WP-019](phase-01-services.md#wp-019) | Sol |
| PLT-19 | [DeviceSettingsPanel](../proposed/08-platform-integrations.md#plt-19) | P2 | [WP-060](phase-05-audio.md#wp-060) | Sol |
| PLT-20 | [FormatRegistry](../proposed/08-platform-integrations.md#plt-20) | P1 | [WP-019](phase-01-services.md#wp-019) | Sol |
| VID-01 | [SourceProgramMonitor](../proposed/07-video-motion.md#vid-01) | P1 | [WP-070](phase-06-video.md#wp-070) | Sol |
| VID-02 | [MediaBin](../proposed/07-video-motion.md#vid-02) | P1 | [WP-070](phase-06-video.md#wp-070) | Sol |
| VID-03 | [TrimView](../proposed/07-video-motion.md#vid-03) | P2 | [WP-071](phase-06-video.md#wp-071) | Sol |
| VID-04 | [MulticamView](../proposed/07-video-motion.md#vid-04) | P2 | [WP-071](phase-06-video.md#wp-071) | Sol |
| VID-05 | [TransitionEditor](../proposed/07-video-motion.md#vid-05) | P2 | [WP-071](phase-06-video.md#wp-071) | Sol |
| VID-06 | [CaptionEditor](../proposed/07-video-motion.md#vid-06) | P2 | [WP-072](phase-06-video.md#wp-072) | Sol |
| VID-07 | [VideoScopes](../proposed/07-video-motion.md#vid-07) | P2 | [WP-072](phase-06-video.md#wp-072) | Sol |
| VID-08 | [ColorWheels](../proposed/07-video-motion.md#vid-08) | P2 | [WP-072](phase-06-video.md#wp-072) | Sol |
| VID-09 | [EffectBrowser](../proposed/07-video-motion.md#vid-09) | P2 | [WP-073](phase-06-video.md#wp-073) | Sol |
| VID-10 | [KeyframeTimeline](../proposed/07-video-motion.md#vid-10) | P1 | [WP-073](phase-06-video.md#wp-073) | Sol |
| VID-11 | [DopeSheet](../proposed/07-video-motion.md#vid-11) | P2 | [WP-073](phase-06-video.md#wp-073) | Sol |
| VID-12 | [CompositionView](../proposed/07-video-motion.md#vid-12) | P2 | [WP-073](phase-06-video.md#wp-073) | Sol |
| VID-13 | [NodeGraph](../proposed/07-video-motion.md#vid-13) | P2 | [WP-074](phase-06-video.md#wp-074) | Sol |
| VID-14 | [MotionPathOverlay](../proposed/07-video-motion.md#vid-14) | P2 | [WP-073](phase-06-video.md#wp-073) | Sol |
| VID-15 | [TrackingPanel](../proposed/07-video-motion.md#vid-15) | P3 | [WP-075](phase-06-video.md#wp-075) | Sol |
| VID-16 | [RotoscopeEditor](../proposed/07-video-motion.md#vid-16) | P3 | [WP-075](phase-06-video.md#wp-075) | Sol |
| VID-17 | [OnionSkinView](../proposed/07-video-motion.md#vid-17) | P2 | [WP-073](phase-06-video.md#wp-073) | Sol |
| VID-18 | [RigEditor](../proposed/07-video-motion.md#vid-18) | P3 | [WP-077](phase-06-video.md#wp-077) | Sol |
| VID-19 | [SceneView3D](../proposed/07-video-motion.md#vid-19) | P2 | [WP-076](phase-06-video.md#wp-076) | Sol |
| VID-20 | [SceneOutliner](../proposed/07-video-motion.md#vid-20) | P2 | [WP-076](phase-06-video.md#wp-076) | Sol |
| VID-21 | [TransformGizmo3D](../proposed/07-video-motion.md#vid-21) | P2 | [WP-076](phase-06-video.md#wp-076) | Sol |
| VID-22 | [MaterialPreview](../proposed/07-video-motion.md#vid-22) | P2 | [WP-076](phase-06-video.md#wp-076) | Sol |
| VID-23 | [UVEditor](../proposed/07-video-motion.md#vid-23) | P3 | [WP-077](phase-06-video.md#wp-077) | Sol |
| VID-24 | [TexturePaintView](../proposed/07-video-motion.md#vid-24) | P3 | [WP-077](phase-06-video.md#wp-077) | Sol |
| VID-25 | [RenderQueueView](../proposed/07-video-motion.md#vid-25) | P2 | [WP-078](phase-06-video.md#wp-078) | Sol |

## Category reconciliation

| Category | Proposal count |
| --- | --- |
| APP | 20 |
| AUD | 29 |
| CAN | 24 |
| CTL | 28 |
| DAT | 20 |
| EXT | 11 |
| OFF | 35 |
| PLT | 20 |
| VID | 25 |
| **Total** | **212** |

Structural verification: every proposal appears once, every owner exists,
every dependency resolves, and the package graph is acyclic. Runtime
completion is tracked separately; this table does not claim implementation.
