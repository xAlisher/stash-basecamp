import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Palette (aligned with notes/keycard) ──────────────────────────────
    readonly property color bgPrimary:      "#171717"
    readonly property color bgSecondary:    "#262626"
    readonly property color bgActive:       "#332A27"
    readonly property color textPrimary:    "#FFFFFF"
    readonly property color textSecondary:  "#A4A4A4"
    readonly property color textMuted:      "#5D5D5D"
    readonly property color accentOrange:   "#FF5000"
    readonly property color successGreen:   "#22C55E"
    readonly property color errorRed:       "#FB3748"
    readonly property color warningYellow:  "#FFC107"
    readonly property color borderColor:    "#383838"

    // ── State ─────────────────────────────────────────────────────────────
    property bool   pollBusy:             false
    property bool   settingsPanelOpen:    false
    property bool   transportPopupOpen:   false
    property bool   pinningConfigured:    false
    property bool   logosStorageReady:    false
    property bool   logosStorageStarting: false
    property string activeTransport:      "kubo"
    property string logosStoragePeerId:   ""
    property int    logSeenCount:         0

    // Watched modules list model
    ListModel { id: watchedModulesModel }

    // ── Helpers ───────────────────────────────────────────────────────────

    function callModuleParse(raw) {
        try {
            var tmp = JSON.parse(raw)
            if (typeof tmp === "string") {
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

    function transportLabel(t) {
        if (t === "logos")  return "Logos Storage"
        if (t === "kubo")   return "Kubo"
        if (t === "pinata") return "Pinata"
        return t
    }

    function transportDotColor() {
        if (root.activeTransport !== "logos") return root.textMuted
        if (root.logosStorageReady)    return root.successGreen
        if (root.logosStorageStarting) return root.warningYellow
        return root.errorRed
    }

    function refreshModulesList() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var res = callModuleParse(logos.callModule("stash", "getWatchedModules", []))
        if (res && Array.isArray(res.modules)) {
            watchedModulesModel.clear()
            for (var i = 0; i < res.modules.length; i++)
                watchedModulesModel.append({ name: res.modules[i] })
        }
    }

    function refresh() {
        if (root.pollBusy) return
        root.pollBusy = true

        if (typeof logos === "undefined" || !logos.callModule) {
            root.pollBusy = false
            return
        }

        var logRaw = callModuleParse(logos.callModule("stash", "getLog", []))
        if (Array.isArray(logRaw) && logRaw.length > root.logSeenCount) {
            for (var i = root.logSeenCount; i < logRaw.length; i++) {
                var e = logRaw[i]
                if (logListView.model.count >= 200)
                    logListView.model.remove(0)
                logListView.model.append({
                    ts:    "[" + Qt.formatDateTime(new Date(e.timestamp), "HH:mm:ss") + "]",
                    msg:   entryText(e).replace(/^\[[^\]]+\] /, ""),
                    level: entryLevel(e.type)
                })
            }
            root.logSeenCount = logRaw.length
        }

        var si = callModuleParse(logos.callModule("stash", "getStorageInfo", []))
        if (si) {
            root.logosStorageReady    = si.ready    === true
            root.logosStorageStarting = si.starting === true
            root.logosStoragePeerId   = si.peerId   || ""
        }

        var at = callModuleParse(logos.callModule("stash", "getActiveTransport", []))
        if (at && at.transport) root.activeTransport = at.transport

        root.pollBusy = false
    }

    // ── Timers ────────────────────────────────────────────────────────────

    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: root.refresh()
    }

    Component.onCompleted: {
        root.refresh()
        root.refreshModulesList()
    }

    // ── Root background ───────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: root.bgPrimary
    }

    // ── Layout ────────────────────────────────────────────────────────────

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── Header ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            ColumnLayout {
                spacing: 2
                Text {
                    text: "Stash"
                    font.pixelSize: 20
                    font.bold: true
                    color: root.textPrimary
                }
                Text {
                    text: "Pins files from the listed modules to selected decentralised storage."
                    font.pixelSize: 11
                    color: root.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillWidth: true }

            // Transport status pill
            Rectangle {
                id: transportPill
                height: 28
                width: pillRow.implicitWidth + 20
                radius: 14
                color: transportPillArea.containsMouse ? root.bgSecondary : Qt.rgba(0.149, 0.149, 0.149, 0.85)
                border.color: root.transportPopupOpen ? root.accentOrange : root.borderColor
                border.width: 1

                Row {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: 6

                    Rectangle {
                        width: 7; height: 7; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.transportDotColor()
                    }

                    Text {
                        text: root.transportLabel(root.activeTransport)
                        font.pixelSize: 11
                        color: root.textPrimary
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: "▾"
                        font.pixelSize: 9
                        color: root.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: transportPillArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.transportPopupOpen = !root.transportPopupOpen
                        root.settingsPanelOpen  = false
                    }
                }
            }

            // Gear icon
            Rectangle {
                width: 28; height: 28
                radius: 6
                color: gearArea.containsMouse ? root.bgSecondary : "transparent"
                border.color: root.settingsPanelOpen ? root.accentOrange : root.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "⚙"
                    font.pixelSize: 14
                    color: root.settingsPanelOpen ? root.accentOrange : root.textSecondary
                }

                MouseArea {
                    id: gearArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.settingsPanelOpen  = !root.settingsPanelOpen
                        root.transportPopupOpen = false
                        if (root.settingsPanelOpen)
                            root.refreshModulesList()
                    }
                }
            }
        }

        // ── Transport dropdown ────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            visible: root.transportPopupOpen
            height: transportDropCol.implicitHeight + 16
            color: root.bgSecondary
            radius: 6
            border.color: root.borderColor
            border.width: 1

            ColumnLayout {
                id: transportDropCol
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 8 }
                spacing: 4

                // Logos Storage option
                Repeater {
                    model: [
                        { label: "Logos Storage", value: "logos",
                          status: root.logosStorageReady ? "Ready"
                                : root.logosStorageStarting ? "Starting…"
                                : "Offline",
                          statusColor: root.logosStorageReady ? root.successGreen
                                     : root.logosStorageStarting ? root.warningYellow
                                     : root.textMuted },
                        { label: "Kubo",   value: "kubo",   status: "", statusColor: root.textMuted },
                        { label: "Pinata", value: "pinata", status: "", statusColor: root.textMuted }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool selected: root.activeTransport === modelData.value
                        Layout.fillWidth: true
                        height: 34
                        radius: 4
                        color: selected ? root.bgActive
                             : optionArea.containsMouse ? Qt.rgba(0.22, 0.22, 0.22, 1) : "transparent"

                        RowLayout {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            spacing: 8

                            Rectangle {
                                width: 6; height: 6; radius: 3
                                color: modelData.statusColor.toString().length > 0
                                       ? modelData.statusColor : root.textMuted
                                visible: modelData.value === "logos"
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: modelData.label
                                font.pixelSize: 12
                                color: selected ? root.textPrimary : root.textSecondary
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                visible: modelData.status.length > 0
                                text: modelData.status
                                font.pixelSize: 10
                                color: modelData.statusColor
                            }

                            Text {
                                visible: selected
                                text: "✓"
                                font.pixelSize: 11
                                color: root.accentOrange
                            }
                        }

                        MouseArea {
                            id: optionArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (typeof logos === "undefined" || !logos.callModule) return
                                var res = callModuleParse(
                                    logos.callModule("stash", "setActiveTransport", [modelData.value]))
                                if (res && res.ok) {
                                    root.activeTransport = modelData.value
                                    root.transportPopupOpen = false
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Settings panel ────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            visible: root.settingsPanelOpen
            height: settingsCol.implicitHeight + 20
            color: root.bgSecondary
            radius: 6
            border.color: root.borderColor
            border.width: 1

            ColumnLayout {
                id: settingsCol
                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 }
                spacing: 8

                Text {
                    text: "Watched modules"
                    font.pixelSize: 11
                    color: root.textSecondary
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 72
                    radius: 4
                    color: root.bgPrimary
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        width: 64; height: 26
                        radius: 4
                        color: cancelBtn.containsMouse ? Qt.rgba(0.22,0.22,0.22,1) : "transparent"
                        border.color: root.borderColor; border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "Cancel"
                            font.pixelSize: 11
                            color: root.textSecondary
                        }
                        MouseArea {
                            id: cancelBtn
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.settingsPanelOpen = false
                        }
                    }

                    Rectangle {
                        width: 64; height: 26
                        radius: 4
                        color: saveModBtn.containsMouse ? "#CC4000" : root.accentOrange

                        Text {
                            anchors.centerIn: parent
                            text: "Save"
                            font.pixelSize: 11
                            color: root.textPrimary
                        }
                        MouseArea {
                            id: saveModBtn
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (typeof logos === "undefined" || !logos.callModule) return
                                logos.callModule("stash", "setWatchedModules", [modulesInput.text])
                                root.settingsPanelOpen = false
                                root.refreshModulesList()
                            }
                        }
                    }
                }

                // Pinning config (kubo/pinata only)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.activeTransport !== "logos"

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
                    }

                    Text {
                        text: "Pinning config"
                        font.pixelSize: 11
                        color: root.textSecondary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 32
                        radius: 4
                        color: root.bgPrimary
                        border.color: root.borderColor; border.width: 1

                        TextField {
                            id: tokenField
                            anchors.fill: parent; anchors.margins: 4
                            background: null
                            color: root.textPrimary
                            font.pixelSize: 12
                            echoMode: TextInput.Password
                            placeholderText: "Bearer token (Pinata JWT or Kubo auth)"
                            placeholderTextColor: root.textMuted
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 32
                        radius: 4
                        color: root.bgPrimary
                        border.color: root.borderColor; border.width: 1
                        visible: root.activeTransport === "kubo"

                        TextField {
                            id: endpointField
                            anchors.fill: parent; anchors.margins: 4
                            background: null
                            color: root.textPrimary
                            font.pixelSize: 12
                            placeholderText: "Node URL (https://...)"
                            placeholderTextColor: root.textMuted
                        }
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignRight
                        width: 64; height: 26
                        radius: 4
                        color: pinSaveBtn2.containsMouse ? "#CC4000" : root.accentOrange

                        Text {
                            anchors.centerIn: parent
                            text: "Save"
                            font.pixelSize: 11
                            color: root.textPrimary
                        }
                        MouseArea {
                            id: pinSaveBtn2
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (typeof logos === "undefined" || !logos.callModule) return
                                var provider = root.activeTransport === "pinata" ? "pinata" : "kubo"
                                var res = callModuleParse(
                                    logos.callModule("stash", "setPinningConfig",
                                                     [provider, endpointField.text.trim(), tokenField.text.trim()]))
                                if (res && res.ok) root.settingsPanelOpen = false
                            }
                        }
                    }
                }
            }
        }

        // ── Main content: Modules + Logs ──────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ── Modules column ────────────────────────────────────────────
            ColumnLayout {
                Layout.preferredWidth: 200
                Layout.minimumWidth: 160
                Layout.fillHeight: true
                spacing: 6

                Text {
                    text: "Modules"
                    font.pixelSize: 13
                    font.bold: true
                    color: root.textPrimary
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.bgSecondary
                    radius: 6
                    border.color: root.borderColor
                    border.width: 1
                    clip: true

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: watchedModulesModel.count === 0
                        text: "No modules\nwatched"
                        horizontalAlignment: Text.AlignHCenter
                        color: root.textMuted
                        font.pixelSize: 11
                    }

                    ListView {
                        id: moduleListView
                        anchors { fill: parent; topMargin: 4; bottomMargin: 4 }
                        clip: true
                        model: watchedModulesModel
                        spacing: 2

                        delegate: Rectangle {
                            required property string name
                            required property int index
                            width: moduleListView.width
                            height: 36
                            color: moduleRowArea.containsMouse ? root.bgActive : "transparent"
                            radius: 4

                            RowLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                                spacing: 6

                                Text {
                                    text: name
                                    font.pixelSize: 12
                                    color: root.textPrimary
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                // Backup button
                                Rectangle {
                                    width: 26; height: 26
                                    radius: 4
                                    color: backupBtnArea.containsMouse
                                           ? Qt.rgba(1, 0.314, 0, 0.2) : "transparent"
                                    border.color: backupBtnArea.containsMouse
                                                  ? root.accentOrange : "transparent"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "⬆"
                                        font.pixelSize: 12
                                        color: backupBtnArea.containsMouse
                                               ? root.accentOrange : root.textMuted
                                    }

                                    MouseArea {
                                        id: backupBtnArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (typeof logos === "undefined" || !logos.callModule) return
                                            logos.callModule("stash", "checkAll", [])
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                id: moduleRowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                // Pass through clicks to children
                                onClicked: { }
                            }
                        }
                    }
                }
            }

            // ── Logs column ───────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "Logs"
                        font.pixelSize: 13
                        font.bold: true
                        color: root.textPrimary
                    }

                    Item { Layout.fillWidth: true }

                    // Copy-all button
                    Rectangle {
                        width: 26; height: 26
                        radius: 4
                        color: copyBtnArea.containsMouse ? root.bgSecondary : "transparent"
                        border.color: copyBtnArea.containsMouse ? root.borderColor : "transparent"
                        border.width: 1
                        opacity: copyBtnArea.containsMouse ? 1.0 : 0.5
                        Behavior on opacity { NumberAnimation { duration: 150 } }

                        // Clipboard icon (two overlapping squares)
                        Rectangle {
                            x: 4; y: 7; width: 10; height: 11
                            color: "transparent"
                            border.color: root.textSecondary; border.width: 1; radius: 1
                        }
                        Rectangle {
                            x: 7; y: 4; width: 10; height: 11
                            color: copyBtnArea.containsMouse ? root.bgSecondary : root.bgPrimary
                            border.color: root.textSecondary; border.width: 1; radius: 1
                        }

                        MouseArea {
                            id: copyBtnArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var text = "# stash log\n"
                                for (var i = 0; i < logListView.model.count; i++) {
                                    var e = logListView.model.get(i)
                                    text += e.ts + " " + e.msg + "\n"
                                }
                                clipHelper.text = text
                                clipHelper.selectAll()
                                clipHelper.copy()
                                copyDoneTimer.restart()
                            }
                        }

                        Timer {
                            id: copyDoneTimer
                            interval: 1200
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0D0D0D"
                    radius: 6
                    border.color: root.borderColor
                    border.width: 1
                    clip: true

                    // Clipboard helper — opacity:0 not visible:false (copy needs rendering)
                    TextEdit {
                        id: clipHelper
                        opacity: 0; width: 0; height: 0
                    }

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: logListView.model.count === 0
                        text: "No activity yet"
                        color: root.textMuted
                        font.pixelSize: 11
                        font.family: "Courier New, monospace"
                    }

                    ListView {
                        id: logListView
                        anchors { fill: parent; margins: 10 }
                        clip: true
                        spacing: 2
                        onCountChanged: Qt.callLater(() => logListView.positionViewAtEnd())

                        model: ListModel {}

                        delegate: TextEdit {
                            required property string ts
                            required property string msg
                            required property string level
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

        // ── Footer ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "Get Stash button for your module"
                font.pixelSize: 11
                color: footerLinkArea.containsMouse ? root.accentOrange : root.textSecondary
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            Text {
                text: " ↗"
                font.pixelSize: 11
                color: footerLinkArea.containsMouse ? root.accentOrange : root.textMuted
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            MouseArea {
                id: footerLinkArea
                // Cover both text items
                x: 0
                width: 210; height: 20
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                // Opens integration docs — handled by platform shell if available
                onClicked: { }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
