import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Palette ───────────────────────────────────────────────────────────
    readonly property color bgPrimary:      "#171717"
    readonly property color bgSecondary:    "#262626"
    readonly property color bgRow:          "#1E1E1E"
    readonly property color textPrimary:    "#FFFFFF"
    readonly property color textSecondary:  "#A4A4A4"
    readonly property color textMuted:      "#5D5D5D"
    readonly property color accentOrange:   "#FF5000"
    readonly property color successGreen:   "#22C55E"
    readonly property color errorRed:       "#FB3748"
    readonly property color warningYellow:  "#F59E0B"
    readonly property color borderColor:    "#333333"

    // ── State ─────────────────────────────────────────────────────────────
    property string nodeStatus: "starting"   // "ready" | "starting" | "offline"
    property var    logItems:   []
    property int    quotaUsed:  0
    property int    quotaTotal: 0

    // ── Helpers ───────────────────────────────────────────────────────────

    function callModuleParse(raw) {
        try { return JSON.parse(raw) } catch(e) { return null }
    }

    function refresh() {
        if (typeof logos === "undefined" || !logos.callModule) return

        var st = logos.callModule("stash", "getStatus", [])
        if (st) nodeStatus = String(st).replace(/"/g, "")

        var logRaw = callModuleParse(logos.callModule("stash", "getLog", []))
        if (Array.isArray(logRaw)) logItems = logRaw

        var q = callModuleParse(logos.callModule("stash", "getQuota", []))
        if (q && q.used !== undefined) {
            quotaUsed  = q.used
            quotaTotal = q.total
        }
    }

    function iconFor(type) {
        switch(type) {
            case "uploading":   return "↑"
            case "uploaded":    return "✓"
            case "downloading": return "↓"
            case "downloaded":  return "✓"
            case "error":       return "✗"
            case "offline":     return "○"
            case "quota_reached": return "⚠"
            default:            return "·"
        }
    }

    function colorFor(type) {
        switch(type) {
            case "uploaded":
            case "downloaded":  return root.successGreen
            case "error":       return root.errorRed
            case "offline":     return root.textMuted
            case "quota_reached": return root.warningYellow
            case "uploading":
            case "downloading": return root.accentOrange
            default:            return root.textSecondary
        }
    }

    function isCidRow(type) {
        return type === "uploaded"
    }

    function formatTs(ms) {
        var d = new Date(ms)
        return Qt.formatTime(d, "hh:mm:ss")
    }

    function quotaPercent() {
        if (quotaTotal <= 0) return 0
        return Math.min(1.0, quotaUsed / quotaTotal)
    }

    function formatBytes(n) {
        if (n <= 0) return "0 B"
        if (n < 1024) return n + " B"
        if (n < 1048576) return (n / 1024).toFixed(1) + " KB"
        if (n < 1073741824) return (n / 1048576).toFixed(1) + " MB"
        return (n / 1073741824).toFixed(2) + " GB"
    }

    // ── Timers ────────────────────────────────────────────────────────────

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: root.refresh()
    }

    Component.onCompleted: root.refresh()

    // ── Root background ───────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: root.bgPrimary
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── Header ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "Stash"
                font.pixelSize: 20
                font.bold: true
                color: root.textPrimary
            }

            Item { Layout.fillWidth: true }

            // Node status dot
            Rectangle {
                width: 8; height: 8
                radius: 4
                color: root.nodeStatus === "ready"    ? root.successGreen :
                       root.nodeStatus === "starting" ? root.warningYellow :
                                                        root.errorRed
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.nodeStatus
                font.pixelSize: 12
                color: root.textSecondary
                leftPadding: 6
            }
        }

        // ── Quota bar ─────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.quotaTotal > 0

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "Storage"
                    font.pixelSize: 11
                    color: root.textSecondary
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: root.formatBytes(root.quotaUsed) + " / " + root.formatBytes(root.quotaTotal)
                    font.pixelSize: 11
                    color: root.textMuted
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: root.bgSecondary

                Rectangle {
                    width: parent.width * root.quotaPercent()
                    height: parent.height
                    radius: parent.radius
                    color: root.quotaPercent() > 0.9 ? root.errorRed :
                           root.quotaPercent() > 0.7 ? root.warningYellow :
                                                       root.accentOrange
                    Behavior on width { NumberAnimation { duration: 300 } }
                }
            }
        }

        // ── Divider ───────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.borderColor
        }

        // ── Log list ──────────────────────────────────────────────────────
        ListView {
            id: logView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.logItems
            spacing: 2

            // Auto-scroll to bottom on new entries
            onCountChanged: Qt.callLater(() => logView.positionViewAtEnd())

            delegate: Rectangle {
                width: logView.width
                height: 36
                color: root.bgRow
                radius: 4

                required property var   modelData
                required property int   index

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    // Icon
                    Text {
                        text: root.iconFor(modelData.type)
                        font.pixelSize: 14
                        color: root.colorFor(modelData.type)
                        Layout.preferredWidth: 16
                    }

                    // Detail text
                    Text {
                        text: modelData.detail || modelData.type
                        font.pixelSize: 12
                        color: root.textSecondary
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    // Timestamp
                    Text {
                        text: root.formatTs(modelData.timestamp)
                        font.pixelSize: 10
                        color: root.textMuted
                    }

                    // Copy button — only on rows with a CID
                    Rectangle {
                        visible: root.isCidRow(modelData.type)
                        width: 40
                        height: 22
                        radius: 4
                        color: copyArea.containsMouse ? root.bgSecondary : "transparent"
                        border.color: root.borderColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: copyFeedback.running ? "✓" : "copy"
                            font.pixelSize: 10
                            color: copyFeedback.running ? root.successGreen : root.textSecondary
                        }

                        Timer {
                            id: copyFeedback
                            interval: 1200
                        }

                        MouseArea {
                            id: copyArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                // Qt clipboard trick via invisible TextEdit
                                clipHelper.text = modelData.detail
                                clipHelper.selectAll()
                                clipHelper.copy()
                                copyFeedback.restart()
                            }
                        }
                    }
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                visible: root.logItems.length === 0
                text: "No activity yet"
                font.pixelSize: 13
                color: root.textMuted
            }
        }
    }

    // Invisible TextEdit for clipboard copy
    TextEdit {
        id: clipHelper
        visible: false
    }
}
