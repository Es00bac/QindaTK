<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Proposed additions: complete index

**PROPOSED — 212 named additions across 9 categories.** These are a
hypothetical design inventory, not shipped APIs, implementation commitments or
a requirement to build everything before making an application. Extensions to
existing primitives can satisfy a proposal without adding a redundant wrapper.

Every entry links to purpose, contract sketch, dependencies and a completion
check. Read [architecture](00-architecture.md) for common ownership, undo,
accessibility and validation requirements, then use the [roadmap](../roadmap.md)
to choose a concrete vertical slice.

P0 = shared contract/correctness; P1 = broad reuse; P2 = domain completion;
P3 = specialist extension. Engine implementation remains an independent workstream.

| Priority | Entries |
| --- | --- |
| P0 | 13 |
| P1 | 92 |
| P2 | 91 |
| P3 | 16 |

## Application services

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| APP-01 | [CommandRegistry](01-application-services.md#app-01) | P0 |
| APP-02 | [CommandContext](01-application-services.md#app-02) | P0 |
| APP-03 | [ShortcutMap](01-application-services.md#app-03) | P0 |
| APP-04 | [UndoService](01-application-services.md#app-04) | P0 |
| APP-05 | [EditTransaction](01-application-services.md#app-05) | P0 |
| APP-06 | [SelectionModel](01-application-services.md#app-06) | P0 |
| APP-07 | [DocumentSession](01-application-services.md#app-07) | P0 |
| APP-08 | [DocumentManager](01-application-services.md#app-08) | P0 |
| APP-09 | [WorkspaceSession](01-application-services.md#app-09) | P1 |
| APP-10 | [PropertyBinding](01-application-services.md#app-10) | P0 |
| APP-11 | [ValidationModel](01-application-services.md#app-11) | P1 |
| APP-12 | [SettingsModel](01-application-services.md#app-12) | P1 |
| APP-13 | [JobModel](01-application-services.md#app-13) | P1 |
| APP-14 | [AssetProvider](01-application-services.md#app-14) | P1 |
| APP-15 | [ClipboardService](01-application-services.md#app-15) | P1 |
| APP-16 | [DragDropService](01-application-services.md#app-16) | P1 |
| APP-17 | [HistoryView](01-application-services.md#app-17) | P1 |
| APP-18 | [RecoveryManager](01-application-services.md#app-18) | P0 |
| APP-19 | [ResourceResolver](01-application-services.md#app-19) | P1 |
| APP-20 | [CapabilityRegistry](01-application-services.md#app-20) | P1 |

## Controls, navigation and inspectors

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| CTL-01 | [SplitButton](02-controls-navigation.md#ctl-01) | P1 |
| CTL-02 | [MenuButton](02-controls-navigation.md#ctl-02) | P1 |
| CTL-03 | [ToolShelf](02-controls-navigation.md#ctl-03) | P1 |
| CTL-04 | [Ribbon](02-controls-navigation.md#ctl-04) | P2 |
| CTL-05 | [CommandBar](02-controls-navigation.md#ctl-05) | P1 |
| CTL-06 | [OverflowToolBar](02-controls-navigation.md#ctl-06) | P1 |
| CTL-07 | [DocumentTabs](02-controls-navigation.md#ctl-07) | P1 |
| CTL-08 | [NavigationRail](02-controls-navigation.md#ctl-08) | P1 |
| CTL-09 | [BreadcrumbBar](02-controls-navigation.md#ctl-09) | P1 |
| CTL-10 | [ShortcutEditor](02-controls-navigation.md#ctl-10) | P1 |
| CTL-11 | [PreferencesDialog](02-controls-navigation.md#ctl-11) | P1 |
| CTL-12 | [Inspector](02-controls-navigation.md#ctl-12) | P1 |
| CTL-13 | [MixedValueField](02-controls-navigation.md#ctl-13) | P1 |
| CTL-14 | [UnitField](02-controls-navigation.md#ctl-14) | P1 |
| CTL-15 | [ExpressionField](02-controls-navigation.md#ctl-15) | P2 |
| CTL-16 | [VectorField](02-controls-navigation.md#ctl-16) | P1 |
| CTL-17 | [TransformEditor](02-controls-navigation.md#ctl-17) | P1 |
| CTL-18 | [FontPicker](02-controls-navigation.md#ctl-18) | P1 |
| CTL-19 | [FontFeaturesEditor](02-controls-navigation.md#ctl-19) | P2 |
| CTL-20 | [FilePathField](02-controls-navigation.md#ctl-20) | P1 |
| CTL-21 | [TokenField](02-controls-navigation.md#ctl-21) | P1 |
| CTL-22 | [DateTimeField](02-controls-navigation.md#ctl-22) | P2 |
| CTL-23 | [CalendarPicker](02-controls-navigation.md#ctl-23) | P2 |
| CTL-24 | [FormGroup](02-controls-navigation.md#ctl-24) | P1 |
| CTL-25 | [FindReplaceBar](02-controls-navigation.md#ctl-25) | P1 |
| CTL-26 | [NotificationCenter](02-controls-navigation.md#ctl-26) | P1 |
| CTL-27 | [Toast](02-controls-navigation.md#ctl-27) | P1 |
| CTL-28 | [TaskCenter](02-controls-navigation.md#ctl-28) | P1 |

## Data views and visualization

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| DAT-01 | [VirtualList](03-data-views.md#dat-01) | P1 |
| DAT-02 | [VirtualTree](03-data-views.md#dat-02) | P1 |
| DAT-03 | [AssetGrid](03-data-views.md#dat-03) | P1 |
| DAT-04 | [EditableGrid](03-data-views.md#dat-04) | P1 |
| DAT-05 | [HeaderView](03-data-views.md#dat-05) | P1 |
| DAT-06 | [RangeSelectionOverlay](03-data-views.md#dat-06) | P1 |
| DAT-07 | [FilterBar](03-data-views.md#dat-07) | P1 |
| DAT-08 | [QueryBuilder](03-data-views.md#dat-08) | P2 |
| DAT-09 | [PivotView](03-data-views.md#dat-09) | P2 |
| DAT-10 | [ChartView](03-data-views.md#dat-10) | P1 |
| DAT-11 | [ChartAxis](03-data-views.md#dat-11) | P1 |
| DAT-12 | [ChartSeries](03-data-views.md#dat-12) | P1 |
| DAT-13 | [ChartLegend](03-data-views.md#dat-13) | P1 |
| DAT-14 | [Heatmap](03-data-views.md#dat-14) | P2 |
| DAT-15 | [Histogram](03-data-views.md#dat-15) | P1 |
| DAT-16 | [DistributionPlot](03-data-views.md#dat-16) | P2 |
| DAT-17 | [GanttView](03-data-views.md#dat-17) | P2 |
| DAT-18 | [EventLogView](03-data-views.md#dat-18) | P1 |
| DAT-19 | [DiffView](03-data-views.md#dat-19) | P2 |
| DAT-20 | [ResourceTable](03-data-views.md#dat-20) | P2 |

## Canvas, vector, color and image authoring

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| CAN-01 | [CanvasView](04-canvas-color.md#can-01) | P1 |
| CAN-02 | [ToolController](04-canvas-color.md#can-02) | P1 |
| CAN-03 | [HitTestService](04-canvas-color.md#can-03) | P1 |
| CAN-04 | [SnapModel](04-canvas-color.md#can-04) | P1 |
| CAN-05 | [SelectionOverlay](04-canvas-color.md#can-05) | P1 |
| CAN-06 | [TransformHandles](04-canvas-color.md#can-06) | P1 |
| CAN-07 | [GuideLayer](04-canvas-color.md#can-07) | P1 |
| CAN-08 | [ArtboardView](04-canvas-color.md#can-08) | P2 |
| CAN-09 | [LayerTree](04-canvas-color.md#can-09) | P1 |
| CAN-10 | [MaskChannelPanel](04-canvas-color.md#can-10) | P2 |
| CAN-11 | [ColorContext](04-canvas-color.md#can-11) | P1 |
| CAN-12 | [ColorPicker](04-canvas-color.md#can-12) | P1 |
| CAN-13 | [GradientEditor](04-canvas-color.md#can-13) | P2 |
| CAN-14 | [CurveEditor](04-canvas-color.md#can-14) | P1 |
| CAN-15 | [BrushPresetView](04-canvas-color.md#can-15) | P2 |
| CAN-16 | [BrushDynamicsEditor](04-canvas-color.md#can-16) | P2 |
| CAN-17 | [StrokeStyleEditor](04-canvas-color.md#can-17) | P2 |
| CAN-18 | [FillStyleEditor](04-canvas-color.md#can-18) | P2 |
| CAN-19 | [PathEditor](04-canvas-color.md#can-19) | P2 |
| CAN-20 | [ShapePalette](04-canvas-color.md#can-20) | P2 |
| CAN-21 | [AlignDistributePanel](04-canvas-color.md#can-21) | P1 |
| CAN-22 | [NavigatorView](04-canvas-color.md#can-22) | P1 |
| CAN-23 | [CompareView](04-canvas-color.md#can-23) | P2 |
| CAN-24 | [AppearanceStack](04-canvas-color.md#can-24) | P2 |

## Documents, Office and productivity surfaces

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| OFF-01 | [RichTextEditor](05-documents-office.md#off-01) | P1 |
| OFF-02 | [RichDocumentView](05-documents-office.md#off-02) | P1 |
| OFF-03 | [FormatBar](05-documents-office.md#off-03) | P1 |
| OFF-04 | [StyleManager](05-documents-office.md#off-04) | P2 |
| OFF-05 | [PageNavigator](05-documents-office.md#off-05) | P1 |
| OFF-06 | [PageSetupDialog](05-documents-office.md#off-06) | P2 |
| OFF-07 | [TextFrameEditor](05-documents-office.md#off-07) | P2 |
| OFF-08 | [TableEditor](05-documents-office.md#off-08) | P2 |
| OFF-09 | [EquationEditor](05-documents-office.md#off-09) | P2 |
| OFF-10 | [ReferenceManager](05-documents-office.md#off-10) | P2 |
| OFF-11 | [ReviewPane](05-documents-office.md#off-11) | P2 |
| OFF-12 | [PrintPreview](05-documents-office.md#off-12) | P1 |
| OFF-13 | [PreflightPanel](05-documents-office.md#off-13) | P2 |
| OFF-14 | [SheetView](05-documents-office.md#off-14) | P1 |
| OFF-15 | [FormulaBar](05-documents-office.md#off-15) | P1 |
| OFF-16 | [WorkbookTabs](05-documents-office.md#off-16) | P1 |
| OFF-17 | [ConditionalFormatEditor](05-documents-office.md#off-17) | P2 |
| OFF-18 | [DataValidationEditor](05-documents-office.md#off-18) | P2 |
| OFF-19 | [SlideCanvas](05-documents-office.md#off-19) | P1 |
| OFF-20 | [SlideSorter](05-documents-office.md#off-20) | P1 |
| OFF-21 | [SlideMasterEditor](05-documents-office.md#off-21) | P2 |
| OFF-22 | [AnimationPane](05-documents-office.md#off-22) | P2 |
| OFF-23 | [PresenterView](05-documents-office.md#off-23) | P2 |
| OFF-24 | [PdfView](05-documents-office.md#off-24) | P1 |
| OFF-25 | [PdfAnnotationTools](05-documents-office.md#off-25) | P2 |
| OFF-26 | [NotebookView](05-documents-office.md#off-26) | P2 |
| OFF-27 | [BlockEditor](05-documents-office.md#off-27) | P2 |
| OFF-28 | [MailView](05-documents-office.md#off-28) | P2 |
| OFF-29 | [RichMailComposer](05-documents-office.md#off-29) | P2 |
| OFF-30 | [CalendarView](05-documents-office.md#off-30) | P2 |
| OFF-31 | [RecurrenceEditor](05-documents-office.md#off-31) | P2 |
| OFF-32 | [TaskBoard](05-documents-office.md#off-32) | P2 |
| OFF-33 | [FormDesigner](05-documents-office.md#off-33) | P2 |
| OFF-34 | [DatabaseSchemaView](05-documents-office.md#off-34) | P2 |
| OFF-35 | [ReportDesigner](05-documents-office.md#off-35) | P2 |

## Audio, timelines and music

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| AUD-01 | [TimeMap](06-audio-music.md#aud-01) | P0 |
| AUD-02 | [TransportModel](06-audio-music.md#aud-02) | P1 |
| AUD-03 | [TransportBar](06-audio-music.md#aud-03) | P1 |
| AUD-04 | [EditTimeline](06-audio-music.md#aud-04) | P1 |
| AUD-05 | [TrackHeader](06-audio-music.md#aud-05) | P1 |
| AUD-06 | [ClipItem](06-audio-music.md#aud-06) | P1 |
| AUD-07 | [TimeSelection](06-audio-music.md#aud-07) | P1 |
| AUD-08 | [MarkerLane](06-audio-music.md#aud-08) | P1 |
| AUD-09 | [TempoLane](06-audio-music.md#aud-09) | P2 |
| AUD-10 | [Knob](06-audio-music.md#aud-10) | P1 |
| AUD-11 | [Fader](06-audio-music.md#aud-11) | P1 |
| AUD-12 | [AudioMeter](06-audio-music.md#aud-12) | P1 |
| AUD-13 | [ChannelStrip](06-audio-music.md#aud-13) | P2 |
| AUD-14 | [MixerView](06-audio-music.md#aud-14) | P2 |
| AUD-15 | [PanningControl](06-audio-music.md#aud-15) | P2 |
| AUD-16 | [RoutingMatrix](06-audio-music.md#aud-16) | P2 |
| AUD-17 | [PluginRack](06-audio-music.md#aud-17) | P2 |
| AUD-18 | [PluginBrowser](06-audio-music.md#aud-18) | P2 |
| AUD-19 | [AutomationLane](06-audio-music.md#aud-19) | P1 |
| AUD-20 | [WaveformEditor](06-audio-music.md#aud-20) | P2 |
| AUD-21 | [SpectrogramView](06-audio-music.md#aud-21) | P2 |
| AUD-22 | [PianoRoll](06-audio-music.md#aud-22) | P2 |
| AUD-23 | [MidiControllerLane](06-audio-music.md#aud-23) | P2 |
| AUD-24 | [StepSequencer](06-audio-music.md#aud-24) | P2 |
| AUD-25 | [ClipLauncher](06-audio-music.md#aud-25) | P2 |
| AUD-26 | [TakeCompView](06-audio-music.md#aud-26) | P2 |
| AUD-27 | [ScoreView](06-audio-music.md#aud-27) | P2 |
| AUD-28 | [PitchEditor](06-audio-music.md#aud-28) | P3 |
| AUD-29 | [LoudnessHistory](06-audio-music.md#aud-29) | P2 |

## Video, motion graphics and 3D

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| VID-01 | [SourceProgramMonitor](07-video-motion.md#vid-01) | P1 |
| VID-02 | [MediaBin](07-video-motion.md#vid-02) | P1 |
| VID-03 | [TrimView](07-video-motion.md#vid-03) | P2 |
| VID-04 | [MulticamView](07-video-motion.md#vid-04) | P2 |
| VID-05 | [TransitionEditor](07-video-motion.md#vid-05) | P2 |
| VID-06 | [CaptionEditor](07-video-motion.md#vid-06) | P2 |
| VID-07 | [VideoScopes](07-video-motion.md#vid-07) | P2 |
| VID-08 | [ColorWheels](07-video-motion.md#vid-08) | P2 |
| VID-09 | [EffectBrowser](07-video-motion.md#vid-09) | P2 |
| VID-10 | [KeyframeTimeline](07-video-motion.md#vid-10) | P1 |
| VID-11 | [DopeSheet](07-video-motion.md#vid-11) | P2 |
| VID-12 | [CompositionView](07-video-motion.md#vid-12) | P2 |
| VID-13 | [NodeGraph](07-video-motion.md#vid-13) | P2 |
| VID-14 | [MotionPathOverlay](07-video-motion.md#vid-14) | P2 |
| VID-15 | [TrackingPanel](07-video-motion.md#vid-15) | P3 |
| VID-16 | [RotoscopeEditor](07-video-motion.md#vid-16) | P3 |
| VID-17 | [OnionSkinView](07-video-motion.md#vid-17) | P2 |
| VID-18 | [RigEditor](07-video-motion.md#vid-18) | P3 |
| VID-19 | [SceneView3D](07-video-motion.md#vid-19) | P2 |
| VID-20 | [SceneOutliner](07-video-motion.md#vid-20) | P2 |
| VID-21 | [TransformGizmo3D](07-video-motion.md#vid-21) | P2 |
| VID-22 | [MaterialPreview](07-video-motion.md#vid-22) | P2 |
| VID-23 | [UVEditor](07-video-motion.md#vid-23) | P3 |
| VID-24 | [TexturePaintView](07-video-motion.md#vid-24) | P3 |
| VID-25 | [RenderQueueView](07-video-motion.md#vid-25) | P2 |

## Platform, integration and cross-cutting services

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| PLT-01 | [WindowHost](08-platform-integrations.md#plt-01) | P1 |
| PLT-02 | [DetachedPanelHost](08-platform-integrations.md#plt-02) | P2 |
| PLT-03 | [FileDialogAdapter](08-platform-integrations.md#plt-03) | P1 |
| PLT-04 | [PrintAdapter](08-platform-integrations.md#plt-04) | P2 |
| PLT-05 | [DesktopIntegration](08-platform-integrations.md#plt-05) | P2 |
| PLT-06 | [AccessibilityAdapter](08-platform-integrations.md#plt-06) | P0 |
| PLT-07 | [LocaleService](08-platform-integrations.md#plt-07) | P1 |
| PLT-08 | [FocusManager](08-platform-integrations.md#plt-08) | P0 |
| PLT-09 | [GestureRouter](08-platform-integrations.md#plt-09) | P1 |
| PLT-10 | [RenderSurfaceAdapter](08-platform-integrations.md#plt-10) | P1 |
| PLT-11 | [PluginHostAdapter](08-platform-integrations.md#plt-11) | P2 |
| PLT-12 | [ExtensionRegistry](08-platform-integrations.md#plt-12) | P2 |
| PLT-13 | [ScriptConsole](08-platform-integrations.md#plt-13) | P2 |
| PLT-14 | [CollaborationAdapter](08-platform-integrations.md#plt-14) | P3 |
| PLT-15 | [PresenceLayer](08-platform-integrations.md#plt-15) | P3 |
| PLT-16 | [VersionBrowser](08-platform-integrations.md#plt-16) | P2 |
| PLT-17 | [ImportDialog](08-platform-integrations.md#plt-17) | P1 |
| PLT-18 | [ExportDialog](08-platform-integrations.md#plt-18) | P1 |
| PLT-19 | [DeviceSettingsPanel](08-platform-integrations.md#plt-19) | P2 |
| PLT-20 | [FormatRegistry](08-platform-integrations.md#plt-20) | P1 |

## Additional application domains

| ID | Proposed component/service | Priority |
| --- | --- | --- |
| EXT-01 | [CodeEditor](09-specialized-domains.md#ext-01) | P2 |
| EXT-02 | [TerminalView](09-specialized-domains.md#ext-02) | P3 |
| EXT-03 | [DiagramCanvas](09-specialized-domains.md#ext-03) | P2 |
| EXT-04 | [CadViewport](09-specialized-domains.md#ext-04) | P3 |
| EXT-05 | [MapView](09-specialized-domains.md#ext-05) | P3 |
| EXT-06 | [ScientificPlot](09-specialized-domains.md#ext-06) | P3 |
| EXT-07 | [ImageSliceView](09-specialized-domains.md#ext-07) | P3 |
| EXT-08 | [VolumeView](09-specialized-domains.md#ext-08) | P3 |
| EXT-09 | [TransferFunctionEditor](09-specialized-domains.md#ext-09) | P3 |
| EXT-10 | [AssetDependencyView](09-specialized-domains.md#ext-10) | P2 |
| EXT-11 | [TraceView](09-specialized-domains.md#ext-11) | P3 |
