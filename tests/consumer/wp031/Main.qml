// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1024
    height: 1360

    Tk.HeatmapModel { id: heat; objectName: "installedHeatmapModel"; valueUnit: "dB" }
    Tk.DistributionModel { id: distribution; objectName: "installedDistributionModel" }
    Tk.GanttCalendar { id: calendar; objectName: "installedGanttCalendar" }
    Tk.GanttSimpleScheduleAdapter { id: scheduler }
    Tk.GanttTaskModel {
        id: tasks
        objectName: "installedGanttTaskModel"
        calendar: calendar
        transactionAdapter: scheduler
    }
    Tk.PartWholeModel { id: parts; objectName: "installedPartWholeModel"; unit: "kg" }
    Tk.OhlcModel { id: candles; objectName: "installedOhlcModel" }
    Tk.TargetModel { id: targets; objectName: "installedTargetModel" }
    Tk.PolarDataModel { id: polar; objectName: "installedPolarDataModel" }
    Tk.UncertaintyModel {
        id: intervals
        objectName: "installedUncertaintyModel"
        sourceDatasetId: "installed-sensor"
        xUnit: "hour"
        yUnit: "°C"
        meaning: Tk.UncertaintyModel.Confidence
        coverage: 0.95
    }

    readonly property bool viewsReady:
        heatView.model === heat && heatView.model.storedCellCount === 2
        && distributionView.distributionModel === distribution
        && distributionView.groupCount === 1
        && distributionView.legendText.indexOf("ms") >= 0
        && ganttView.taskModel === tasks && ganttView.count === 2
        && partView.model === parts && parts.knownTotal === 4
        && candleView.candleModel === candles && candles.count === 2
        && targetView.targetModel === targets && targets.count === 1
        && polarView.model === polar && polarView.valid && polar.count === 2
        && intervalView.model === intervals && intervals.count === 2
        && intervals.missingCount === 1

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Heatmap {
            id: heatView
            objectName: "installedHeatmap"
            model: heat
            tooltip: qsTr("Installed heatmap")
            Tk.Flex.flex: 1
        }
        Tk.DistributionPlot {
            id: distributionView
            objectName: "installedDistributionPlot"
            distributionModel: distribution
            tooltip: qsTr("Installed distribution")
            Tk.Flex.flex: 1
        }
        Tk.GanttView {
            id: ganttView
            objectName: "installedGanttView"
            taskModel: tasks
            visibleStartUtc: "1741356000000"
            tooltip: qsTr("Installed schedule")
            Tk.Flex.flex: 1
        }
        Tk.PartWholeView {
            id: partView
            objectName: "installedPartWholeView"
            model: parts
            tooltip: qsTr("Installed parts")
            Tk.Flex.flex: 1
        }
        Tk.CandlestickView {
            id: candleView
            objectName: "installedCandlestickView"
            candleModel: candles
            tooltip: qsTr("Installed candles")
            Tk.Flex.flex: 1
        }
        Tk.TargetDisplay {
            id: targetView
            objectName: "installedTargetDisplay"
            targetModel: targets
            tooltip: qsTr("Installed targets")
            Tk.Flex.flex: 1
        }
        Tk.PolarView {
            id: polarView
            objectName: "installedPolarView"
            model: polar
            radialMinimum: 0
            radialMaximum: 10
            radialUnit: "m/s"
            tooltip: qsTr("Installed polar")
            Tk.Flex.flex: 1
        }
        Tk.UncertaintyView {
            id: intervalView
            objectName: "installedUncertaintyView"
            model: intervals
            tooltip: qsTr("Installed intervals")
            Tk.Flex.flex: 1
        }
    }
}
