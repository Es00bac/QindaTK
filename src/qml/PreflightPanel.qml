// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The preflight engine owns checks and repair policy. This view lists stable
// issue IDs and reports explicit navigation/repair requests to that host.
Item {
    id: control
    property var issues: [] // [{id,severity,category,title,description,targetId,scope,repairActionIds,repairLabel,repairAvailable,error}]
    property string selectedIssueId: ""
    property string filterSeverity: "all"
    property string preflightStatus: "ready" // ready, running, unavailable, failed
    property string profileId: ""
    property string errorText: ""
    property string tooltip: qsTr("Review production issues before output")
    readonly property var filteredIssues: filter()
    readonly property var selectedIssue: issueById(selectedIssueId)
    readonly property int blockingCount: countAtOrAbove("error")
    signal issueSelectionRequested(string issueId)
    signal navigateRequested(string issueId, string targetId)
    signal repairRequested(string issueId, string repairActionId)
    signal refreshRequested(string profileId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2.3
    implicitHeight: content.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function issueById(id) {
        for (let i = 0; i < issues.length; ++i) if (issues[i].id === id) return issues[i]
        return null
    }
    function rank(severity) {
        if (severity === "blocker") return 4
        if (severity === "error") return 3
        if (severity === "warning") return 2
        return 1
    }
    function countAtOrAbove(severity) {
        const minimum = rank(severity)
        return issues.filter(function(issue) { return rank(issue.severity) >= minimum }).length
    }
    function filter() {
        return issues.filter(function(issue) {
            return control.filterSeverity === "all" || issue.severity === control.filterSeverity
        })
    }
    function selectIssue(id) {
        if (!enabled || !issueById(id)) return
        selectedIssueId = id
        issueSelectionRequested(id)
    }
    function navigate() {
        if (enabled && selectedIssue && selectedIssue.targetId)
            navigateRequested(selectedIssue.id, selectedIssue.targetId)
    }
    function repair() {
        const issue = selectedIssue
        if (enabled && issue && issue.repairAvailable && issue.repairActionIds.length > 0)
            repairRequested(issue.id, issue.repairActionIds[0])
    }

    Tk.Flex {
        id: content
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("PREFLIGHT"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Badge {
                text: qsTr("%1 blocking").arg(control.blockingCount)
                variant: control.blockingCount > 0 ? "danger" : "success"
                tooltip: qsTr("Error and blocker issues")
            }
            Tk.Button {
                objectName: "preflightRefresh"
                text: qsTr("Refresh")
                iconName: "refresh-cw"
                small: true
                tooltip: qsTr("Request new validation for the active profile")
                enabled: control.preflightStatus !== "running"
                onClicked: control.refreshRequested(control.profileId)
            }
        }
        Tk.Caption {
            objectName: "preflightUnavailable"
            visible: control.preflightStatus === "unavailable" || control.preflightStatus === "failed"
            color: control.preflightStatus === "failed" ? Tk.Theme.color.danger : Tk.Theme.color.warning
            text: control.errorText.length > 0 ? control.errorText
                 : control.preflightStatus === "unavailable" ? qsTr("Preflight checks are unavailable for this profile.")
                 : qsTr("Preflight did not complete.")
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Segmented {
                objectName: "preflightSeverityFilter"
                model: ["all", "blocker", "error", "warning", "info"]
                currentIndex: Math.max(0, model.indexOf(control.filterSeverity))
                small: true
                stretch: true
                tooltip: qsTr("Filter preflight issues by severity")
                onActivated: function(index) { control.filterSeverity = model[index] }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
        }
        Tk.Scroll {
            objectName: "preflightIssueScroll"
            implicitHeight: Math.min(issueList.implicitHeight, Tk.Theme.size.menuItem * 10)
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: issueList
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.filteredIssues
                    delegate: Tk.Button {
                        required property var modelData
                        objectName: "preflightIssue_" + modelData.id
                        text: "" // accessible row uses the semantic severity and summary below
                        tooltip: modelData.title
                        variant: modelData.id === control.selectedIssueId ? "outline" : "ghost"
                        contentItem: Tk.Flex {
                            direction: Tk.Flex.Column
                            align: Tk.Flex.Start
                            gap: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.Badge {
                                    text: modelData.severity
                                    variant: modelData.severity === "blocker" || modelData.severity === "error" ? "danger"
                                           : modelData.severity === "warning" ? "warning" : "info"
                                    tooltip: qsTr("Issue severity: %1").arg(modelData.severity)
                                }
                                Tk.Caption { text: modelData.category; color: Tk.Theme.color.textMuted; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                            }
                            Tk.Label { text: modelData.title; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                            Tk.Caption { text: modelData.description; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                        }
                        Accessible.name: modelData.severity + ": " + modelData.title
                        Accessible.description: modelData.description
                        onClicked: control.selectIssue(modelData.id)
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "preflightIssueDetail"
            visible: control.selectedIssue !== null
            text: control.selectedIssue
                  ? qsTr("ID %1 · scope %2 · target %3").arg(control.selectedIssue.id)
                    .arg(control.selectedIssue.scope || qsTr("document"))
                    .arg(control.selectedIssue.targetId || qsTr("none")) : ""
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            objectName: "preflightIssueError"
            visible: control.selectedIssue !== null && !!control.selectedIssue.error
            color: Tk.Theme.color.danger
            text: control.selectedIssue ? control.selectedIssue.error || "" : ""
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "preflightNavigate"
                text: qsTr("Go to issue")
                iconName: "navigation"
                tooltip: qsTr("Navigate to the affected document object")
                enabled: control.selectedIssue !== null && !!control.selectedIssue.targetId
                onClicked: control.navigate()
            }
            Tk.Button {
                objectName: "preflightRepair"
                text: control.selectedIssue && control.selectedIssue.repairLabel
                     ? control.selectedIssue.repairLabel : qsTr("Repair")
                iconName: "wrench"
                tooltip: qsTr("Ask the host to validate and apply this repair action")
                enabled: control.selectedIssue !== null && control.selectedIssue.repairAvailable
                         && control.selectedIssue.repairActionIds.length > 0
                onClicked: control.repair()
            }
        }
        Tk.Caption {
            visible: control.preflightStatus === "ready" && control.filteredIssues.length === 0
            color: Tk.Theme.color.success
            text: qsTr("No issues match this filter.")
        }
    }
}
