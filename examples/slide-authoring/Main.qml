// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned slide, master, animation and presenter snapshots for WP-056.
Rectangle {
    id: root
    width: 1520
    height: 960
    color: Tk.Theme.color.bg
    property string documentId: "qindatk.example.deck"
    property string currentSlideId: "slide-title"
    property var selectedSlideIds: ["slide-title"]
    property var selectedObjectIds: ["obj-title"]
    property string selectedMasterId: "master-midnight"
    property string selectedLayoutId: "layout-title"
    property string viewMode: "author"
    property string statusText: qsTr("Host model ready · 4 slides · 3 editable objects on selected slide")
    property var slides: [
        { id: "slide-title", title: "Signals in Motion", section: "Opening", hidden: false, masterId: "master-midnight", layoutId: "layout-title", objects: [
            { id: "obj-title", kind: "text", role: "title", text: "Signals in Motion", bounds: ({ x: 116, y: 76, width: 560, height: 100 }) },
            { id: "obj-subtitle", kind: "text", role: "subtitle", text: "A design systems field guide", bounds: ({ x: 122, y: 188, width: 450, height: 54 }) },
            { id: "obj-accent", kind: "shape", role: "accent", text: "", bounds: ({ x: 120, y: 296, width: 196, height: 18 }), fill: "#16c7d8" }] },
        { id: "slide-map", title: "The connected studio", section: "Principles", hidden: false, masterId: "master-midnight", layoutId: "layout-content", objects: [
            { id: "map-heading", kind: "text", role: "title", text: "One system, many surfaces", bounds: ({ x: 100, y: 64, width: 600, height: 80 }) },
            { id: "map-body", kind: "text", role: "body", text: "Tokens connect every panel, page and timeline.", bounds: ({ x: 112, y: 188, width: 540, height: 100 }) },
            { id: "map-card", kind: "shape", role: "card", text: "Shared language", bounds: ({ x: 620, y: 250, width: 230, height: 132 }), fill: "#2d3342" }] },
        { id: "slide-craft", title: "Make details legible", section: "Principles", hidden: false, masterId: "master-paper", layoutId: "layout-content", objects: [
            { id: "craft-title", kind: "text", role: "title", text: "Make details legible", bounds: ({ x: 100, y: 68, width: 650, height: 76 }) }] },
        { id: "slide-end", title: "Thank you", section: "Closing", hidden: false, masterId: "master-midnight", layoutId: "layout-title", objects: [
            { id: "end-title", kind: "text", role: "title", text: "Thank you", bounds: ({ x: 120, y: 180, width: 450, height: 96 }) }] }
    ]
    property var masters: [
        { id: "master-midnight", title: "Midnight", themeId: "Tideglass", layouts: [{ id: "layout-title", title: "Title" }, { id: "layout-content", title: "Content" }], placeholders: [
            { role: "title", label: "Title", value: "Slide title" }, { role: "subtitle", label: "Subtitle", value: "Supporting line" }, { role: "body", label: "Body", value: "Content placeholder" }] },
        { id: "master-paper", title: "Paper", themeId: "Quiet Ink", layouts: [{ id: "layout-title", title: "Title" }, { id: "layout-content", title: "Content" }], placeholders: [
            { role: "title", label: "Title", value: "Paper title" }, { role: "subtitle", label: "Subtitle", value: "Supporting line" }, { role: "body", label: "Body", value: "Content placeholder" }] }
    ]
    property var placeholderOverrides: [
        { slideId: "slide-title", role: "title", value: "Signals in Motion" },
        { slideId: "slide-title", role: "subtitle", value: "A design systems field guide" }
    ]
    property var effects: [
        { id: "effect-title", targetId: "obj-title", targetLabel: "Title", kind: "fade-in", label: "Fade in", trigger: "with", delay: 0, duration: 0.6, easing: "out-cubic" },
        { id: "effect-subtitle", targetId: "obj-subtitle", targetLabel: "Subtitle", kind: "move-up", label: "Rise", trigger: "after", delay: 0.2, duration: 0.8, easing: "out-cubic" },
        { id: "effect-accent", targetId: "obj-accent", targetLabel: "Accent bar", kind: "wipe", label: "Wipe", trigger: "click", delay: 0, duration: 0.45, easing: "linear" }
    ]
    property string selectedEffectId: "effect-title"
    property bool audienceBlanked: false
    property string timerState: "running"
    property string audienceStatus: "connected"
    property string displayId: "display-projector-1"

    function slideById(id) { return slides.find(function(item) { return item.id === id }) || null }
    function activeSlide() { return slideById(currentSlideId) || ({}) }
    function currentObjects() { return activeSlide().objects || [] }
    function setSlide(id) {
        if (!slideById(id)) return
        currentSlideId = id; selectedSlideIds = [id]; selectedObjectIds = []
        selectedMasterId = activeSlide().masterId; selectedLayoutId = activeSlide().layoutId
        statusText = qsTr("Selected slide %1").arg(activeSlide().title)
    }
    function replaceSlide(id, transform) {
        const next = slides.map(function(item) { return Object.assign({}, item) })
        const index = next.findIndex(function(item) { return item.id === id })
        if (index >= 0) { next[index] = transform(next[index]); slides = next }
    }
    function updateObject(id, transform) {
        const objects = currentObjects().map(function(item) { return Object.assign({}, item) })
        const index = objects.findIndex(function(item) { return item.id === id })
        if (index >= 0) { objects[index] = transform(objects[index]); replaceSlide(currentSlideId, function(item) { item.objects = objects; return item }) }
    }
    function applyReorder(id, targetIndex) {
        const next = slides.slice(), from = next.findIndex(function(item) { return item.id === id })
        if (from < 0 || targetIndex < 0 || targetIndex >= next.length) return
        const moved = next.splice(from, 1)[0]; next.splice(targetIndex, 0, moved); slides = next
        statusText = qsTr("Moved %1 to position %2").arg(moved.title).arg(targetIndex + 1)
    }
    function nextSlide() { const i = slides.findIndex(function(item) { return item.id === currentSlideId }); return slides[i + 1] || null }
    function setEffect(id, patch) {
        const next = effects.map(function(item) { return Object.assign({}, item) })
        const i = next.findIndex(function(item) { return item.id === id })
        if (i >= 0) { Object.assign(next[i], patch); effects = next }
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { text: qsTr("Tideglass · Presentation workshop"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Button { objectName: "slideAuthorMode"; text: qsTr("Author"); iconName: "panels-top-left"; variant: root.viewMode === "author" ? "accent" : "outline"; tooltip: qsTr("Show slide authoring tools"); onClicked: root.viewMode = "author" }
            Tk.Button { objectName: "slidePresenterMode"; text: qsTr("Present"); iconName: "presentation"; variant: root.viewMode === "present" ? "accent" : "outline"; tooltip: qsTr("Show presenter controls and audience preview"); onClicked: root.viewMode = "present" }
        }
        Tk.Caption { text: root.statusText; objectName: "slideHostStatus"; wrapMode: Text.WordWrap }
        Item {
            visible: root.viewMode === "author"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.sm
                Tk.Panel {
                    objectName: "slideSorterPanel"; title: qsTr("Slides · %1").arg(root.slides.length)
                    implicitWidth: Tk.Theme.size.panelMinWidth * 1.55
                    Tk.SlideSorter {
                        anchors.fill: parent; anchors.margins: Tk.Theme.space.sm
                        documentId: root.documentId; slides: root.slides; selectedSlideIds: root.selectedSlideIds; currentSlideId: root.currentSlideId
                        onSelectionRequested: function(id, additive) {
                            if (additive) { const next = root.selectedSlideIds.slice(); const i = next.indexOf(id); if (i >= 0) next.splice(i, 1); else next.push(id); root.setSlide(id); root.selectedSlideIds = next }
                            else root.setSlide(id)
                        }
                        onReorderRequested: function(id, index) { root.applyReorder(id, index) }
                        onDuplicateRequested: function(id) {
                            const source = root.slideById(id), stamp = Date.now()
                            const copy = Object.assign({}, source, { id: "slide-copy-" + stamp, title: source.title + " copy",
                                objects: (source.objects || []).map(function(obj) { return Object.assign({}, obj, { id: obj.id + "-copy-" + stamp, bounds: Object.assign({}, obj.bounds, { x: obj.bounds.x + 24, y: obj.bounds.y + 24 }) }) }) })
                            const next = root.slides.slice(); next.splice(root.slides.findIndex(function(item) { return item.id === id }) + 1, 0, copy); root.slides = next; root.setSlide(copy.id)
                        }
                        onDeleteRequested: function(id) { root.slides = root.slides.filter(function(item) { return item.id !== id }); if (root.currentSlideId === id) root.setSlide(root.slides[0].id); root.statusText = qsTr("Slide %1 removed from the host model").arg(id) }
                        onHiddenChangeRequested: function(id, hidden) { root.replaceSlide(id, function(item) { item.hidden = hidden; return item }) }
                        onInsertRequested: function(afterId, requestId) {
                            const slide = { id: "slide-new-" + Date.now(), title: "New slide", section: "Unsectioned", hidden: false, masterId: root.selectedMasterId, layoutId: "layout-content", objects: [] }
                            const next = root.slides.slice(); next.splice(root.slides.findIndex(function(item) { return item.id === afterId }) + 1, 0, slide); root.slides = next; root.setSlide(slide.id); root.statusText = qsTr("Created slide for request %1").arg(requestId)
                        }
                    }
                }
                Tk.Panel {
                    objectName: "slideCanvasPanel"; title: qsTr("%1 · 16:9").arg(root.activeSlide().title || "Slide")
                    Tk.Flex.grow: 1; Tk.Flex.basis: 0
                    Tk.SlideCanvas {
                        anchors.fill: parent; anchors.margins: Tk.Theme.space.sm
                        documentId: root.documentId; slideId: root.currentSlideId; masterId: root.selectedMasterId
                        slideSize: ({ width: 960, height: 540 }); objects: root.currentObjects(); selectedObjectIds: root.selectedObjectIds
                        masterChoices: root.masters.map(function(master) { return { id: master.id, label: master.title } })
                        onSelectionRequested: function(id) { root.selectedObjectIds = [id]; root.statusText = qsTr("Selected object %1").arg(id) }
                        onObjectMoveRequested: function(id, bounds) { root.updateObject(id, function(obj) { obj.bounds = bounds; return obj }); root.statusText = qsTr("Moved %1 to x %2, y %3").arg(id).arg(Math.round(bounds.x)).arg(Math.round(bounds.y)) }
                        onObjectTextEditRequested: function(id, text) { root.updateObject(id, function(obj) { obj.text = text; return obj }); root.statusText = qsTr("Updated text on %1").arg(id) }
                        onObjectInsertRequested: function(kind, bounds, requestId) {
                            const object = { id: "obj-" + Date.now(), kind: kind, role: kind, text: kind === "text" ? "New text" : "New shape", bounds: bounds }
                            const next = root.currentObjects().concat([object]); root.replaceSlide(root.currentSlideId, function(item) { item.objects = next; return item }); root.selectedObjectIds = [object.id]
                            root.statusText = qsTr("Inserted %1 for request %2").arg(kind).arg(requestId)
                        }
                        onMasterApplyRequested: function(masterId, slideId) { root.replaceSlide(slideId, function(item) { item.masterId = masterId; return item }); root.selectedMasterId = masterId; root.statusText = qsTr("Applied master %1 to %2").arg(masterId).arg(slideId) }
                    }
                }
                Tk.Panel {
                    objectName: "slideInspectorPanel"; title: qsTr("Master and motion"); implicitWidth: Tk.Theme.size.panelMinWidth * 2
                    Tk.Scroll {
                        anchors.fill: parent; anchors.margins: Tk.Theme.space.sm; overflowY: Tk.Scroll.Auto
                        Tk.Flex {
                            direction: Tk.Flex.Column; gap: Tk.Theme.space.md
                            Tk.SlideMasterEditor {
                                objectName: "slideMasterEditor"; width: parent.width; documentId: root.documentId; masters: root.masters
                                masterId: root.selectedMasterId; layoutId: root.selectedLayoutId; selectedSlideId: root.currentSlideId
                                selectedSlideIds: root.selectedSlideIds; placeholderOverrides: root.placeholderOverrides
                                onMasterSelectionRequested: function(id) { root.selectedMasterId = id }
                                onLayoutSelectionRequested: function(id) { root.selectedLayoutId = id }
                                onPlaceholderEditRequested: function(masterId, layoutId, role, value, requestId) {
                                    const next = root.masters.map(function(master) { return Object.assign({}, master, { placeholders: (master.placeholders || []).map(function(item) { return Object.assign({}, item) }) }) })
                                    const master = next.find(function(item) { return item.id === masterId }), placeholder = master.placeholders.find(function(item) { return item.role === role })
                                    if (placeholder) placeholder.value = value; root.masters = next; root.statusText = qsTr("Master placeholder %1 changed · %2").arg(role).arg(requestId)
                                }
                                onApplyMasterRequested: function(masterId, ids) { ids.forEach(function(id) { root.replaceSlide(id, function(item) { item.masterId = masterId; item.layoutId = root.selectedLayoutId; return item }) }); root.statusText = qsTr("Applied %1 master to %2 slide(s)").arg(masterId).arg(ids.length) }
                                onOverrideResetRequested: function(slideId, role) { root.placeholderOverrides = root.placeholderOverrides.filter(function(item) { return !(item.slideId === slideId && item.role === role) }); root.statusText = qsTr("Reset %1 override on %2").arg(role).arg(slideId) }
                            }
                            Tk.Divider {}
                            Tk.AnimationPane {
                                objectName: "slideAnimationPane"; width: parent.width; documentId: root.documentId; slideId: root.currentSlideId
                                effects: root.effects; selectedEffectId: root.selectedEffectId
                                targetChoices: root.currentObjects().map(function(obj) { return { id: obj.id, label: obj.role || obj.id } })
                                onEffectSelectionRequested: function(id) { root.selectedEffectId = id }
                                onEffectMoveRequested: function(id, targetIndex) { const next = root.effects.slice(), from = next.findIndex(function(item) { return item.id === id }), moved = next.splice(from, 1)[0]; next.splice(targetIndex, 0, moved); root.effects = next; root.statusText = qsTr("Reordered animation %1").arg(id) }
                                onEffectAddRequested: function(targetId, kind, requestId) {
                                    const target = root.currentObjects().find(function(obj) { return obj.id === targetId })
                                    const effect = { id: "effect-" + Date.now(), targetId: targetId, targetLabel: target ? target.role : targetId, kind: kind, label: "Fade in", trigger: "after", delay: 0, duration: 0.5, easing: "out-cubic" }
                                    root.effects = root.effects.concat([effect]); root.selectedEffectId = effect.id; root.statusText = qsTr("Added effect for %1 · %2").arg(targetId).arg(requestId)
                                }
                                onEffectDeleteRequested: function(id) { root.effects = root.effects.filter(function(item) { return item.id !== id }) }
                                onEffectEditRequested: function(id, patch, requestId) { root.setEffect(id, patch); root.statusText = qsTr("Updated effect %1 · %2").arg(id).arg(requestId) }
                                onPreviewRequested: function(id) { root.statusText = qsTr("Preview evaluation requested for %1").arg(id) }
                                onPreviewCancelRequested: function(id) { root.statusText = qsTr("Preview cancelled for %1").arg(id) }
                            }
                        }
                    }
                }
            }
        }
        Item {
            visible: root.viewMode === "present"
            Tk.Flex.grow: 1; Tk.Flex.basis: 0
            Tk.PresenterView {
                anchors.fill: parent; documentId: root.documentId; currentSlideId: root.currentSlideId
                previousSlideId: root.slides[root.slides.findIndex(function(item) { return item.id === root.currentSlideId }) - 1] ? root.slides[root.slides.findIndex(function(item) { return item.id === root.currentSlideId }) - 1].id : ""
                nextSlideId: root.nextSlide() ? root.nextSlide().id : ""
                currentIndex: root.slides.findIndex(function(item) { return item.id === root.currentSlideId }); slideCount: root.slides.length
                currentTitle: root.activeSlide().title || ""; nextTitle: root.nextSlide() ? root.nextSlide().title : ""
                speakerNotes: "Opening: explain that every surface shares the same design language.\n\nAsk the audience where a state change should be visible."
                elapsedText: "08:42"; timerState: root.timerState; audienceStatus: root.audienceStatus; selectedDisplayId: root.displayId
                displays: [{ id: "display-laptop", label: "Studio display", connected: true, role: "presenter" }, { id: "display-projector-1", label: "Projector · room A", connected: true, role: "audience" }]
                blackedOut: root.audienceBlanked
                onSlideNavigationRequested: function(id, index) { root.setSlide(root.slides[index].id) }
                onTimerActionRequested: function(action) { root.timerState = action === "pause" ? "paused" : "running" }
                onBlankingRequested: function(blanked) { root.audienceBlanked = blanked }
                onDisplaySelectionRequested: function(id) { root.displayId = id; root.audienceStatus = "connected" }
                onAnnotationRequested: function(slideId, tool) { root.statusText = qsTr("Audience annotation tool %1 requested for %2").arg(tool).arg(slideId) }
                onReconnectRequested: function(id) { root.audienceStatus = "connected"; root.statusText = qsTr("Reconnected audience display %1").arg(id) }
                Tk.Flex {
                    anchors.fill: parent; direction: Tk.Flex.Column; align: Tk.Flex.Center; gap: Tk.Theme.space.md
                    Tk.Heading { text: root.activeSlide().title || qsTr("Untitled slide"); level: 1 }
                    Tk.Caption { text: (root.activeSlide().objects || []).map(function(obj) { return obj.text || "" }).filter(function(text) { return text.length > 0 }).join(" · ") }
                    Tk.Badge { text: qsTr("Audience output · no notes"); variant: "success"; tooltip: qsTr("The audience surface excludes speaker notes") }
                }
            }
        }
    }
}
