// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk
TestCase {
    id: root
    name: "OfficeLists"
    visible: true; when: windowShown
    width: 800; height: 700
    Component { id: spyComponent; SignalSpy {} }
    Component { id: checklistComponent; Tk.SearchChecklist {
        width: 300; height: 240
        items: [{id:"a",label:"Alpha"},{id:"b",label:"Beta"},{id:"c",label:"Gamma",disabled:true}]
        checkedIds: ["b"]
        onCheckedIdsEdited: ids => checkedIds = ids
    } }
    Component { id: wellsComponent; Tk.FieldWells {
        width: 520
        fields: [{id:"region",label:"Region"},{id:"amount",label:"Amount"}]
    } }
    Component { id: sectionsComponent; Tk.SectionedGallery {
        width: 520
        sections: [{id:"intro",label:"Introduction",items:[{id:"one",label:"Title"}]},
                   {id:"results",label:"Results",items:[{id:"two",label:"Chart"}]}]
        currentId: "one"
    } }
    function init() { failOnWarning(/Binding loop detected/) }
    function test_search_and_select_only_matching_enabled_rows() {
        const list=createTemporaryObject(checklistComponent,root)
        verify(list)
        list.searchText="Al"
        compare(list.visibleItems.length,1)
        list.selectVisible(true)
        compare(list.checkedIds.length,2)
        verify(list.checkedIds.indexOf("b")>=0)
        list.selectVisible(false)
        compare(list.checkedIds,["b"])
        list.searchText=""
        list.selectVisible(true)
        compare(list.checkedIds.length,2)
        verify(list.checkedIds.indexOf("c")<0)
    }
    function test_field_assignment_by_keyboard_and_drag() {
        const wells=createTemporaryObject(wellsComponent,root)
        const spy=createTemporaryObject(spyComponent,root,{target:wells,signalName:"assignmentRequested"})
        verify(wells&&spy)
        waitForRendering(wells)
        const source=findChild(wells,"fieldWellSource_region")
        source.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(wells.selectedFieldId,"region")
        const target=findChild(wells,"fieldWellAssign_rows")
        target.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(spy.count,1)
        compare(Array.from(spy.signalArguments[0]),["region","rows",-1])
        const values=findChild(wells,"fieldWell_values")
        const amount = findChild(wells,"fieldWellSource_amount")
        const at = values.mapToItem(amount, values.width/2, values.height/2)
        mouseDrag(amount, amount.width/2, amount.height/2,
                  at.x - amount.width/2, at.y - amount.height/2, Qt.LeftButton, Qt.NoModifier, 50)
        compare(spy.count,2)
        compare(Array.from(spy.signalArguments[1]),["amount","values",-1])
        compare(wells.draggingFieldId,"")
    }
    function test_section_collapse_is_a_host_request() {
        const gallery=createTemporaryObject(sectionsComponent,root)
        const spy=createTemporaryObject(spyComponent,root,{target:gallery,signalName:"sectionToggled"})
        verify(gallery&&spy)
        waitForRendering(gallery)
        const header=findChild(gallery,"gallerySection_intro")
        header.forceActiveFocus();keyClick(Qt.Key_Space)
        compare(spy.count,1)
        compare(Array.from(spy.signalArguments[0]),["intro",true])
        compare(gallery.collapsedIds.length,0)
        gallery.collapsedIds=["intro"]
        tryCompare(findChild(gallery,"gallerySectionItems_intro"),"visible",false)
        compare(gallery.currentId,"one")
        compare(gallery.sections[0].items.length,1)
    }
}
