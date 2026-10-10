// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// D-314: TextField's host-fed completion popup (keyboard selection, the whole
// entry in suggestionChosen, Return unchanged while it is closed) and the
// TokenField validator that receives the whole suggestion.
TestCase {
    id: root
    name: "TextCompletion"
    when: windowShown
    visible: true
    width: 480
    height: 360

    Component { id: spyComponent; SignalSpy {} }
    Component { id: fieldComponent; Tk.TextField { x: 10; y: 10; width: 240; tooltip: "Formula" } }
    Component { id: otherComponent; Tk.TextField { x: 10; y: 300; width: 120; tooltip: "Other" } }
    Component {
        id: tokenComponent
        Tk.TokenField {
            property var seen: []
            width: 360
            standalone: true
            label: "To"
            validator: function(label, suggestion) {
                seen.push(suggestion)
                return { label: label, value: suggestion.value || label, valid: true }
            }
        }
    }
    Component {
        id: legacyTokenComponent
        Tk.TokenField {
            property int calls: 0
            width: 360
            standalone: true
            label: "Cc"
            validator: function(label) {
                calls += 1
                return { label: label, value: label.toLowerCase(), valid: label.indexOf("@") > 0 }
            }
        }
    }

    function test_suggestions_keyboard_selection_and_whole_entry() {
        const field = createTemporaryObject(fieldComponent, root)
        const chosen = createTemporaryObject(spyComponent, root, { target: field, signalName: "suggestionChosen" })
        const accepted = createTemporaryObject(spyComponent, root, { target: field, signalName: "accepted" })
        field.forceActiveFocus()
        compare(field.suggestionsOpen, false)
        compare(findChild(field, "fieldSuggestions"), null)
        field.text = "=SU"
        field.suggestions = [{ label: "SUM", detail: "SUM(number1, …)", insert: "SUM(" },
                             { label: "SUMIF", detail: "SUMIF(range, criteria, …)", insert: "SUMIF(" },
                             "SUBSTITUTE"]
        compare(field.suggestionIndex, 0)
        verify(field.suggestionsOpen)
        const popup = findChild(field, "fieldSuggestions")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(popup.y, field.height + Tk.Theme.space.xs)
        compare(popup.width, Math.max(field.width, Tk.Theme.size.menuMinWidth))
        compare(popup.height, 3 * Tk.Theme.size.menuItem + popup.topPadding + popup.bottomPadding)
        const first = findChild(popup.contentItem, "fieldSuggestion_0")
        verify(first !== null)
        compare(findChild(first, "fieldSuggestionLabel").text, "SUM")
        compare(findChild(first, "fieldSuggestionDetail").text, "SUM(number1, …)")
        compare(first.Accessible.name, "SUM")
        keyClick(Qt.Key_Down)
        compare(field.suggestionIndex, 1)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        compare(field.suggestionIndex, 2)
        keyClick(Qt.Key_Up)
        compare(field.suggestionIndex, 1)
        keyClick(Qt.Key_Return)
        compare(chosen.count, 1)
        compare(chosen.signalArguments[0][0].label, "SUMIF")
        compare(chosen.signalArguments[0][0].insert, "SUMIF(")
        compare(accepted.count, 0)
        compare(field.text, "=SU")
        compare(field.suggestionsOpen, false)
        tryCompare(popup, "visible", false)
        keyClick(Qt.Key_Return)
        compare(accepted.count, 1)

        // New suggestions reopen it; Tab chooses a plain string entry.
        field.suggestions = ["alpha", "beta"]
        verify(field.suggestionsOpen)
        tryCompare(popup, "opened", true)
        keyClick(Qt.Key_Up)
        compare(field.suggestionIndex, 0)
        keyClick(Qt.Key_End)
        keyClick(Qt.Key_Tab)
        compare(chosen.count, 2)
        compare(chosen.signalArguments[1][0], "alpha")
        verify(field.activeFocus)

        // Escape closes without clearing; typing reopens; a click chooses.
        field.suggestions = ["gamma", "delta"]
        verify(field.suggestionsOpen)
        keyClick(Qt.Key_Escape)
        compare(field.suggestionsOpen, false)
        compare(field.text, "=SU")
        keyClick(Qt.Key_M)
        verify(field.suggestionsOpen)
        tryCompare(popup, "opened", true)
        const second = findChild(popup.contentItem, "fieldSuggestion_1")
        mouseClick(second)
        compare(chosen.count, 3)
        compare(chosen.signalArguments[2][0], "delta")
        verify(field.activeFocus)

        // Focus elsewhere closes it; an empty list removes the popup.
        field.suggestions = ["one"]
        verify(field.suggestionsOpen)
        const other = createTemporaryObject(otherComponent, root)
        other.forceActiveFocus()
        compare(field.suggestionsOpen, false)
        field.suggestions = []
        tryCompare(field, "suggestionIndex", -1)
        tryVerify(function() { return findChild(field, "fieldSuggestions") === null })
    }

    function test_suggestions_open_above_near_the_window_bottom() {
        const field = createTemporaryObject(otherComponent, root, { y: root.height - Tk.Theme.size.control - 4 })
        field.forceActiveFocus()
        field.suggestions = ["a", "b", "c", "d"]
        const popup = findChild(field, "fieldSuggestions")
        tryCompare(popup, "opened", true)
        verify(popup.y < 0)
        compare(popup.y, -popup.height - Tk.Theme.space.xs)
    }

    function test_token_validator_receives_whole_suggestion() {
        const tokens = createTemporaryObject(tokenComponent, root)
        verify(tokens.appendCandidates([{ label: "Ana Lima", value: "ana@example.org" }]))
        compare(tokens.tokens.length, 1)
        compare(tokens.tokens[0].label, "Ana Lima")
        compare(tokens.tokens[0].value, "ana@example.org")
        compare(tokens.seen[0].value, "ana@example.org")
        tokens.draft = "bo@example.org"
        verify(tokens.commitDraft())
        compare(tokens.seen[1].label, "bo@example.org")
        compare(tokens.tokens[1].value, "bo@example.org")

        const legacy = createTemporaryObject(legacyTokenComponent, root)
        verify(legacy.appendCandidates([{ label: "Cy@Example.org", value: "ignored" }]))
        compare(legacy.calls, 1)
        compare(legacy.tokens[0].value, "cy@example.org")
        legacy.draft = "nobody"
        verify(legacy.commitDraft())
        compare(legacy.tokens[1].valid, false)
        verify(legacy.errorMessage.length > 0)
    }
}
