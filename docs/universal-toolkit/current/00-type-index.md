<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Current type index

Snapshot: 2026-09-22; repository HEAD `89d5ca39a13c974d2877597a39d0fe6db88fbaf0`, including local source changes.

Coverage: **78 main-module QML files + 21 C++ catalog entries + stylusSample + QindaQtTheme = 101 described entries**. Of the 21 C++ entries, 13 have QML names and 8 are anonymous attached/theme helpers. These numbers are not a count of independent widgets. Internal dock renderers are included because CMake exports every QML file.

The original generator does not include the bridge or value type and omits some pointer properties/multiline methods. This reference supplements those omissions from headers. Inherited framework APIs remain documented by Qt.

[Book index](../README.md) · [Tools and implementation helpers](10-tools-tests.md)

| Entry | Category | Kind |
| --- | --- | --- |
| [AppWindow](05-shell-navigation.md#appwindow) | Application shell, menus and navigation | QML component |
| [Badge](03-text-actions.md#badge) | Text, actions and indicators | QML component |
| [Box](02-layout.md#box) | Layout, scrolling and geometry | QML component |
| [Button](03-text-actions.md#button) | Text, actions and indicators | QML component |
| [Caption](03-text-actions.md#caption) | Text, actions and indicators | QML component |
| [Card](06-structure-feedback.md#card) | Panels, rows, dialogs and feedback | QML component |
| [CheckBox](04-inputs.md#checkbox) | Text, numeric, choice and color input | QML component |
| [Chip](03-text-actions.md#chip) | Text, actions and indicators | QML component |
| [ColorButton](04-inputs.md#colorbutton) | Text, numeric, choice and color input | QML component |
| [ColorField](04-inputs.md#colorfield) | Text, numeric, choice and color input | QML component |
| [ColorSwatches](04-inputs.md#colorswatches) | Text, numeric, choice and color input | QML component |
| [ComboBox](04-inputs.md#combobox) | Text, numeric, choice and color input | QML component |
| [CommandPalette](05-shell-navigation.md#commandpalette) | Application shell, menus and navigation | QML component |
| [DataTable](07-data-telemetry.md#datatable) | Tables and telemetry displays | QML component |
| [Density](01-foundations.md#density) | Theme, density, icons and desktop bridge | C++ singleton |
| [Dialog](06-structure-feedback.md#dialog) | Panels, rows, dialogs and feedback | QML component |
| [Divider](02-layout.md#divider) | Layout, scrolling and geometry | QML component |
| [DockDivider](09-docking.md#dockdivider) | Docking model and renderers | QML component; internal renderer |
| [DockDropOverlay](09-docking.md#dockdropoverlay) | Docking model and renderers | QML component; internal renderer |
| [DockFloating](09-docking.md#dockfloating) | Docking model and renderers | QML component; internal renderer |
| [DockFrame](09-docking.md#dockframe) | Docking model and renderers | QML component; internal renderer |
| [DockHost](09-docking.md#dockhost) | Docking model and renderers | QML component |
| [DockModel](09-docking.md#dockmodel) | Docking model and renderers | C++ type |
| [DockPanel](09-docking.md#dockpanel) | Docking model and renderers | QML component |
| [DockTabStrip](09-docking.md#docktabstrip) | Docking model and renderers | QML component; internal renderer |
| [DockZone](09-docking.md#dockzone) | Docking model and renderers | QML component; internal renderer |
| [EmptyState](06-structure-feedback.md#emptystate) | Panels, rows, dialogs and feedback | QML component |
| [Filmstrip](08-media-canvas.md#filmstrip) | Media, canvas navigation and pen input | QML component |
| [Flex](02-layout.md#flex) | Layout, scrolling and geometry | C++ type |
| [FlexAttached](02-layout.md#flexattached) | Layout, scrolling and geometry | Anonymous C++ helper |
| [Graph](07-data-telemetry.md#graph) | Tables and telemetry displays | C++ type |
| [GraphSeries](07-data-telemetry.md#graphseries) | Tables and telemetry displays | C++ type |
| [Grid](02-layout.md#grid) | Layout, scrolling and geometry | C++ type |
| [GridAttached](02-layout.md#gridattached) | Layout, scrolling and geometry | Anonymous C++ helper |
| [Heading](03-text-actions.md#heading) | Text, actions and indicators | QML component |
| [Icon](01-foundations.md#icon) | Theme, density, icons and desktop bridge | C++ type |
| [IconButton](03-text-actions.md#iconbutton) | Text, actions and indicators | QML component |
| [Icons](01-foundations.md#icons) | Theme, density, icons and desktop bridge | C++ singleton |
| [Island](06-structure-feedback.md#island) | Panels, rows, dialogs and feedback | QML component |
| [KeyCap](03-text-actions.md#keycap) | Text, actions and indicators | QML component |
| [KeyValue](06-structure-feedback.md#keyvalue) | Panels, rows, dialogs and feedback | QML component |
| [Label](03-text-actions.md#label) | Text, actions and indicators | QML component |
| [LayoutInfo](02-layout.md#layoutinfo) | Layout, scrolling and geometry | C++ singleton |
| [ListRow](06-structure-feedback.md#listrow) | Panels, rows, dialogs and feedback | QML component |
| [Menu](05-shell-navigation.md#menu) | Application shell, menus and navigation | QML component |
| [MenuBar](05-shell-navigation.md#menubar) | Application shell, menus and navigation | QML component |
| [MenuItem](05-shell-navigation.md#menuitem) | Application shell, menus and navigation | QML component |
| [MenuSeparator](05-shell-navigation.md#menuseparator) | Application shell, menus and navigation | QML component |
| [MessageDialog](06-structure-feedback.md#messagedialog) | Panels, rows, dialogs and feedback | QML component |
| [Meter](07-data-telemetry.md#meter) | Tables and telemetry displays | C++ type |
| [Mono](03-text-actions.md#mono) | Text, actions and indicators | QML component |
| [Notice](06-structure-feedback.md#notice) | Panels, rows, dialogs and feedback | QML component |
| [NumberField](04-inputs.md#numberfield) | Text, numeric, choice and color input | QML component |
| [Overline](03-text-actions.md#overline) | Text, actions and indicators | QML component |
| [Panel](06-structure-feedback.md#panel) | Panels, rows, dialogs and feedback | QML component |
| [PillSwitcher](03-text-actions.md#pillswitcher) | Text, actions and indicators | QML component |
| [Popover](06-structure-feedback.md#popover) | Panels, rows, dialogs and feedback | QML component |
| [ProgressBar](03-text-actions.md#progressbar) | Text, actions and indicators | QML component |
| [PromptDialog](06-structure-feedback.md#promptdialog) | Panels, rows, dialogs and feedback | QML component |
| [PropertyGroup](06-structure-feedback.md#propertygroup) | Panels, rows, dialogs and feedback | QML component |
| [PropertyRow](06-structure-feedback.md#propertyrow) | Panels, rows, dialogs and feedback | QML component |
| [QindaQtTheme](01-foundations.md#qindaqttheme) | Theme, density, icons and desktop bridge | QML component |
| [RadioButton](04-inputs.md#radiobutton) | Text, numeric, choice and color input | QML component |
| [RangeSlider](04-inputs.md#rangeslider) | Text, numeric, choice and color input | QML component |
| [Ruler](08-media-canvas.md#ruler) | Media, canvas navigation and pen input | QML component |
| [Scroll](02-layout.md#scroll) | Layout, scrolling and geometry | QML component |
| [ScrollBar](02-layout.md#scrollbar) | Layout, scrolling and geometry | QML component |
| [SearchField](04-inputs.md#searchfield) | Text, numeric, choice and color input | QML component |
| [SectionHeader](06-structure-feedback.md#sectionheader) | Panels, rows, dialogs and feedback | QML component |
| [Segmented](03-text-actions.md#segmented) | Text, actions and indicators | QML component |
| [Slider](04-inputs.md#slider) | Text, numeric, choice and color input | QML component |
| [Spacer](02-layout.md#spacer) | Layout, scrolling and geometry | QML component |
| [Sparkline](07-data-telemetry.md#sparkline) | Tables and telemetry displays | QML component |
| [Spinner](03-text-actions.md#spinner) | Text, actions and indicators | QML component |
| [Splitter](02-layout.md#splitter) | Layout, scrolling and geometry | QML component |
| [Stack](02-layout.md#stack) | Layout, scrolling and geometry | C++ type |
| [StackAttached](02-layout.md#stackattached) | Layout, scrolling and geometry | Anonymous C++ helper |
| [StatusBar](05-shell-navigation.md#statusbar) | Application shell, menus and navigation | QML component |
| [StatusField](05-shell-navigation.md#statusfield) | Application shell, menus and navigation | QML component |
| [StylusHandler](08-media-canvas.md#stylushandler) | Media, canvas navigation and pen input | C++ type |
| [stylusSample](08-media-canvas.md#stylussample) | Media, canvas navigation and pen input | QML value type |
| [Switch](04-inputs.md#switch) | Text, numeric, choice and color input | QML component |
| [TableColumn](07-data-telemetry.md#tablecolumn) | Tables and telemetry displays | QML component |
| [TabStrip](05-shell-navigation.md#tabstrip) | Application shell, menus and navigation | QML component |
| [TextArea](04-inputs.md#textarea) | Text, numeric, choice and color input | QML component |
| [TextField](04-inputs.md#textfield) | Text, numeric, choice and color input | QML component |
| [Theme](01-foundations.md#theme) | Theme, density, icons and desktop bridge | C++ singleton |
| [ThemeColors](01-foundations.md#themecolors) | Theme, density, icons and desktop bridge | Anonymous C++ helper |
| [ThemeFont](01-foundations.md#themefont) | Theme, density, icons and desktop bridge | Anonymous C++ helper |
| [ThemeMetrics](01-foundations.md#thememetrics) | Theme, density, icons and desktop bridge | Anonymous C++ helper |
| [ThemeRamp](01-foundations.md#themeramp) | Theme, density, icons and desktop bridge | Anonymous C++ helper |
| [ThemeRamps](01-foundations.md#themeramps) | Theme, density, icons and desktop bridge | Anonymous C++ helper |
| [Thumbnail](08-media-canvas.md#thumbnail) | Media, canvas navigation and pen input | QML component |
| [TimeRuler](08-media-canvas.md#timeruler) | Media, canvas navigation and pen input | QML component |
| [ToolBar](05-shell-navigation.md#toolbar) | Application shell, menus and navigation | QML component |
| [ToolSeparator](05-shell-navigation.md#toolseparator) | Application shell, menus and navigation | QML component |
| [ToolTip](06-structure-feedback.md#tooltip) | Panels, rows, dialogs and feedback | QML component |
| [TreeRow](06-structure-feedback.md#treerow) | Panels, rows, dialogs and feedback | QML component |
| [Viewport](08-media-canvas.md#viewport) | Media, canvas navigation and pen input | QML component |
| [Waveform](08-media-canvas.md#waveform) | Media, canvas navigation and pen input | QML component |
| [ZoomControl](08-media-canvas.md#zoomcontrol) | Media, canvas navigation and pen input | QML component |
