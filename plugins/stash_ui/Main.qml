import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Palette ───────────────────────────────────────────────────────────
    readonly property color bgPrimary:      "#171717"
    readonly property color bgSecondary:    "#262626"
    readonly property color textPrimary:    "#FFFFFF"
    readonly property color textSecondary:  "#A4A4A4"
    readonly property color textMuted:      "#5D5D5D"
    readonly property color accentOrange:   "#FF5000"
    readonly property color successGreen:   "#22C55E"
    readonly property color errorRed:       "#FB3748"
    readonly property color warningYellow:  "#F59E0B"
    readonly property color borderColor:    "#333333"

    // ── State ─────────────────────────────────────────────────────────────
    property int    quotaUsed:    0
    property int    quotaTotal:   0
    property bool   modulesPanelOpen:   false
    property bool   pinningPanelOpen:   false
    property bool   logosPanelOpen:     false
    property bool   pinningConfigured:  false
    property bool   coreReady:          false
    property int    logSeenCount:       0
    property bool   logosStorageReady:   false
    property bool   logosStorageStarting: false
    property string logosStoragePeerId:  ""
    property string activeTransport:    "kubo"

    // ── Helpers ───────────────────────────────────────────────────────────

    function callModuleParse(raw) {
        try {
            var tmp = JSON.parse(raw)
            if (typeof tmp === 'string') {
                try { return JSON.parse(tmp) } catch(e) { return tmp }
            }
            return tmp
        } catch(e) { return null }
    }

    function entryLevel(type) {
        if (type === "backup_uploaded" || type === "uploaded" || type === "downloaded"
                || type === "logos_uploaded")
            return "success"
        if (type === "error")
            return "error"
        if (type === "offline" || type === "quota_reached")
            return "muted"
        return "info"
    }

    function entryText(e) {
        var ts = "[" + Qt.formatDateTime(new Date(e.timestamp), "HH:mm:ss") + "]"
        if (e.type === "error")
            return ts + " error: " + (e.detail || "unknown error")
        return ts + " " + (e.detail || e.type)
    }

    function formatBytes(n) {
        if (n <= 0) return "0 B"
        if (n < 1024) return n + " B"
        if (n < 1048576) return (n / 1024).toFixed(1) + " KB"
        if (n < 1073741824) return (n / 1048576).toFixed(1) + " MB"
        return (n / 1073741824).toFixed(2) + " GB"
    }

    function quotaPercent() {
        if (quotaTotal <= 0) return 0
        return Math.min(1.0, quotaUsed / quotaTotal)
    }

    function refresh() {
        if (typeof logos === "undefined" || !logos.callModule) return

        var st = callModuleParse(logos.callModule("stash", "getStatus", []))
        root.coreReady = (st !== null)

        var logRaw = callModuleParse(logos.callModule("stash", "getLog", []))
        if (Array.isArray(logRaw) && logRaw.length > root.logSeenCount) {
            for (var i = root.logSeenCount; i < logRaw.length; i++) {
                var e = logRaw[i]
                if (activityLog.logModel.count >= 200) activityLog.logModel.remove(0)
                activityLog.logModel.append({
                    ts:    "[" + Qt.formatDateTime(new Date(e.timestamp), "HH:mm:ss") + "]",
                    msg:   entryText(e).replace(/^\[[^\]]+\] /, ""),
                    level: entryLevel(e.type),
                    cid:   e.cid || ""
                })
            }
            root.logSeenCount = logRaw.length
        }

        var q = callModuleParse(logos.callModule("stash", "getQuota", []))
        if (q && q.used !== undefined) {
            quotaUsed  = q.used
            quotaTotal = q.total
        }

        var si = callModuleParse(logos.callModule("stash", "getStorageInfo", []))
        if (si) {
            root.logosStorageReady    = si.ready   === true
            root.logosStorageStarting = si.starting === true
            root.logosStoragePeerId   = si.peerId  || ""
        }

        var at = callModuleParse(logos.callModule("stash", "getActiveTransport", []))
        if (at && at.transport) root.activeTransport = at.transport
    }

    // ── Timers ────────────────────────────────────────────────────────────

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: root.refresh()
    }

    Timer {
        id: pinningConfigPoller
        interval: 500
        repeat: true
        running: !root.pinningConfigured
        onTriggered: {
            if (typeof logos === "undefined" || !logos.callModule) return
            var cfg = callModuleParse(logos.callModule("stash", "getPinningConfig", []))
            if (cfg && cfg.configured === true) {
                root.pinningConfigured = true
                pinningConfigPoller.stop()
            }
        }
    }

    Component.onCompleted: {
        root.refresh()
        if (typeof logos !== "undefined" && logos.callModule) {
            var cfg = callModuleParse(logos.callModule("stash", "getPinningConfig", []))
            if (cfg) root.pinningConfigured = cfg.configured === true
            var at = callModuleParse(logos.callModule("stash", "getActiveTransport", []))
            if (at && at.transport) root.activeTransport = at.transport
        }
    }

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

            // Modules gear button
            Rectangle {
                width: 32; height: 32
                radius: 6
                color: gearBtn.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "⚙"
                    font.pixelSize: 15
                    color: root.modulesPanelOpen ? root.accentOrange : root.textSecondary
                }

                MouseArea {
                    id: gearBtn
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.modulesPanelOpen = !root.modulesPanelOpen
                }
            }

            // Pinning config button
            Rectangle {
                width: 32; height: 32
                radius: 6
                color: pinBtn.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "⊕"
                    font.pixelSize: 15
                    color: root.pinningPanelOpen
                           ? root.accentOrange
                           : (root.pinningConfigured ? root.successGreen : root.errorRed)
                }

                MouseArea {
                    id: pinBtn
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.pinningPanelOpen = !root.pinningPanelOpen
                        if (root.pinningPanelOpen && typeof logos !== "undefined" && logos.callModule) {
                            var at = callModuleParse(logos.callModule("stash", "getActiveTransport", []))
                            if (at && at.transport === "logos") {
                                providerCombo.currentIndex = 2
                            } else {
                                var cfg = callModuleParse(logos.callModule("stash", "getPinningConfig", []))
                                if (cfg) {
                                    providerCombo.currentIndex = cfg.provider === "kubo" ? 1 : 0
                                    tokenField.text = ""
                                    tokenField.placeholderText = cfg.token === "***"
                                        ? "Token saved — leave blank to keep"
                                        : "JWT / API token"
                                    endpointField.text = cfg.endpoint || ""
                                    root.pinningConfigured = cfg.configured === true
                                }
                            }
                        }
                    }
                }
            }

            // Logos storage button
            Rectangle {
                width: 32; height: 32
                radius: 6
                color: logosBtn.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.logosPanelOpen ? root.accentOrange : root.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "◈"
                    font.pixelSize: 14
                    color: root.logosPanelOpen     ? root.accentOrange
                         : root.logosStorageReady  ? root.successGreen
                         : root.logosStorageStarting ? root.warningYellow
                         : root.textMuted
                }

                MouseArea {
                    id: logosBtn
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.logosPanelOpen = !root.logosPanelOpen
                }
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

        // ── Watched modules panel (collapsible) ───────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: root.modulesPanelOpen

            Text {
                text: "Watched modules (one per line):"
                font.pixelSize: 11
                color: root.textSecondary
            }

            Rectangle {
                Layout.fillWidth: true
                height: 80
                radius: 4
                color: root.bgSecondary
                border.color: root.borderColor
                border.width: 1

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 4
                    TextArea {
                        id: modulesInput
                        background: null
                        color: root.textPrimary
                        font.pixelSize: 12
                        font.family: "monospace"
                        wrapMode: TextArea.NoWrap
                        placeholderText: "notes\nkeycard"
                        placeholderTextColor: root.textMuted

                        Component.onCompleted: {
                            if (typeof logos !== "undefined" && logos.callModule) {
                                var res = callModuleParse(logos.callModule("stash", "getWatchedModules", []))
                                if (res && Array.isArray(res.modules))
                                    text = res.modules.join("\n")
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.alignment: Qt.AlignRight
                width: 60; height: 26
                radius: 4
                color: saveBtn.containsMouse ? root.accentOrange : root.bgSecondary
                border.color: root.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "Save"
                    font.pixelSize: 11
                    color: root.textPrimary
                }

                MouseArea {
                    id: saveBtn
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (typeof logos === "undefined" || !logos.callModule) return
                        logos.callModule("stash", "setWatchedModules", [modulesInput.text])
                        root.modulesPanelOpen = false
                    }
                }
            }
        }

        // ── Pinning config panel (collapsible) ────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.pinningPanelOpen

            Text {
                text: "Pinning Provider"
                font.pixelSize: 11
                color: root.textSecondary
            }

            ComboBox {
                id: providerCombo
                Layout.fillWidth: true
                model: ["Pinata", "Kubo (self-hosted)", "Logos Network"]
                background: Rectangle {
                    color: root.bgSecondary
                    border.color: root.borderColor
                    border.width: 1
                    radius: 4
                }
                contentItem: Text {
                    leftPadding: 8
                    text: providerCombo.displayText
                    font.pixelSize: 12
                    color: root.textPrimary
                    verticalAlignment: Text.AlignVCenter
                }
                popup.background: Rectangle {
                    color: root.bgSecondary
                    border.color: root.borderColor
                    border.width: 1
                    radius: 4
                }
                delegate: ItemDelegate {
                    width: providerCombo.width
                    contentItem: Text {
                        text: modelData
                        font.pixelSize: 12
                        color: root.textPrimary
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: hovered ? root.borderColor : root.bgSecondary
                    }
                }
            }

            Text {
                text: "Bearer Token"
                font.pixelSize: 11
                color: root.textSecondary
                visible: providerCombo.currentIndex !== 2
            }

            Rectangle {
                Layout.fillWidth: true
                height: 32
                radius: 4
                color: root.bgSecondary
                border.color: root.borderColor
                border.width: 1
                visible: providerCombo.currentIndex !== 2

                TextField {
                    id: tokenField
                    anchors.fill: parent
                    anchors.margins: 4
                    background: null
                    color: root.textPrimary
                    font.pixelSize: 12
                    echoMode: TextInput.Password
                    placeholderText: "JWT / API token"
                    placeholderTextColor: root.textMuted
                }
            }

            Text {
                text: "Node URL"
                font.pixelSize: 11
                color: root.textSecondary
                visible: providerCombo.currentIndex === 1
            }

            Rectangle {
                Layout.fillWidth: true
                height: 32
                radius: 4
                color: root.bgSecondary
                border.color: root.borderColor
                border.width: 1
                visible: providerCombo.currentIndex === 1

                TextField {
                    id: endpointField
                    anchors.fill: parent
                    anchors.margins: 4
                    background: null
                    color: root.textPrimary
                    font.pixelSize: 12
                    placeholderText: "https://node.example.com"
                    placeholderTextColor: root.textMuted
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: providerCombo.currentIndex === 2
                          ? (root.logosStorageReady ? "● Logos ready" : root.logosStorageStarting ? "● Logos starting…" : "● Logos offline — init failed")
                          : (root.pinningConfigured ? "● Configured" : "● Not configured — backups will fail")
                    font.pixelSize: 11
                    color: providerCombo.currentIndex === 2
                           ? (root.logosStorageReady ? root.successGreen : root.logosStorageStarting ? root.warningYellow : root.errorRed)
                           : (root.pinningConfigured ? root.successGreen : root.errorRed)
                    Layout.fillWidth: true
                }

                Rectangle {
                    width: 60; height: 26
                    radius: 4
                    color: pinSaveBtn.containsMouse ? root.accentOrange : root.bgSecondary
                    border.color: root.borderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Save"
                        font.pixelSize: 11
                        color: root.textPrimary
                    }

                    MouseArea {
                        id: pinSaveBtn
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (typeof logos === "undefined" || !logos.callModule) return
                            if (providerCombo.currentIndex === 2) {
                                // Logos Network — set active transport
                                var tres = callModuleParse(
                                    logos.callModule("stash", "setActiveTransport", ["logos"]))
                                if (tres && tres.ok) {
                                    root.activeTransport = "logos"
                                    root.pinningConfigured = true
                                    root.pinningPanelOpen = false
                                }
                            } else {
                                var provider = providerCombo.currentIndex === 0 ? "pinata" : "kubo"
                                var endpoint = endpointField.text.trim()
                                var token    = tokenField.text.trim()
                                var res = callModuleParse(logos.callModule("stash", "setPinningConfig",
                                                                           [provider, endpoint, token]))
                                if (res && res.ok) {
                                    logos.callModule("stash", "setActiveTransport",
                                                     [provider === "pinata" ? "pinata" : "kubo"])
                                    root.activeTransport = provider
                                    root.pinningConfigured = true
                                    root.pinningPanelOpen = false
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Logos storage panel (collapsible) ────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.logosPanelOpen

            // Status row
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "Logos Storage"
                    font.pixelSize: 11
                    color: root.textSecondary
                }

                Rectangle {
                    width: 6; height: 6; radius: 3
                    color: root.logosStorageReady   ? root.successGreen
                         : root.logosStorageStarting ? root.warningYellow
                         : root.textMuted
                }

                Text {
                    text: root.logosStorageReady   ? "Ready"
                        : root.logosStorageStarting ? "Starting…"
                        : "Offline"
                    font.pixelSize: 11
                    color: root.logosStorageReady   ? root.successGreen
                         : root.logosStorageStarting ? root.warningYellow
                         : root.textMuted
                }

                Item { Layout.fillWidth: true }
            }

            // Peer ID (shown when ready)
            Text {
                visible: root.logosStoragePeerId.length > 0
                text: "Peer: " + root.logosStoragePeerId.substring(0, 20) + "…"
                font.pixelSize: 10
                font.family: "Courier New, monospace"
                color: root.textMuted
            }

            // Transport selector
            Text {
                text: "Active transport"
                font.pixelSize: 11
                color: root.textSecondary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: [
                        { label: "Logos",  value: "logos"  },
                        { label: "Kubo",   value: "kubo"   },
                        { label: "Pinata", value: "pinata" }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool selected: root.activeTransport === modelData.value
                        readonly property bool isLogos:  modelData.value === "logos"

                        width: 64; height: 26
                        radius: 4
                        color: selected
                               ? root.accentOrange
                               : (transportArea.containsMouse ? root.bgSecondary : "transparent")
                        border.color: selected ? root.accentOrange : root.borderColor
                        border.width: 1
                        // Disable Logos option when not ready
                        opacity: isLogos && !root.logosStorageReady ? 0.4 : 1.0

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: 11
                            color: selected ? root.bgPrimary : root.textSecondary
                        }

                        MouseArea {
                            id: transportArea
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !(isLogos && !root.logosStorageReady)
                            onClicked: {
                                if (typeof logos === "undefined" || !logos.callModule) return
                                var res = callModuleParse(
                                    logos.callModule("stash", "setActiveTransport",
                                                     [modelData.value]))
                                if (res && res.ok) root.activeTransport = modelData.value
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // ── Activity log (keycard/notes style) ────────────────────────────
        Rectangle {
            id: activityLog
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0D0D0D"
            radius: 4

            property alias logModel: logListView.model

            TextEdit { id: clipHelper; visible: false }

            function copyAllToClipboard() {
                var text = ""
                for (var i = 0; i < logModel.count; i++) {
                    var e = logModel.get(i)
                    text += e.ts + " " + e.msg + "\n"
                }
                clipHelper.text = text
                clipHelper.selectAll()
                clipHelper.copy()
                copyFeedback.restart()
            }

            // Top border
            Rectangle {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 1
                color: root.borderColor
                radius: 0
            }

            // Copy-all button
            Rectangle {
                id: copyBtn
                anchors { top: parent.top; right: parent.right; topMargin: 6; rightMargin: 8 }
                width: 20; height: 20
                color: "transparent"
                opacity: copyBtnArea.containsMouse ? 0.8 : 0.4
                Behavior on opacity { NumberAnimation { duration: 150 } }

                Rectangle {
                    x: 3; y: 5; width: 10; height: 10
                    color: "transparent"
                    border.color: root.textSecondary; border.width: 1; radius: 2
                }
                Rectangle {
                    x: 6; y: 2; width: 10; height: 10
                    color: "#0D0D0D"
                    border.color: root.textSecondary; border.width: 1; radius: 2
                }

                MouseArea {
                    id: copyBtnArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: activityLog.copyAllToClipboard()
                }

                Timer {
                    id: copyFeedback
                    interval: 1200
                    onTriggered: copyBtn.opacity = copyBtnArea.containsMouse ? 0.8 : 0.4
                }
            }

            // Empty placeholder
            Text {
                anchors.centerIn: parent
                visible: logListView.model.count === 0
                text: "No activity yet"
                color: root.textMuted
                font.pixelSize: 11
                font.family: "Courier New, monospace"
            }

            // Log entries
            ListView {
                id: logListView
                anchors { fill: parent; margins: 10; topMargin: 14 }
                clip: true
                spacing: 2
                onCountChanged: Qt.callLater(() => logListView.positionViewAtEnd())

                model: ListModel {}

                delegate: TextEdit {
                    required property string ts
                    required property string msg
                    required property string level
                    required property string cid
                    width: logListView.width
                    text: ts + " " + msg
                    color: level === "success" ? root.successGreen
                         : level === "error"   ? root.errorRed
                         : level === "muted"   ? root.textMuted
                         : root.textSecondary
                    font.pixelSize: 11
                    font.family: "Courier New, monospace"
                    wrapMode: Text.WrapAnywhere
                    readOnly: true
                    selectByMouse: true
                    selectedTextColor: root.bgPrimary
                    selectionColor: root.textSecondary
                }
            }
        }
    }
}
