// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "SourceProgramMediaBin"
    visible: true
    width: 800
    height: 600

    property string requestedId: ""
    property string requestedTicks: ""
    property string relinkId: ""
    property string proxyAction: ""

    ListModel {
        id: catalog
        ListElement { assetId: "stable-asset-7"; resourceId: "media:7"; label: "Take seven"; caption: "01:02:03:04" }
    }
    Tk.SelectionModel { id: selection }
    Component {
        id: fixture
        Item {
            width: 800; height: 600
            Tk.SourceProgramMonitor {
                id: monitor
                width: 460; height: 420
                viewId: "test-source"
                frameId: "frame-900719925474099312345"
                frameTimeTicks: "900719925474099312399"
                nextFrameId: "frame-900719925474099312346"
                nextFrameTimeTicks: "900719925474099313000"
                onSeekRequested: function(requestId, ticks, frameId) {
                    testCase.requestedId = frameId
                    testCase.requestedTicks = ticks
                }
            }
            Tk.MediaBin {
                width: 320; height: 520
                model: catalog
                idRole: "assetId"
                selectionModel: selection
                selectedAssetId: "stable-asset-7"
                selectedMetadata: ({name: "Take seven", online: false, proxyStatus: "Missing proxy"})
                onRelinkRequested: function(id) { testCase.relinkId = id }
                onProxyActionRequested: function(id, action) { testCase.relinkId = id; testCase.proxyAction = action }
            }
        }
    }
    property var testCase: null

    function init() { testCase = this }

    function test_exactSeekIdentityAndCancelAcknowledgement() {
        const object = createTemporaryObject(fixture, this)
        verify(object !== null)
        const monitor = findChild(object, "sourceProgramMonitor")
        verify(monitor !== null)
        const requestId = monitor.stepFrame(1)
        compare(requestedId, "frame-900719925474099312346")
        compare(requestedTicks, "900719925474099313000")
        verify(requestId.length > 0)
        compare(monitor.acknowledgeSeek("stale", true, "ignored", "1", ""), false)
        compare(monitor.pendingRequestId, requestId)
        compare(monitor.acknowledgeSeek(requestId, false, "", "", "host canceled"), true)
        compare(monitor.pendingRequestId, "")
        compare(monitor.frameId, "frame-900719925474099312345")
        object.destroy()
    }

    function test_binRelinkAndProxyAreHostRequests() {
        const object = createTemporaryObject(fixture, this)
        verify(object !== null)
        const bin = findChild(object, "mediaBin")
        verify(bin !== null)
        bin.relinkRequested(bin.selectedAssetId)
        compare(relinkId, "stable-asset-7")
        bin.proxyActionRequested(bin.selectedAssetId, "create")
        compare(proxyAction, "create")
        object.destroy()
    }
}
