// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TypographyCalendar"
    when: windowShown
    visible: true
    width: 640
    height: 520

    Tk.LocaleService { id: locale; localeName: "en_US"; timeZoneId: "America/Los_Angeles" }
    Tk.FontCatalog { id: installedFonts }
    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.FontPicker {
            id: fontPicker
            width: 280
            tooltip: "Font family"
            family: "Missing Family"
            fontProvider: ({ families: [
                { family: "Inter", installed: true, styles: ["Regular", "Bold"] },
                { family: "Noto Sans", installed: true, styles: ["Regular"] }
            ] })
        }
        Tk.FontFeaturesEditor {
            id: features
            width: 280
            fontFamily: "Inter"
            fontStyle: "Regular"
            fontMetadata: ({ available: true, axisMetadataAvailable: true, featureMetadataAvailable: true,
                axes: [{ tag: "wght", name: "Weight", supported: true, minimum: 100, maximum: 900 }],
                features: [{ tag: "liga", name: "Standard ligatures", supported: false, value: false }] })
        }
        Tk.DateTimeField { id: dateTime; width: 360; localeService: locale; tooltip: "Start time" }
        Tk.CalendarPicker { id: calendar; width: 280; localeService: locale; selectedDate: "2024-02-29" }
    }

    Component { id: pickerComponent; Tk.FontPicker { width: 260; tooltip: "Font" } }
    Component { id: realPickerComponent; Tk.FontPicker { width: 280; fontProvider: installedFonts } }
    Component { id: realFeaturesComponent; Tk.FontFeaturesEditor { width: 280; fontProvider: installedFonts } }
    Component { id: localeComponent; Tk.LocaleService { localeName: "en_US"; timeZoneId: "America/Los_Angeles" } }
    Component { id: datetimeComponent; Tk.DateTimeField { width: 360; dateTimeFormat: "yyyy-MM-dd HH:mm" } }
    Component { id: calendarComponent; Tk.CalendarPicker { width: 280; selectedDate: "2024-02-29" } }
    Component { id: spyComponent; SignalSpy {} }

    function test_font_picker_preserves_missing_family_and_filters_provider_metadata() {
        verify(fontPicker.metadataAvailable)
        compare(fontPicker.selectedMissing, true)
        compare(fontPicker.family, "Missing Family")
        compare(fontPicker.fallbackFamily, "")
        compare(fontPicker.filteredFamilies[0], "Missing Family")
        fontPicker.searchText = "noto"
        compare(fontPicker.filteredFamilies.length, 1)
        compare(fontPicker.filteredFamilies[0], "Noto Sans")
        verify(findChild(fontPicker, "fontAvailabilityState").visible)
        compare(fontPicker.Accessible.name, "Font family")
        compare(findChild(fontPicker, "fontSpecimenText").font.family, Tk.Theme.font.family)
    }

    function test_installed_face_metadata_and_specimen_are_connected() {
        verify(installedFonts.families.length > 0)
        const preferred = installedFonts.resolveFamily("Newsreader")
        const selected = preferred.installed ? preferred : installedFonts.families[0]
        const picker = createTemporaryObject(realPickerComponent, root, { family: selected.family, style: selected.styles[0] })
        const editor = createTemporaryObject(realFeaturesComponent, root,
            { fontFamily: selected.family, fontStyle: selected.styles[0] })
        verify(picker && editor)
        compare(picker.selectedMissing, false)
        compare(findChild(picker, "fontSpecimenText").font.family, selected.family)
        compare(picker.selectedFaceMetadata.family, selected.family)
        compare(editor.effectiveFontMetadata.family, selected.family)
        verify(editor.axisMetadataAvailable || editor.featureMetadataAvailable)
        if (editor.axes.length > 0)
            verify(findChild(editor, "fontAxis_" + editor.axes[0].tag))
        if (editor.features.length > 0)
            verify(findChild(editor, "fontFeature_" + editor.features[0].tag))
        const missing = createTemporaryObject(realPickerComponent, root,
            { family: "QindaTK Fixture Missing Font 89B56" })
        verify(missing.selectedMissing)
        compare(missing.family, "QindaTK Fixture Missing Font 89B56")
        compare(findChild(missing, "fontSpecimenText").font.family, installedFonts.fallbackFamily)
    }

    function test_font_features_show_unsupported_metadata_without_enabling_edit() {
        verify(features.metadataAvailable)
        compare(features.axes.length, 1)
        compare(features.features.length, 1)
        const unsupported = findChild(features, "fontFeature_liga")
        verify(unsupported)
        verify(!unsupported.enabled)
        verify(unsupported.tooltip.includes("Unsupported"))
        features.fontMetadata = null
        verify(!features.metadataAvailable)
        verify(findChild(features, "fontMetadataUnavailable").visible)
    }

    function test_font_feature_mixed_edit_and_reset_are_single_undo_steps() {
        const fixture = inspectorFixtures.create()
        const originalMetadata = features.fontMetadata
        verify(fixture.ready)
        const binding = fixture.featureBinding
        verify(binding.mixed)
        compare(fixture.document.valueFor(fixture.firstId, "liga"), false)
        compare(fixture.document.valueFor(fixture.secondId, "liga"), true)

        features.fontMetadata = ({ available: true, axisMetadataAvailable: true, featureMetadataAvailable: true,
            axes: [], features: [{ tag: "liga", name: "Standard ligatures", supported: true }] })
        features.featureBindings = ({ liga: binding })
        const checkbox = findChild(features, "fontFeature_liga")
        const reset = findChild(features, "fontFeatureReset_liga")
        verify(checkbox && reset)
        compare(checkbox.checkState, Qt.PartiallyChecked)
        verify(checkbox.tristate)
        verify(reset.visible)

        checkbox.click()
        compare(fixture.document.valueFor(fixture.firstId, "liga"), true)
        compare(fixture.document.valueFor(fixture.secondId, "liga"), true)
        verify(!binding.mixed)
        verify(fixture.undoService.undo())
        verify(binding.mixed)
        compare(fixture.document.valueFor(fixture.firstId, "liga"), false)
        compare(fixture.document.valueFor(fixture.secondId, "liga"), true)

        reset.click()
        compare(fixture.document.valueFor(fixture.firstId, "liga"), false)
        compare(fixture.document.valueFor(fixture.secondId, "liga"), false)
        verify(!binding.mixed)
        verify(fixture.undoService.undo())
        verify(binding.mixed)
        compare(fixture.document.valueFor(fixture.firstId, "liga"), false)
        compare(fixture.document.valueFor(fixture.secondId, "liga"), true)
        features.featureBindings = ({})
        features.fontMetadata = originalMetadata
    }

    function test_datetime_reports_dst_overlap_and_requires_explicit_choice() {
        const service = createTemporaryObject(localeComponent, root)
        const field = createTemporaryObject(datetimeComponent, root)
        verify(service && field)
        field.localeService = service
        field.draft = "2024-11-03 01:30"
        verify(!field.commitDraft())
        compare(field.resolutionState, "ambiguous")
        compare(field.resolutionCandidates.length, 2)
        compare(field.candidateIndex, -1)
        verify(field.resolutionDescription.includes("twice"))
        verify(field.chooseCandidate(1) === false) // no binding: resolution never silently stores
        compare(field.candidateIndex, 1)

        field.draft = "2024-03-10 02:30"
        verify(!field.commitDraft())
        compare(field.resolutionState, "nonexistent")
        compare(field.resolutionCandidates.length, 0)
        verify(field.validationError.length > 0)
    }

    function test_locale_date_parse_and_leap_calendar_grid() {
        const service = createTemporaryObject(localeComponent, root)
        const picker = createTemporaryObject(calendarComponent, root)
        verify(service && picker)
        picker.localeService = service
        verify(picker.monthData.ok)
        compare(picker.monthData.cells.length, 42)
        verify(picker.monthData.cells.some(function(cell) { return cell.date === "2024-02-29" && cell.enabled }))
        verify(!picker.showDate("2024-02-01") || picker.monthData.monthLabel.length > 0)
        const parsed = service.parseDate("2/29/2024", "M/d/yyyy")
        verify(parsed.ok)
        compare(parsed.value, "2024-02-29")
        verify(!service.parseDate("2/30/2024", "M/d/yyyy").ok)
    }

    function test_calendar_keyboard_month_navigation_and_range_selection() {
        const service = createTemporaryObject(localeComponent, root)
        const picker = createTemporaryObject(calendarComponent, root)
        picker.localeService = service
        picker.rangeMode = true
        const spy = createTemporaryObject(spyComponent, root, { target: picker, signalName: "dateRangeSelected" })
        picker.activateCell(picker.activeIndex)
        compare(picker.rangeStart, "2024-02-29")
        picker.activateCell(picker.activeIndex + 1)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "2024-02-29")
        compare(spy.signalArguments[0][1], "2024-03-01")
        const month = picker.shownMonth
        verify(picker.shiftMonth(1))
        compare(picker.shownMonth, month === 12 ? 1 : month + 1)
    }

    function test_ambiguous_instant_commit_keeps_utc_candidate_and_undoes() {
        const fixture = inspectorFixtures.create()
        const service = createTemporaryObject(localeComponent, root)
        const field = createTemporaryObject(datetimeComponent, root)
        field.localeService = service
        field.binding = fixture.instantBinding
        field.draft = "2024-11-03 01:30"
        verify(!field.commitDraft())
        compare(field.resolutionState, "ambiguous")
        const chosen = field.resolutionCandidates[1]
        verify(field.chooseCandidate(1))
        compare(fixture.document.valueFor(fixture.firstId, "instant"), chosen)
        compare(field.canonicalValue, chosen)
        verify(fixture.undoService.undo())
        compare(fixture.document.valueFor(fixture.firstId, "instant"), "2024-11-03T04:00:00.000Z")
    }

    function test_all_day_date_commit_remains_date_only_and_undoes() {
        const fixture = inspectorFixtures.create()
        const service = createTemporaryObject(localeComponent, root)
        const field = createTemporaryObject(datetimeComponent, root)
        field.localeService = service
        field.binding = fixture.dateBinding
        field.dateOnly = true
        field.dateFormat = "M/d/yyyy"
        field.draft = "3/1/2024"
        verify(field.commitDraft())
        compare(fixture.document.valueFor(fixture.firstId, "allDay"), "2024-03-01")
        verify(fixture.undoService.undo())
        compare(fixture.document.valueFor(fixture.firstId, "allDay"), "2024-02-29")
        compare(field.canonicalValue, "2024-02-29")
    }
}
